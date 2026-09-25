// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 13: get_server_health (V5-16), get_server_capabilities (V5-17) and list_worlds (V5-19), initial slices.

#include "MCPDiagnostics.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPHttpServer.h"
#include "MCPSettings.h"
#include "MCPEditorSurfaces.h"
#include "MCPActorChangePlans.h"
#include "MCPSnapshots.h"
#include "MCPScenarios.h"
#include "MCPResultStore.h"
#include "MCPResourceProvider.h"
#include "MCPPromptProvider.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "RHI.h"
#include "ShaderCompiler.h"
#include "MCPProtocol.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "GenericPlatform/GenericPlatformOutputDevices.h"
#include "Misc/ScopeLock.h"

namespace MCPDiagnostics
{
namespace
{
double StartedAt = 0.0;

// ---- v5 increment 20: tool timing telemetry ----
struct FCallRecord { FString Tool; double Ms = 0.0; bool bOk = true; FString At; };
struct FToolAgg { int64 Calls = 0, Failures = 0; double TotalMs = 0.0, MaxMs = 0.0; };
FCriticalSection TimingLock;
TArray<FCallRecord> Ring; int32 RingNext = 0; int64 CallsTotal = 0;
TMap<FString, FToolAgg> Aggregates;
constexpr int32 RingCapacity = 256;

// ---- v5 increment 20: diagnostic bundles ----
struct FBundle { FString Id, Principal, Session, Dir, ManifestJson; double CreatedAt = 0.0; int64 Bytes = 0; };
TMap<FString, FBundle> Bundles;
constexpr int32 MaxBundles = 16;

FString Redact(FString Text, int32& Count)
{
    const UMCPSettings* S = UMCPSettings::Get();
    TArray<FString> Secrets;
    if (S) { if (S->FalAIApiKey.Len() >= 6) Secrets.Add(S->FalAIApiKey); for (const FString& T : S->AuthTokens) if (T.Len() >= 6) Secrets.Add(T); }
    for (const FString& Sec : Secrets) { const int32 Before = Text.Len(); Text.ReplaceInline(*Sec, TEXT("[REDACTED]")); if (Text.Len() != Before) ++Count; }
    // Authorization headers and bearer/key tokens in log lines.
    TArray<FString> Lines; Text.ParseIntoArrayLines(Lines, false); bool bChanged = false;
    for (FString& L : Lines)
    {
        int32 Idx = L.Find(TEXT("Authorization"), ESearchCase::IgnoreCase);
        if (Idx != INDEX_NONE) { L = L.Left(Idx) + TEXT("Authorization: [REDACTED]"); ++Count; bChanged = true; }
    }
    return bChanged ? FString::Join(Lines, TEXT("\n")) : Text;
}

FString ReadLogTail(int32 KiloBytes, FString& OutNote)
{
    if (KiloBytes <= 0) return FString();
    const FString LogFile = FGenericPlatformOutputDevices::GetAbsoluteLogFilename();
    IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
    TUniquePtr<IFileHandle> H(PF.OpenRead(*LogFile, true));
    if (!H) { OutNote = TEXT("log file not readable: ") + LogFile; return FString(); }
    const int64 Size = H->Size(); const int64 Want = FMath::Min<int64>(Size, (int64)KiloBytes * 1024);
    if (Want <= 0) return FString();
    H->Seek(Size - Want);
    TArray<uint8> Buf; Buf.SetNumUninitialized((int32)Want);
    if (!H->Read(Buf.GetData(), Want)) { OutNote = TEXT("log read failed"); return FString(); }
    FString Text;
    if (Want >= 2 && Buf[0] == 0xFF && Buf[1] == 0xFE) Text = FString((int32)(Want / 2), (const TCHAR*)Buf.GetData());
    else if (Want >= 2 && ((Buf[1] == 0 && Buf[3] == 0) )) Text = FString((int32)(Want / 2), (const UTF16CHAR*)Buf.GetData());
    else { FUTF8ToTCHAR Conv((const ANSICHAR*)Buf.GetData(), (int32)Want); Text = FString(Conv.Length(), Conv.Get()); }
    OutNote = LogFile;
    return Text;
}

const TCHAR* WorldTypeName(EWorldType::Type T)
{
    switch (T)
    {
    case EWorldType::Editor: return TEXT("editor");
    case EWorldType::PIE: return TEXT("pie");
    case EWorldType::Game: return TEXT("game");
    case EWorldType::EditorPreview: return TEXT("editor_preview");
    case EWorldType::GamePreview: return TEXT("game_preview");
    case EWorldType::GameRPC: return TEXT("game_rpc");
    case EWorldType::Inactive: return TEXT("inactive");
    default: return TEXT("none");
    }
}
TSharedPtr<FJsonObject> PluginJson(const TCHAR* Name, const TCHAR* Provides)
{
    auto O = MakeShared<FJsonObject>();
    const TSharedPtr<IPlugin> P = IPluginManager::Get().FindPlugin(Name);
    O->SetStringField(TEXT("plugin"), Name); O->SetStringField(TEXT("provides"), Provides);
    O->SetBoolField(TEXT("found"), P.IsValid()); O->SetBoolField(TEXT("enabled"), P.IsValid() && P->IsEnabled());
    if (!P.IsValid()) O->SetStringField(TEXT("reason"), TEXT("plugin not installed in this engine/project"));
    else if (!P->IsEnabled()) O->SetStringField(TEXT("reason"), TEXT("plugin installed but disabled"));
    return O;
}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    if (StartedAt == 0.0) StartedAt = FPlatformTime::Seconds();

    MCP_TOOL(Registry, "get_server_health")
        .Description(TEXT("Bounded, redacted server health: listener state and port, ready/busy/degraded classification, uptime, tool/resource/prompt counts, PIE state, active editor transaction, shader compilation, and owned-resource counts for observers, plans and verification records. No secrets, no payloads."))
        .ReadOnly().Idempotent()
        .EnumArg(TEXT("detail"), TEXT("basic (default) or diagnostic"), { TEXT("basic"), TEXT("diagnostic") })
        .OutputSchema(TEXT(R"({"type":"object","required":["state","listening","port","uptime_seconds","tool_count","pie_active","server_version","protocol_version"],"properties":{"state":{"type":"string"},"listening":{"type":"boolean"},"port":{"type":"integer"},"uptime_seconds":{"type":"number"},"tool_count":{"type":"integer"},"resource_count":{"type":"integer"},"prompt_count":{"type":"integer"},"pie_active":{"type":"boolean"},"transaction_active":{"type":"boolean"},"shaders_compiling":{"type":"boolean"},"server_version":{"type":"string"},"protocol_version":{"type":"string"},"engine_version":{"type":"string"},"visual":{"type":"object"},"change_plans":{"type":"object"},"degraded_reasons":{"type":"array"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&)
        {
            const bool bDiagnostic = Args->HasField(TEXT("detail")) && Args->GetStringField(TEXT("detail")) == TEXT("diagnostic");
            auto Out = MakeShared<FJsonObject>();
            const bool bListening = FMCPHttpServer::Get().IsRunning();
            const bool bPie = GEditor && GEditor->IsPlaySessionInProgress();
            const bool bTxn = GEditor && GEditor->IsTransactionActive();
            const bool bShaders = GShaderCompilingManager && GShaderCompilingManager->IsCompiling();
            TArray<TSharedPtr<FJsonValue>> Degraded;
            if (!GEditor) Degraded.Add(MakeShared<FJsonValueString>(TEXT("no_editor")));
            if (GUsingNullRHI) Degraded.Add(MakeShared<FJsonValueString>(TEXT("null_rhi_no_visual_backend")));
            FString State = !bListening ? TEXT("stopped") : (bTxn || bPie ? TEXT("busy") : (Degraded.Num() ? TEXT("degraded") : TEXT("listening")));
            Out->SetStringField(TEXT("state"), State); Out->SetBoolField(TEXT("listening"), bListening); Out->SetNumberField(TEXT("port"), FMCPHttpServer::Get().GetPort());
            Out->SetNumberField(TEXT("uptime_seconds"), FPlatformTime::Seconds() - StartedAt);
            Out->SetNumberField(TEXT("tool_count"), FMCPToolRegistry::Get().GetToolCount());
            Out->SetNumberField(TEXT("resource_count"), FMCPResourceProvider::Get().GetAllResources().Num());
            Out->SetNumberField(TEXT("resource_template_count"), FMCPResourceProvider::Get().GetAllTemplates().Num());
            Out->SetNumberField(TEXT("prompt_count"), FMCPPromptProvider::Get().GetAllPrompts().Num());
            Out->SetBoolField(TEXT("pie_active"), bPie); Out->SetBoolField(TEXT("transaction_active"), bTxn); Out->SetBoolField(TEXT("shaders_compiling"), bShaders);
            Out->SetStringField(TEXT("server_version"), MCPProtocol::ServerVersion); Out->SetStringField(TEXT("protocol_version"), MCPProtocol::Version);
            Out->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
            Out->SetArrayField(TEXT("degraded_reasons"), Degraded);
            if (bDiagnostic)
            {
                Out->SetObjectField(TEXT("visual"), MCPEditorSurfaces::DiagnosticsJson());
                Out->SetObjectField(TEXT("change_plans"), MCPActorChangePlans::DiagnosticsJson());
                Out->SetObjectField(TEXT("snapshots"), MCPSnapshots::DiagnosticsJson());
                Out->SetObjectField(TEXT("operations"), MCPScenarios::DiagnosticsJson());
                Out->SetObjectField(TEXT("tool_timing"), ToolTimingJson());
                Out->SetObjectField(TEXT("results"), MCPResultStore::DiagnosticsJson());
                auto B = MakeShared<FJsonObject>(); B->SetNumberField(TEXT("bundles_retained"), Bundles.Num()); B->SetNumberField(TEXT("bundles_capacity"), MaxBundles); Out->SetObjectField(TEXT("bundles"), B);
                Out->SetStringField(TEXT("project"), FApp::GetProjectName());
            }
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Server %s on port %d; %d tools; PIE %s."), *State, FMCPHttpServer::Get().GetPort(), FMCPToolRegistry::Get().GetToolCount(), bPie ? TEXT("active") : TEXT("inactive")), Out);
        });

    MCP_TOOL(Registry, "get_server_capabilities")
        .Description(TEXT("Report which optional providers and features are actually available in this editor process, with reasons when they are not: visual capture backend, Python, Epic's ModelContextProtocol toolsets, and the optional plugins the tool families depend on (GameplayAbilities, Metasound, MetaHuman, PCG, Niagara, EnhancedInput, GeometryScripting, Fracture, ChaosClothAsset). Also reports tool exposure mode and enabled categories. Nothing is loaded or enabled by this call."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("feature"), TEXT("Optional single feature/plugin name to report"))
        .OutputSchema(TEXT(R"({"type":"object","required":["server_version","protocol_version","visual_backend","providers","exposure_mode"],"properties":{"server_version":{"type":"string"},"protocol_version":{"type":"string"},"engine_version":{"type":"string"},"platform":{"type":"string"},"visual_backend":{"type":"object"},"providers":{"type":"array"},"exposure_mode":{"type":"string"},"enabled_categories":{"type":"array"},"supported_modes":{"type":"array"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&)
        {
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("server_version"), MCPProtocol::ServerVersion); Out->SetStringField(TEXT("protocol_version"), MCPProtocol::Version);
            Out->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString()); Out->SetStringField(TEXT("platform"), FPlatformProperties::IniPlatformName());
            auto Visual = MakeShared<FJsonObject>();
            const bool bVisual = !GUsingNullRHI && FApp::CanEverRender();
            Visual->SetBoolField(TEXT("available"), bVisual); Visual->SetStringField(TEXT("backend"), bVisual ? TEXT("slate_widget") : TEXT("none"));
            if (!bVisual) Visual->SetStringField(TEXT("reason"), GUsingNullRHI ? TEXT("NullRHI process") : TEXT("process cannot render"));
            Out->SetObjectField(TEXT("visual_backend"), Visual);
            TArray<TSharedPtr<FJsonValue>> Providers;
            auto Add = [&](const TSharedPtr<FJsonObject>& P) { Providers.Add(MakeShared<FJsonValueObject>(P)); };
            {
                auto P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("plugin"), TEXT("PythonScriptPlugin")); P->SetStringField(TEXT("provides"), TEXT("execute_python (Destructive scope)"));
                const bool bLoaded = FModuleManager::Get().IsModuleLoaded(TEXT("PythonScriptPlugin"));
                P->SetBoolField(TEXT("found"), IPluginManager::Get().FindPlugin(TEXT("PythonScriptPlugin")).IsValid()); P->SetBoolField(TEXT("enabled"), bLoaded);
                if (!bLoaded) P->SetStringField(TEXT("reason"), TEXT("module not loaded"));
                Add(P);
            }
            {
                auto P = PluginJson(TEXT("ModelContextProtocol"), TEXT("epic_list_toolsets / epic_call_tool imports"));
                P->SetBoolField(TEXT("module_loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("ModelContextProtocol"))); Add(P);
            }
            Add(PluginJson(TEXT("GameplayAbilities"), TEXT("GAS tools")));
            { auto SI = PluginJson(TEXT("SlateInspectorToolset"), TEXT("evaluated native Slate Inspector adapter; not adopted: the standalone slate_widget backend is the shipped capture path")); SI->SetStringField(TEXT("decision"), TEXT("not_adopted")); Add(SI); }
            Add(PluginJson(TEXT("Metasound"), TEXT("MetaSound and MetaSoundGraph tools")));
            Add(PluginJson(TEXT("MetaHuman"), TEXT("MetaHuman tools")));
            Add(PluginJson(TEXT("PCG"), TEXT("PCG tools")));
            Add(PluginJson(TEXT("Niagara"), TEXT("Niagara tools")));
            Add(PluginJson(TEXT("EnhancedInput"), TEXT("Enhanced Input tools")));
            Add(PluginJson(TEXT("GeometryScripting"), TEXT("Modeling tools")));
            Add(PluginJson(TEXT("Fracture"), TEXT("Chaos fracture tools")));
            Add(PluginJson(TEXT("ChaosClothAsset"), TEXT("Chaos cloth tools")));
            FString Only; Args->TryGetStringField(TEXT("feature"), Only);
            if (!Only.IsEmpty()) Providers = Providers.FilterByPredicate([&](const TSharedPtr<FJsonValue>& V) { return V->AsObject()->GetStringField(TEXT("plugin")).Equals(Only, ESearchCase::IgnoreCase); });
            Out->SetArrayField(TEXT("providers"), Providers);
            const UMCPSettings* Settings = UMCPSettings::Get();
            Out->SetStringField(TEXT("exposure_mode"), Settings && Settings->ToolExposureMode == EMCPToolExposureMode::Catalog ? TEXT("catalog") : TEXT("full"));
            TArray<TSharedPtr<FJsonValue>> Cats;
            for (const auto& Pair : FMCPToolRegistry::Get().GetToolsByCategory()) Cats.Add(MakeShared<FJsonValueString>(Pair.Key.ToString()));
            Out->SetArrayField(TEXT("enabled_categories"), Cats);
            TArray<TSharedPtr<FJsonValue>> Modes; for (const TCHAR* M : { TEXT("preview"), TEXT("apply"), TEXT("observe"), TEXT("verify") }) Modes.Add(MakeShared<FJsonValueString>(M));
            Out->SetArrayField(TEXT("supported_modes"), Modes);
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d provider(s) reported; visual backend %s."), Providers.Num(), bVisual ? TEXT("available") : TEXT("unavailable")), Out);
        });

    MCP_TOOL(Registry, "list_worlds")
        .Description(TEXT("List the worlds this editor process currently holds with stable ids (object paths), role (editor, pie, game, editor_preview...), map name, PIE instance, actor count and whether each is the current editor world. Use the editor world path for plan_actor_transform and the PIE world for play-time inspection; tools never cross worlds implicitly."))
        .ReadOnly().Idempotent()
        .EnumArg(TEXT("world_type"), TEXT("Filter: any (default), editor, pie, game, editor_preview"), { TEXT("any"), TEXT("editor"), TEXT("pie"), TEXT("game"), TEXT("editor_preview") })
        .OutputSchema(TEXT(R"({"type":"object","required":["worlds","pie_active"],"properties":{"worlds":{"type":"array"},"pie_active":{"type":"boolean"},"current_editor_world":{"type":"string"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&)
        {
            const FString Filter = Args->HasField(TEXT("world_type")) ? Args->GetStringField(TEXT("world_type")) : TEXT("any");
            auto Out = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Worlds;
            UWorld* Current = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (GEngine)
                for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
                {
                    UWorld* W = Ctx.World(); if (!W) continue;
                    const FString Role = WorldTypeName(Ctx.WorldType);
                    if (Filter != TEXT("any") && Role != Filter) continue;
                    auto J = MakeShared<FJsonObject>();
                    J->SetStringField(TEXT("world_id"), W->GetPathName()); J->SetStringField(TEXT("role"), Role);
                    J->SetStringField(TEXT("map_name"), W->GetMapName()); J->SetStringField(TEXT("package"), W->GetOutermost()->GetName());
                    J->SetNumberField(TEXT("pie_instance"), Ctx.PIEInstance); J->SetStringField(TEXT("context_handle"), Ctx.ContextHandle.ToString());
                    int32 Actors = 0; for (TActorIterator<AActor> It(W); It; ++It) ++Actors;
                    J->SetNumberField(TEXT("actor_count"), Actors); J->SetBoolField(TEXT("is_current_editor_world"), W == Current);
                    J->SetBoolField(TEXT("has_begun_play"), W->HasBegunPlay());
                    Worlds.Add(MakeShared<FJsonValueObject>(J));
                }
            Out->SetArrayField(TEXT("worlds"), Worlds); Out->SetBoolField(TEXT("pie_active"), GEditor && GEditor->IsPlaySessionInProgress());
            if (Current) Out->SetStringField(TEXT("current_editor_world"), Current->GetPathName());
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d world(s)."), Worlds.Num()), Out);
        });

    MCP_TOOL(Registry, "export_diagnostic_bundle")
        .Description(TEXT("Write a redacted diagnostic bundle for support: server health (diagnostic detail), capabilities, this session's operations, tool timing telemetry and a bounded tail of the editor log, all with API keys, auth tokens and Authorization headers redacted. Files land under Saved/MCPV5/Bundles/<bundle_id>/ (bundle.json, log_tail.txt); the manifest lists each file with size and SHA-1 and is readable as unreal://bundles/{bundle_id} by the owner. Bounded: log_tail_kb 0..2048 (default 128); at most 16 bundles are retained per editor session."))
        .ReadOnly().Idempotent()
        .IntArg(TEXT("log_tail_kb"), TEXT("Kilobytes of editor log tail to include, 0..2048 (default 128)"))
        .BoolArg(TEXT("include_operations"), TEXT("Include this session's operations (default true)"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Bundles require the game thread"));
            const int32 TailKb = Args->HasField(TEXT("log_tail_kb")) ? FMath::Clamp((int32)Args->GetNumberField(TEXT("log_tail_kb")), 0, 2048) : 128;
            const bool bOps = !Args->HasField(TEXT("include_operations")) || Args->GetBoolField(TEXT("include_operations"));
            if (Bundles.Num() >= MaxBundles)
            {
                FString Oldest; double T = TNumericLimits<double>::Max();
                for (const auto& P : Bundles) if (P.Value.CreatedAt < T) { T = P.Value.CreatedAt; Oldest = P.Key; }
                if (!Oldest.IsEmpty()) { IFileManager::Get().DeleteDirectory(*Bundles[Oldest].Dir, false, true); Bundles.Remove(Oldest); }
            }
            FBundle B; B.Id = TEXT("bundle-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(16);
            B.Principal = Context.PrincipalId; B.Session = Context.SessionId; B.CreatedAt = FPlatformTime::Seconds();
            B.Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MCPV5/Bundles") / B.Id);
            IFileManager::Get().MakeDirectory(*B.Dir, true);
            int32 Redactions = 0;
            auto Health = MakeShared<FJsonObject>(); Health->SetStringField(TEXT("detail"), TEXT("diagnostic"));
            const FMCPToolResult H = FMCPToolRegistry::Get().ExecuteTool(TEXT("get_server_health"), Health, Context);
            const FMCPToolResult C = FMCPToolRegistry::Get().ExecuteTool(TEXT("get_server_capabilities"), MakeShared<FJsonObject>(), Context);
            auto Bundle = MakeShared<FJsonObject>();
            Bundle->SetStringField(TEXT("bundle_id"), B.Id); Bundle->SetStringField(TEXT("created_at"), FDateTime::UtcNow().ToIso8601());
            Bundle->SetStringField(TEXT("principal"), Context.PrincipalId); Bundle->SetStringField(TEXT("session"), Context.SessionId);
            if (H.StructuredContent.IsValid()) Bundle->SetObjectField(TEXT("health"), H.StructuredContent);
            if (C.StructuredContent.IsValid()) Bundle->SetObjectField(TEXT("capabilities"), C.StructuredContent);
            Bundle->SetObjectField(TEXT("tool_timing"), ToolTimingJson(50, 50));
            if (bOps) { const FMCPToolResult O = FMCPToolRegistry::Get().ExecuteTool(TEXT("list_editor_operations"), MakeShared<FJsonObject>(), Context); if (O.StructuredContent.IsValid()) Bundle->SetObjectField(TEXT("operations"), O.StructuredContent); }
            FString LogNote; FString Tail = ReadLogTail(TailKb, LogNote);
            Tail = Redact(Tail, Redactions);
            FString BundleText = Redact(JsonToString(Bundle), Redactions);
            const FString BundleFile = B.Dir / TEXT("bundle.json"), LogFile = B.Dir / TEXT("log_tail.txt");
            FFileHelper::SaveStringToFile(BundleText, *BundleFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            if (TailKb > 0) FFileHelper::SaveStringToFile(Tail, *LogFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
            auto Files = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> FileArr; int64 Total = 0;
            for (const FString& F : { BundleFile, LogFile })
            {
                if (!IFileManager::Get().FileExists(*F)) continue;
                TArray<uint8> Data; FFileHelper::LoadFileToArray(Data, *F);
                auto FJ = MakeShared<FJsonObject>(); FJ->SetStringField(TEXT("name"), FPaths::GetCleanFilename(F)); FJ->SetStringField(TEXT("path"), F);
                FJ->SetNumberField(TEXT("bytes"), (double)Data.Num()); FJ->SetStringField(TEXT("sha1"), FSHA1::HashBuffer(Data.GetData(), Data.Num()).ToString().ToLower());
                Total += Data.Num(); FileArr.Add(MakeShared<FJsonValueObject>(FJ));
            }
            auto Manifest = MakeShared<FJsonObject>();
            Manifest->SetStringField(TEXT("bundle_id"), B.Id); Manifest->SetStringField(TEXT("uri"), TEXT("unreal://bundles/") + B.Id); Manifest->SetStringField(TEXT("directory"), B.Dir);
            Manifest->SetArrayField(TEXT("files"), FileArr); Manifest->SetNumberField(TEXT("total_bytes"), (double)Total); Manifest->SetNumberField(TEXT("redactions_applied"), Redactions);
            Manifest->SetNumberField(TEXT("log_tail_kb"), TailKb); if (!LogNote.IsEmpty()) Manifest->SetStringField(TEXT("log_source"), LogNote);
            Manifest->SetStringField(TEXT("created_at"), Bundle->GetStringField(TEXT("created_at")));
            B.ManifestJson = JsonToString(Manifest); B.Bytes = Total;
            Bundles.Add(B.Id, B);
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Diagnostic bundle %s written (%lld bytes, %d redaction(s))."), *B.Id, Total, Redactions), Manifest);
        });
}

void RecordToolCall(const FString& ToolName, double Milliseconds, bool bOk)
{
    FScopeLock Lock(&TimingLock);
    FCallRecord R; R.Tool = ToolName; R.Ms = Milliseconds; R.bOk = bOk; R.At = FDateTime::UtcNow().ToIso8601();
    if (Ring.Num() < RingCapacity) Ring.Add(R); else { Ring[RingNext] = R; }
    RingNext = (RingNext + 1) % RingCapacity; ++CallsTotal;
    FToolAgg& A = Aggregates.FindOrAdd(ToolName); ++A.Calls; if (!bOk) ++A.Failures; A.TotalMs += Milliseconds; A.MaxMs = FMath::Max(A.MaxMs, Milliseconds);
}

TSharedPtr<FJsonObject> ToolTimingJson(int32 RecentLimit, int32 PerToolLimit)
{
    FScopeLock Lock(&TimingLock);
    auto Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("calls_total"), (double)CallsTotal); Out->SetNumberField(TEXT("ring_capacity"), RingCapacity); Out->SetNumberField(TEXT("ring_size"), Ring.Num());
    Out->SetStringField(TEXT("units"), TEXT("milliseconds of game-thread dispatch including the handler; this is plugin overhead plus handler work, independent of scene rendering cost"));
    TArray<TSharedPtr<FJsonValue>> Recent;
    for (int32 i = 0; i < FMath::Min(RecentLimit, Ring.Num()); ++i)
    {
        const int32 Idx = Ring.Num() < RingCapacity ? (Ring.Num() - 1 - i) : ((RingNext - 1 - i + RingCapacity) % RingCapacity);
        const FCallRecord& R = Ring[Idx];
        auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("tool"), R.Tool); J->SetNumberField(TEXT("ms"), R.Ms); J->SetBoolField(TEXT("ok"), R.bOk); J->SetStringField(TEXT("at"), R.At);
        Recent.Add(MakeShared<FJsonValueObject>(J));
    }
    Out->SetArrayField(TEXT("recent"), Recent);
    TArray<TPair<FString, FToolAgg>> Sorted; for (const auto& P : Aggregates) Sorted.Add(P);
    Sorted.Sort([](const TPair<FString, FToolAgg>& L, const TPair<FString, FToolAgg>& R) { return L.Value.TotalMs > R.Value.TotalMs; });
    TArray<TSharedPtr<FJsonValue>> Per;
    for (int32 i = 0; i < FMath::Min(PerToolLimit, Sorted.Num()); ++i)
    {
        const FToolAgg& A = Sorted[i].Value; auto J = MakeShared<FJsonObject>();
        J->SetStringField(TEXT("tool"), Sorted[i].Key); J->SetNumberField(TEXT("calls"), (double)A.Calls); J->SetNumberField(TEXT("failures"), (double)A.Failures);
        J->SetNumberField(TEXT("total_ms"), A.TotalMs); J->SetNumberField(TEXT("avg_ms"), A.Calls ? A.TotalMs / A.Calls : 0.0); J->SetNumberField(TEXT("max_ms"), A.MaxMs);
        Per.Add(MakeShared<FJsonValueObject>(J));
    }
    Out->SetArrayField(TEXT("per_tool"), Per);
    return Out;
}

void RegisterResources(FMCPResourceProvider& Provider)
{
    FMCPResourceDefinition Def; Def.Uri = TEXT("unreal://bundles/{bundle_id}"); Def.Name = TEXT("Diagnostic bundle manifest");
    Def.Description = TEXT("Manifest of an owned, redacted diagnostic bundle: files with sizes and SHA-1, redaction count; owner only."); Def.MimeType = TEXT("application/json"); Def.bTemplate = true;
    FMCPResourceReaderCtx Reader; Reader.BindLambda([](const FString& Uri, const FMCPRequestContext& Context)
    {
        const FBundle* B = Bundles.Find(Uri.Mid(FString(TEXT("unreal://bundles/")).Len()));
        if (!B || B->Principal != Context.PrincipalId || B->Session != Context.SessionId) return FMCPResourceContent::NotFound(Uri, TEXT("Unknown or foreign bundle"));
        FMCPResourceContent C; C.Uri = Uri; C.MimeType = TEXT("application/json"); C.Text = B->ManifestJson; return C;
    });
    Provider.RegisterResourceCtx(Def, Reader);
}
void ClearSession(const FString& SessionId) { for (auto It = Bundles.CreateIterator(); It; ++It) if (It.Value().Session == SessionId) It.RemoveCurrent(); }
void Reset() { Bundles.Empty(); FScopeLock Lock(&TimingLock); Ring.Empty(); RingNext = 0; CallsTotal = 0; Aggregates.Empty(); }
}
