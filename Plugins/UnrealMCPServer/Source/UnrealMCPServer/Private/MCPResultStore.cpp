// Copyright StraySpark Studio 2026. All Rights Reserved.
#include "MCPResultStore.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "MCPProtocol.h"
#include "MCPRequestContext.h"
#include "Misc/ScopeLock.h"

namespace MCPResultStore
{
namespace
{
struct FStored { FString Id, Principal, Session, Tool, Text; double CreatedAt = 0.0, ExpiresAt = 0.0; };
TMap<FString, FStored> Results;
FCriticalSection Lock; // tools/call runs on the game thread, but the HTTP layer may store from its own thread
constexpr int32 MaxPerSession = 16;
constexpr int64 MaxTotalBytes = 64 * 1024 * 1024;
constexpr double TtlSeconds = 600.0;
int64 TotalBytes = 0;

void Sweep(double Now)
{
    for (auto It = Results.CreateIterator(); It; ++It) if (Now >= It.Value().ExpiresAt) { TotalBytes -= It.Value().Text.Len(); It.RemoveCurrent(); }
}
const FStored* FindOwned(const FString& Id, const FMCPRequestContext& Context)
{
    const FStored* S = Results.Find(Id);
    return S && S->Principal == Context.PrincipalId && S->Session == Context.SessionId ? S : nullptr;
}
} // namespace

FString Store(const FMCPRequestContext& Context, const FString& ToolName, FString&& FullText)
{
    FScopeLock L(&Lock);
    const double Now = FPlatformTime::Seconds();
    Sweep(Now);
    int32 Owned = 0; FString Oldest; double OldestAt = TNumericLimits<double>::Max();
    for (const auto& P : Results) if (P.Value.Principal == Context.PrincipalId && P.Value.Session == Context.SessionId) { ++Owned; if (P.Value.CreatedAt < OldestAt) { OldestAt = P.Value.CreatedAt; Oldest = P.Key; } }
    if (Owned >= MaxPerSession && !Oldest.IsEmpty()) { TotalBytes -= Results[Oldest].Text.Len(); Results.Remove(Oldest); }
    while (TotalBytes + FullText.Len() > MaxTotalBytes && Results.Num())
    {
        FString Victim; double At = TNumericLimits<double>::Max();
        for (const auto& P : Results) if (P.Value.CreatedAt < At) { At = P.Value.CreatedAt; Victim = P.Key; }
        TotalBytes -= Results[Victim].Text.Len(); Results.Remove(Victim);
    }
    if ((int64)FullText.Len() > MaxTotalBytes) return FString(); // cannot be retained at all; the caller reports that honestly
    FStored S; S.Id = TEXT("res-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(16);
    S.Principal = Context.PrincipalId; S.Session = Context.SessionId; S.Tool = ToolName; S.Text = MoveTemp(FullText); S.CreatedAt = Now; S.ExpiresAt = Now + TtlSeconds;
    TotalBytes += S.Text.Len();
    const FString Id = S.Id; Results.Add(Id, MoveTemp(S));
    return Id;
}

bool Page(const FString& ResultId, const FMCPRequestContext& Context, int64 Offset, int32 MaxBytes, FString& OutText, int64& OutTotal)
{
    FScopeLock L(&Lock);
    Sweep(FPlatformTime::Seconds());
    const FStored* S = FindOwned(ResultId, Context);
    if (!S) return false;
    OutTotal = S->Text.Len();
    if (Offset < 0 || Offset > OutTotal) { OutText.Empty(); return true; }
    OutText = S->Text.Mid((int32)Offset, MaxBytes);
    return true;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    MCP_TOOL(Registry, "get_result_page")
        .Description(TEXT("Read a page of a stored oversized tool result. When a tools/call result exceeds the response cap (MaxToolResultKB), the server keeps the full serialized result for 10 minutes and answers with result_truncated=true and a result_id; call this with increasing offset until complete=true (or read unreal://results/{result_id} in one piece). Owner only; at most 16 stored results per session and 64 MiB overall."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("result_id"), TEXT("Result identifier from the truncated response"), true)
        .IntArg(TEXT("offset"), TEXT("Character offset to start from (default 0)"))
        .IntArg(TEXT("max_bytes"), TEXT("Page size in characters, 1024..1048576 (default 262144)"))
        .OutputSchema(TEXT(R"({"type":"object","required":["result_id","offset","bytes","total_bytes","complete"],"properties":{"result_id":{"type":"string"},"tool":{"type":"string"},"offset":{"type":"integer"},"bytes":{"type":"integer"},"total_bytes":{"type":"integer"},"next_offset":{"type":"integer"},"complete":{"type":"boolean"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            const FString Id = Args->GetStringField(TEXT("result_id"));
            const int64 Offset = Args->HasField(TEXT("offset")) ? (int64)Args->GetNumberField(TEXT("offset")) : 0;
            const int32 Max = Args->HasField(TEXT("max_bytes")) ? FMath::Clamp((int32)Args->GetNumberField(TEXT("max_bytes")), 1024, 1024 * 1024) : 256 * 1024;
            FString Text; int64 Total = 0;
            if (!Page(Id, Context, Offset, Max, Text, Total)) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Unknown, expired or foreign result"), TEXT("Stored results live 10 minutes and belong to the session that produced them."));
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("result_id"), Id); Out->SetNumberField(TEXT("offset"), (double)Offset); Out->SetNumberField(TEXT("bytes"), Text.Len());
            Out->SetNumberField(TEXT("total_bytes"), (double)Total);
            const bool bComplete = Offset + Text.Len() >= Total;
            Out->SetBoolField(TEXT("complete"), bComplete);
            if (!bComplete) Out->SetNumberField(TEXT("next_offset"), (double)(Offset + Text.Len()));
            FMCPToolResult R = FMCPToolResult::SuccessStructured(Text, Out);
            return R;
        });
}

void RegisterResources(FMCPResourceProvider& Provider)
{
    FMCPResourceDefinition Def; Def.Uri = TEXT("unreal://results/{result_id}"); Def.Name = TEXT("Stored tool result");
    Def.Description = TEXT("Full serialized result of an oversized tools/call, retained 10 minutes for the owning session."); Def.MimeType = TEXT("application/json"); Def.bTemplate = true;
    FMCPResourceReaderCtx Reader; Reader.BindLambda([](const FString& Uri, const FMCPRequestContext& Context)
    {
        const FString Id = Uri.Mid(FString(TEXT("unreal://results/")).Len());
        FString Text; int64 Total = 0;
        if (!Page(Id, Context, 0, MAX_int32, Text, Total)) return FMCPResourceContent::NotFound(Uri, TEXT("Unknown, expired or foreign result"));
        FMCPResourceContent C; C.Uri = Uri; C.MimeType = TEXT("application/json"); C.Text = Text; return C;
    });
    Provider.RegisterResourceCtx(Def, Reader);
}

void ClearSession(const FString& SessionId)
{
    FScopeLock L(&Lock);
    for (auto It = Results.CreateIterator(); It; ++It) if (It.Value().Session == SessionId) { TotalBytes -= It.Value().Text.Len(); It.RemoveCurrent(); }
}
void Reset() { FScopeLock L(&Lock); Results.Empty(); TotalBytes = 0; }
TSharedPtr<FJsonObject> DiagnosticsJson()
{
    FScopeLock L(&Lock);
    auto Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("results_retained"), Results.Num()); Out->SetNumberField(TEXT("results_bytes"), (double)TotalBytes);
    Out->SetNumberField(TEXT("results_bytes_budget"), (double)MaxTotalBytes); Out->SetNumberField(TEXT("results_per_session_cap"), MaxPerSession);
    return Out;
}
}
