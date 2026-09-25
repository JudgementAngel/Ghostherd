// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 16: owned editor operations (V5-10 slice) and tick-driven PIE scenarios (V5-28 slice).
// Scenarios never block the game thread: every step advances on the core ticker with a deadline,
// cancellation is cooperative and observed at step boundaries, and PIE started by a scenario is
// stopped by it on success, failure, cancellation or timeout.

#include "MCPScenarios.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPTaskManager.h"
#include "MCPEditorSurfaces.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Common/MCPPropertyIO.h"
#include "Containers/Ticker.h"
#include "Misc/Base64.h"

namespace MCPScenarios
{
namespace
{
constexpr int32 MaxOpsPerSession = 4, MaxOpsGlobal = 32, MaxSteps = 32, MaxFrames = 4;
constexpr double MaxDeadline = 600.0, RetainSeconds = 600.0;

struct FStep
{
    FString Kind, Label, Class, Property, Expected, SurfaceTitle, Key;
    double Seconds = 0.0, Timeout = 10.0; int32 Min = -1, Max = -1; bool bOptional = false;
    // runtime
    FString State = TEXT("pending"), Detail; double StartedAt = 0.0; int32 Attempts = 0;
};
struct FFrameRecord { FString FrameId, Mime; int32 Width = 0, Height = 0; TArray64<uint8> Bytes; FString CapturedAt; int32 StepIndex = -1; };
struct FOperation
{
    FString Id, Kind = TEXT("editor_scenario"), Principal, Session, State = TEXT("queued"), Error, Name;
    FMCPRequestContext Context;
    TArray<FStep> Steps; int32 Current = 0;
    double CreatedAt = 0.0, StartedAt = 0.0, EndedAt = 0.0, Deadline = 60.0; FString CreatedUtc, EndedUtc;
    bool bScenarioStartedPie = false, bCancelRequested = false;
    TArray<FFrameRecord> Frames; TArray<TSharedPtr<FJsonValue>> Events;
    int32 Passed = 0, Failed = 0, Skipped = 0;
    // v5 increment 20: driver-based operations
    FMCPOperationDriver Driver; TFunction<void(const FString&)> OnAbort; bool bAborted = false;
    double Progress = 0.0; FString ProgressMessage; TSharedPtr<FJsonObject> Result, Details; double CancelRequestedAt = 0.0;
    uint64 PaintAtEnd = 0;
};
TMap<FString, FOperation> Operations;
FTSTicker::FDelegateHandle TickerHandle;

FMCPToolResult Error(EMCPError Code, const FString& Message, const FString& Hint = FString()) { return FMCPToolResult::ErrorStructured(Code, Message, Hint); }
double Now() { return FPlatformTime::Seconds(); }
FString Utc() { return FDateTime::UtcNow().ToIso8601(); }

void Event(FOperation& Op, const FString& Type, const FString& Message, int32 Step = -1)
{
    if (Op.Events.Num() >= 256) return;
    auto E = MakeShared<FJsonObject>(); E->SetStringField(TEXT("type"), Type); E->SetStringField(TEXT("message"), Message); E->SetStringField(TEXT("at"), Utc());
    if (Step >= 0) E->SetNumberField(TEXT("step"), Step);
    Op.Events.Add(MakeShared<FJsonValueObject>(E));
}
UWorld* PieWorld()
{
    if (!GEditor) return nullptr;
    if (FWorldContext* Ctx = GEditor->GetPIEWorldContext()) return Ctx->World();
    return nullptr;
}
bool PieActive() { return GEditor && GEditor->IsPlaySessionInProgress(); }

void Finish(FOperation& Op, const FString& State, const FString& Err)
{
    if (Op.State != TEXT("running") && Op.State != TEXT("cancel_requested") && Op.State != TEXT("queued")) return;
    Op.State = State; Op.Error = Err; Op.EndedAt = Now(); Op.EndedUtc = Utc();
    Op.PaintAtEnd = MCPEditorSurfaces::CurrentPaintSequence();
    if (Op.bScenarioStartedPie && PieActive()) { GEditor->RequestEndPlayMap(); Event(Op, TEXT("pie_stop_requested"), TEXT("Scenario-owned PIE stop requested")); Op.bScenarioStartedPie = false; }
    Event(Op, TEXT("finished"), FString::Printf(TEXT("%s%s"), *State, Err.IsEmpty() ? TEXT("") : *(TEXT(": ") + Err)));
}

/** Advance one step. Returns true if the step completed (pass, fail or skip) this tick. */
void Abort(FOperation& Op, const FString& Reason)
{
    if (Op.bAborted || !Op.OnAbort) return;
    Op.bAborted = true;
    Op.OnAbort(Reason);
}
bool RunStep(FOperation& Op, FStep& S, int32 Index, double NowSeconds)
{
    if (S.State == TEXT("pending")) { S.State = TEXT("running"); S.StartedAt = NowSeconds; Event(Op, TEXT("step_started"), S.Kind, Index); }
    ++S.Attempts;
    auto Pass = [&](const FString& D) { S.State = TEXT("passed"); S.Detail = D; ++Op.Passed; Event(Op, TEXT("step_passed"), D, Index); return true; };
    auto Fail = [&](const FString& D)
    {
        if (S.bOptional) { S.State = TEXT("skipped"); S.Detail = D; ++Op.Skipped; Event(Op, TEXT("step_skipped"), D, Index); return true; }
        S.State = TEXT("failed"); S.Detail = D; ++Op.Failed; Event(Op, TEXT("step_failed"), D, Index); return true;
    };
    auto TimedOut = [&]() { return NowSeconds - S.StartedAt > S.Timeout; };
    if (S.Kind == TEXT("start_pie"))
    {
        if (!GEditor) return Fail(TEXT("no editor"));
        if (S.Attempts == 1)
        {
            if (PieActive()) return Fail(TEXT("a play session is already active and is not owned by this scenario; it is never taken over"));
            FRequestPlaySessionParams Params; Params.WorldType = EPlaySessionWorldType::PlayInEditor; Params.SessionDestination = EPlaySessionDestinationType::InProcess;
            GEditor->RequestPlaySession(Params); Op.bScenarioStartedPie = true;
            Event(Op, TEXT("pie_requested"), TEXT("Play session requested; waiting for the editor to start it"), Index);
        }
        if (GEditor->IsPlayingSessionInEditor() && PieWorld()) return Pass(TEXT("PIE world ") + PieWorld()->GetPathName());
        return TimedOut() ? Fail(TEXT("PIE did not start before the step timeout")) : false;
    }
    if (S.Kind == TEXT("stop_pie"))
    {
        if (!PieActive()) { Op.bScenarioStartedPie = false; return Pass(TEXT("no play session active")); }
        if (S.Attempts == 1) { GEditor->RequestEndPlayMap(); Event(Op, TEXT("pie_stop_requested"), TEXT("Stop requested"), Index); }
        if (!GEditor->IsPlayingSessionInEditor()) { Op.bScenarioStartedPie = false; return Pass(TEXT("PIE stopped")); }
        return TimedOut() ? Fail(TEXT("PIE did not stop before the step timeout")) : false;
    }
    if (S.Kind == TEXT("wait_seconds")) return NowSeconds - S.StartedAt >= S.Seconds ? Pass(FString::Printf(TEXT("waited %.2fs"), NowSeconds - S.StartedAt)) : false;
    UWorld* World = PieWorld();
    if (S.Kind == TEXT("wait_for_actor") || S.Kind == TEXT("assert_actor_count") || S.Kind == TEXT("assert_property"))
    {
        if (!World) return S.Kind == TEXT("wait_for_actor") && !TimedOut() ? false : Fail(TEXT("no PIE world; start_pie first"));
        UClass* Class = S.Class.IsEmpty() ? AActor::StaticClass() : FindFirstObject<UClass>(*S.Class, EFindFirstObjectOptions::NativeFirst);
        if (!Class) return Fail(FString::Printf(TEXT("class '%s' not found"), *S.Class));
        TArray<AActor*> Matches;
        for (TActorIterator<AActor> It(World, Class); It; ++It) if (IsValid(*It) && (S.Label.IsEmpty() || It->GetActorLabel() == S.Label || It->GetName() == S.Label)) Matches.Add(*It);
        if (S.Kind == TEXT("wait_for_actor")) return Matches.Num() ? Pass(FString::Printf(TEXT("found %s"), *Matches[0]->GetPathName())) : (TimedOut() ? Fail(TEXT("actor did not appear before the step timeout")) : false);
        if (S.Kind == TEXT("assert_actor_count"))
        {
            const bool bOk = (S.Min < 0 || Matches.Num() >= S.Min) && (S.Max < 0 || Matches.Num() <= S.Max);
            return bOk ? Pass(FString::Printf(TEXT("%d actor(s)"), Matches.Num())) : Fail(FString::Printf(TEXT("%d actor(s) of %s, expected %d..%d"), Matches.Num(), *Class->GetName(), S.Min, S.Max));
        }
        if (Matches.IsEmpty()) return TimedOut() ? Fail(TEXT("actor not found for property assertion")) : false;
        FProperty* Prop = Matches[0]->GetClass()->FindPropertyByName(FName(*S.Property));
        if (!Prop) return Fail(FString::Printf(TEXT("property '%s' not found on %s"), *S.Property, *Matches[0]->GetClass()->GetName()));
        FString Value;
        if (const TSharedPtr<FJsonValue> V = MCPCommon::ExportPropertyToJson(Prop, Matches[0]))
        {
            if (V->Type == EJson::String || V->Type == EJson::Number || V->Type == EJson::Boolean) Value = V->AsString();
            else { TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Value); FJsonSerializer::Serialize(V, FString(), W); }
        }
        if (Value.TrimStartAndEnd().Equals(S.Expected.TrimStartAndEnd(), ESearchCase::IgnoreCase)) return Pass(FString::Printf(TEXT("%s == %s"), *S.Property, *Value));
        S.Detail = FString::Printf(TEXT("last observed '%s'"), *Value);
        return TimedOut() ? Fail(FString::Printf(TEXT("%s is '%s', expected '%s'"), *S.Property, *Value, *S.Expected)) : false;
    }
    if (S.Kind == TEXT("add_widget") || S.Kind == TEXT("send_input"))
    {
        if (!World) return Fail(TEXT("no PIE world; start_pie first"));
        auto A = MakeShared<FJsonObject>();
        if (S.Kind == TEXT("add_widget")) A->SetStringField(TEXT("widget_class_path"), S.Class); else A->SetStringField(TEXT("key"), S.Key);
        const FMCPToolResult R = FMCPToolRegistry::Get().ExecuteTool(S.Kind == TEXT("add_widget") ? TEXT("pie_add_widget_to_viewport") : TEXT("pie_send_input"), A, Op.Context);
        return R.bIsError ? Fail(R.Content.Num() ? R.Content[0].Text : TEXT("tool failed")) : Pass(R.Content.Num() ? R.Content[0].Text.Left(200) : TEXT("ok"));
    }
    if (S.Kind == TEXT("capture"))
    {
        auto L = MakeShared<FJsonObject>(); L->SetStringField(TEXT("kind"), TEXT("window")); L->SetStringField(TEXT("title_contains"), S.SurfaceTitle.IsEmpty() ? TEXT("Preview [NetMode") : S.SurfaceTitle);
        const FMCPToolResult List = FMCPToolRegistry::Get().ExecuteTool(TEXT("list_editor_surfaces"), L, Op.Context);
        if (List.bIsError || !List.StructuredContent.IsValid()) return Fail(TEXT("surface discovery failed"));
        const auto Surfaces = List.StructuredContent->GetArrayField(TEXT("surfaces"));
        if (Surfaces.IsEmpty()) return TimedOut() ? Fail(TEXT("no matching surface (PIE window or given title) appeared")) : false;
        auto C = MakeShared<FJsonObject>(); C->SetStringField(TEXT("surface_id"), Surfaces[0]->AsObject()->GetStringField(TEXT("surface_id"))); C->SetNumberField(TEXT("max_long_edge"), 1280);
        const FMCPToolResult Cap = FMCPToolRegistry::Get().ExecuteTool(TEXT("capture_editor_surface"), C, Op.Context);
        if (Cap.bIsError) return Fail(Cap.Content.Num() ? Cap.Content[0].Text : TEXT("capture failed"));
        if (Op.Frames.Num() >= MaxFrames) Op.Frames.RemoveAt(0);
        FFrameRecord F; F.StepIndex = Index; F.CapturedAt = Utc();
        for (const FMCPContentBlock& B : Cap.Content) if (B.Type == TEXT("image")) { F.Mime = B.MimeType; TArray<uint8> Bytes; FBase64::Decode(B.ImageData, Bytes); F.Bytes.Append(Bytes.GetData(), Bytes.Num()); }
        if (Cap.StructuredContent.IsValid()) { F.FrameId = Cap.StructuredContent->GetStringField(TEXT("frame_id")); F.Width = (int32)Cap.StructuredContent->GetNumberField(TEXT("width")); F.Height = (int32)Cap.StructuredContent->GetNumberField(TEXT("height")); }
        Op.Frames.Add(MoveTemp(F));
        return Pass(FString::Printf(TEXT("captured %s"), *Op.Frames.Last().FrameId));
    }
    return Fail(TEXT("unknown step kind"));
}

// v5 increment 24 (V5-14): continuation scheduling with a per-tick budget. Each operation advances by at
// most one step or driver call per tick; once the budget is spent the remaining operations wait for the
// next tick, and the starting point rotates so no operation starves. Indivisible engine calls inside a
// step are reported through last_tick_ms/max_tick_ms rather than hidden.
constexpr double TickBudgetMs = 5.0;
uint32 RoundRobin = 0; double LastTickMs = 0.0, MaxTickMs = 0.0; int64 TicksOverBudget = 0, OperationsDeferred = 0, TicksTotal = 0;
void Tick(double NowSeconds)
{
    const double TickStart = FPlatformTime::Seconds(); bool bOver = false; ++TicksTotal;
    TArray<FString> Keys; Operations.GetKeys(Keys);
    const int32 N = Keys.Num();
    for (int32 K = 0; K < N; ++K)
    {
        FOperation& Op = Operations[Keys[(K + RoundRobin) % FMath::Max(1, N)]];
        if (!bOver && (FPlatformTime::Seconds() - TickStart) * 1000.0 > TickBudgetMs) bOver = true;
        if (bOver) { if (Op.EndedAt == 0.0) ++OperationsDeferred; continue; }
        if (Op.State == TEXT("queued")) { Op.State = TEXT("running"); Op.StartedAt = NowSeconds; Event(Op, TEXT("started"), Op.Driver ? TEXT("Operation running") : TEXT("Scenario running")); }
        if (Op.State != TEXT("running") && Op.State != TEXT("cancel_requested")) continue;
        if (Op.Driver)
        {
            if (NowSeconds - Op.StartedAt > Op.Deadline) { Abort(Op, TEXT("deadline")); Finish(Op, TEXT("failed"), TEXT("Deadline exceeded")); continue; }
            const bool bCancel = Op.bCancelRequested || Op.Context.IsCancelled();
            if (bCancel && Op.CancelRequestedAt == 0.0) Op.CancelRequestedAt = NowSeconds;
            FMCPOperationTick T; T.OperationId = Op.Id; T.NowSeconds = NowSeconds; T.ElapsedSeconds = NowSeconds - Op.StartedAt; T.bCancelRequested = bCancel;
            FOperation* OpPtr = &Op;
            T.Event = [OpPtr](const FString& Type, const FString& Msg) { Event(*OpPtr, Type, Msg); };
            T.Progress = [OpPtr](double P, const FString& Msg) { OpPtr->Progress = FMath::Clamp(P, 0.0, 1.0); OpPtr->ProgressMessage = Msg; };
            T.Finish = [OpPtr](const FString& State, const FString& Err, TSharedPtr<FJsonObject> Result) { OpPtr->Result = Result; OpPtr->bAborted = true; Finish(*OpPtr, State, Err); };
            Op.Driver(T);
            // A driver sees the cancel flag exactly once; if it did not finish itself in that tick the store
            // cancels it (drivers that ignore cancellation, such as measurements, stop here; drivers with
            // provider-side work already finished themselves above).
            if (bCancel && Op.EndedAt == 0.0) { Abort(Op, TEXT("cancel")); Finish(Op, TEXT("cancelled"), TEXT("Cancelled by owner")); }
            continue;
        }
        if (Op.bCancelRequested || Op.Context.IsCancelled()) { Finish(Op, TEXT("cancelled"), TEXT("Cancelled at a step boundary")); continue; }
        if (NowSeconds - Op.StartedAt > Op.Deadline) { Finish(Op, TEXT("failed"), TEXT("Scenario deadline exceeded")); continue; }
        if (Op.Current >= Op.Steps.Num()) { Finish(Op, Op.Failed ? TEXT("failed") : TEXT("succeeded"), Op.Failed ? FString::Printf(TEXT("%d step(s) failed"), Op.Failed) : FString()); continue; }
        FStep& S = Op.Steps[Op.Current];
        if (RunStep(Op, S, Op.Current, NowSeconds))
        {
            if (S.State == TEXT("failed")) { Finish(Op, TEXT("failed"), FString::Printf(TEXT("step %d (%s): %s"), Op.Current, *S.Kind, *S.Detail)); continue; }
            ++Op.Current;
            if (Op.Current >= Op.Steps.Num()) Finish(Op, TEXT("succeeded"), FString());
        }
    }
    for (auto It = Operations.CreateIterator(); It; ++It)
        if (It.Value().EndedAt > 0.0 && NowSeconds - It.Value().EndedAt > RetainSeconds) It.RemoveCurrent();
    if (Operations.IsEmpty() && TickerHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle); TickerHandle.Reset(); }
    RoundRobin = N ? (RoundRobin + 1) % N : 0;
    LastTickMs = (FPlatformTime::Seconds() - TickStart) * 1000.0; MaxTickMs = FMath::Max(MaxTickMs, LastTickMs);
    if (bOver) ++TicksOverBudget;
}
void EnsureTicker()
{
    if (!TickerHandle.IsValid()) TickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("MCPScenarios"), 0.0f, [](float) { Tick(Now()); return true; });
}
/** Legacy long-running tool tasks presented in the operation shape (V5-13 compatibility wrapper). */
TSharedPtr<FJsonObject> TaskAsOperation(const FMCPTaskManager::FTaskSnapshot& T)
{
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("operation_id"), T.TaskId); O->SetStringField(TEXT("kind"), TEXT("legacy_task")); O->SetStringField(TEXT("name"), T.ToolName);
    const TCHAR* State = TEXT("running");
    switch (T.State) { case FMCPTaskManager::ETaskState::Completed: State = TEXT("succeeded"); break; case FMCPTaskManager::ETaskState::Failed: State = TEXT("failed"); break; case FMCPTaskManager::ETaskState::Cancelled: State = TEXT("cancelled"); break; default: break; }
    O->SetStringField(TEXT("state"), State);
    O->SetNumberField(TEXT("progress"), T.Progress < 0 ? 0.0 : T.Progress); if (!T.ProgressMessage.IsEmpty()) O->SetStringField(TEXT("progress_message"), T.ProgressMessage);
    O->SetNumberField(TEXT("duration_seconds"), (T.FinishedAt > 0 ? T.FinishedAt : Now()) - T.CreatedAt);
    O->SetNumberField(TEXT("current_step"), 0); O->SetNumberField(TEXT("step_count"), 0); O->SetArrayField(TEXT("steps"), {}); O->SetArrayField(TEXT("frames"), {});
    O->SetNumberField(TEXT("passed"), 0); O->SetNumberField(TEXT("failed"), 0); O->SetNumberField(TEXT("skipped"), 0);
    O->SetStringField(TEXT("created_at"), FDateTime::UtcNow().ToIso8601()); // legacy tasks keep monotonic times only
    O->SetStringField(TEXT("legacy_tools"), TEXT("get_task_status / cancel_task / list_tasks return the same underlying record"));
    return O;
}
FOperation* FindOwned(const FString& Id, const FMCPRequestContext& Context)
{
    FOperation* Op = Operations.Find(Id);
    return Op && Op->Principal == Context.PrincipalId && Op->Session == Context.SessionId ? Op : nullptr;
}
TSharedPtr<FJsonObject> OpJson(const FOperation& Op, bool bEvents)
{
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("operation_id"), Op.Id); O->SetStringField(TEXT("kind"), Op.Kind); O->SetStringField(TEXT("name"), Op.Name); O->SetStringField(TEXT("state"), Op.State);
    if (!Op.Error.IsEmpty()) O->SetStringField(TEXT("error"), Op.Error);
    O->SetStringField(TEXT("created_at"), Op.CreatedUtc); if (!Op.EndedUtc.IsEmpty()) O->SetStringField(TEXT("ended_at"), Op.EndedUtc);
    O->SetNumberField(TEXT("duration_seconds"), (Op.EndedAt > 0 ? Op.EndedAt : Now()) - (Op.StartedAt > 0 ? Op.StartedAt : Op.CreatedAt));
    O->SetNumberField(TEXT("deadline_seconds"), Op.Deadline);
    O->SetNumberField(TEXT("progress"), Op.Driver ? Op.Progress : (Op.Steps.Num() ? (double)Op.Current / Op.Steps.Num() : 1.0));
    if (!Op.ProgressMessage.IsEmpty()) O->SetStringField(TEXT("progress_message"), Op.ProgressMessage);
    if (Op.Result.IsValid()) O->SetObjectField(TEXT("result"), Op.Result);
    if (Op.Details.IsValid()) O->SetObjectField(TEXT("details"), Op.Details);
    O->SetNumberField(TEXT("current_step"), Op.Current); O->SetNumberField(TEXT("step_count"), Op.Steps.Num());
    O->SetNumberField(TEXT("passed"), Op.Passed); O->SetNumberField(TEXT("failed"), Op.Failed); O->SetNumberField(TEXT("skipped"), Op.Skipped);
    O->SetBoolField(TEXT("pie_owned"), Op.bScenarioStartedPie);
    TArray<TSharedPtr<FJsonValue>> Steps;
    for (int32 I = 0; I < Op.Steps.Num(); ++I)
    {
        const FStep& S = Op.Steps[I]; auto J = MakeShared<FJsonObject>();
        J->SetNumberField(TEXT("index"), I); J->SetStringField(TEXT("kind"), S.Kind); J->SetStringField(TEXT("state"), S.State); J->SetBoolField(TEXT("optional"), S.bOptional);
        if (!S.Detail.IsEmpty()) J->SetStringField(TEXT("detail"), S.Detail);
        Steps.Add(MakeShared<FJsonValueObject>(J));
    }
    O->SetArrayField(TEXT("steps"), Steps);
    TArray<TSharedPtr<FJsonValue>> Frames;
    for (const FFrameRecord& F : Op.Frames) { auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("frame_id"), F.FrameId); J->SetStringField(TEXT("mime"), F.Mime); J->SetNumberField(TEXT("width"), F.Width); J->SetNumberField(TEXT("height"), F.Height); J->SetNumberField(TEXT("bytes"), (double)F.Bytes.Num()); J->SetNumberField(TEXT("step"), F.StepIndex); J->SetStringField(TEXT("captured_at"), F.CapturedAt); Frames.Add(MakeShared<FJsonValueObject>(J)); }
    O->SetArrayField(TEXT("frames"), Frames);
    if (bEvents) O->SetArrayField(TEXT("events"), Op.Events);
    return O;
}
FString ParseSteps(const TArray<TSharedPtr<FJsonValue>>& Raw, TArray<FStep>& Out)
{
    if (Raw.Num() < 1 || Raw.Num() > MaxSteps) return FString::Printf(TEXT("steps must contain 1..%d entries"), MaxSteps);
    static const TSet<FString> Kinds = { TEXT("start_pie"), TEXT("stop_pie"), TEXT("wait_seconds"), TEXT("wait_for_actor"), TEXT("assert_actor_count"), TEXT("assert_property"), TEXT("add_widget"), TEXT("send_input"), TEXT("capture") };
    for (int32 I = 0; I < Raw.Num(); ++I)
    {
        const TSharedPtr<FJsonObject> O = Raw[I].IsValid() ? Raw[I]->AsObject() : nullptr;
        if (!O.IsValid()) return FString::Printf(TEXT("steps[%d] is not an object"), I);
        FStep S; O->TryGetStringField(TEXT("step"), S.Kind);
        if (!Kinds.Contains(S.Kind)) return FString::Printf(TEXT("steps[%d].step is not a supported step"), I);
        O->TryGetStringField(TEXT("label"), S.Label); O->TryGetStringField(TEXT("class"), S.Class); O->TryGetStringField(TEXT("property"), S.Property); O->TryGetStringField(TEXT("expected"), S.Expected);
        O->TryGetStringField(TEXT("surface_title"), S.SurfaceTitle); O->TryGetStringField(TEXT("key"), S.Key);
        if (O->HasTypedField<EJson::Number>(TEXT("seconds"))) S.Seconds = O->GetNumberField(TEXT("seconds"));
        if (O->HasTypedField<EJson::Number>(TEXT("timeout"))) S.Timeout = O->GetNumberField(TEXT("timeout"));
        if (O->HasTypedField<EJson::Number>(TEXT("min"))) S.Min = (int32)O->GetNumberField(TEXT("min"));
        if (O->HasTypedField<EJson::Number>(TEXT("max"))) S.Max = (int32)O->GetNumberField(TEXT("max"));
        if (O->HasTypedField<EJson::Boolean>(TEXT("optional"))) S.bOptional = O->GetBoolField(TEXT("optional"));
        if (S.Kind == TEXT("capture") && !O->HasField(TEXT("optional"))) S.bOptional = true; // headless editors have no pixels
        if (S.Seconds < 0 || S.Seconds > MaxDeadline || S.Timeout <= 0 || S.Timeout > MaxDeadline) return FString::Printf(TEXT("steps[%d]: seconds/timeout out of range"), I);
        if ((S.Kind == TEXT("assert_property")) && (S.Property.IsEmpty())) return FString::Printf(TEXT("steps[%d]: property is required"), I);
        if (S.Kind == TEXT("add_widget") && S.Class.IsEmpty()) return FString::Printf(TEXT("steps[%d]: class (widget asset path) is required"), I);
        if (S.Kind == TEXT("send_input") && S.Key.IsEmpty()) return FString::Printf(TEXT("steps[%d]: key is required"), I);
        Out.Add(S);
    }
    return FString();
}
const TCHAR* OpSchema = TEXT(R"({"type":"object","required":["operation_id","kind","state","progress","current_step","step_count","steps","passed","failed","skipped"],"properties":{"operation_id":{"type":"string"},"kind":{"type":"string"},"name":{"type":"string"},"state":{"type":"string"},"error":{"type":"string"},"created_at":{"type":"string"},"ended_at":{"type":"string"},"duration_seconds":{"type":"number"},"deadline_seconds":{"type":"number"},"progress":{"type":"number"},"current_step":{"type":"integer"},"step_count":{"type":"integer"},"passed":{"type":"integer"},"failed":{"type":"integer"},"skipped":{"type":"integer"},"pie_owned":{"type":"boolean"},"steps":{"type":"array"},"frames":{"type":"array"},"events":{"type":"array"}}})");
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    auto StepSchema = StringToJson(TEXT(R"({"type":"object","required":["step"],"properties":{"step":{"type":"string","enum":["start_pie","stop_pie","wait_seconds","wait_for_actor","assert_actor_count","assert_property","add_widget","send_input","capture"]},"label":{"type":"string"},"class":{"type":"string"},"property":{"type":"string"},"expected":{"type":"string"},"surface_title":{"type":"string"},"key":{"type":"string"},"seconds":{"type":"number","minimum":0,"maximum":600},"timeout":{"type":"number","exclusiveMinimum":0,"maximum":600},"min":{"type":"integer","minimum":0},"max":{"type":"integer","minimum":0},"optional":{"type":"boolean"}}})"));
    auto StepsSchema = MakeShared<FJsonObject>(); StepsSchema->SetStringField(TEXT("type"), TEXT("array")); StepsSchema->SetObjectField(TEXT("items"), StepSchema); StepsSchema->SetNumberField(TEXT("minItems"), 1); StepsSchema->SetNumberField(TEXT("maxItems"), MaxSteps);

    MCP_TOOL(Registry, "run_editor_scenario")
        .Description(TEXT("Start an owned, tick-driven editor scenario and return an operation handle immediately. Steps (1..32, in order): start_pie, stop_pie, wait_seconds{seconds}, wait_for_actor{class?,label?,timeout}, assert_actor_count{class?,label?,min?,max?}, assert_property{class?,label?,property,expected,timeout}, add_widget{class = widget asset path}, send_input{key}, capture{surface_title?} (optional by default: headless editors have no pixels). Waits use per-step timeouts and a scenario deadline, never fixed sleeps on the game thread. A pre-existing play session is never taken over; PIE started by the scenario is stopped when it ends for any reason. Poll with get_editor_operation; cancel with cancel_editor_operation. Requires Scene scope (steps may add widgets and send input)."))
        .RequiresPieOff()
        .StringArg(TEXT("name"), TEXT("Short scenario name for reports"))
        .NumberArg(TEXT("deadline_seconds"), TEXT("Whole-scenario deadline 1..600 (default 60)"))
        .ObjectArg(TEXT("steps"), TEXT("Ordered typed steps"), StepsSchema, true)
        .OutputSchema(OpSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Scenarios require the game thread"));
            Tick(Now());
            int32 Owned = 0, Active = 0;
            for (const auto& P : Operations) if (P.Value.EndedAt == 0.0) { ++Active; if (P.Value.Principal == Context.PrincipalId && P.Value.Session == Context.SessionId) ++Owned; }
            if (Owned >= MaxOpsPerSession) return Error(EMCPError::Unsupported, TEXT("This session already owns the maximum of 4 running operations"));
            if (Active >= MaxOpsGlobal) return Error(EMCPError::Unsupported, TEXT("Editor-wide operation limit reached"));
            FOperation Op;
            const FString ParseError = ParseSteps(Args->GetArrayField(TEXT("steps")), Op.Steps);
            if (!ParseError.IsEmpty()) return Error(EMCPError::OutOfRange, ParseError);
            Op.Deadline = Args->HasField(TEXT("deadline_seconds")) ? Args->GetNumberField(TEXT("deadline_seconds")) : 60.0;
            if (Op.Deadline < 1 || Op.Deadline > MaxDeadline) return Error(EMCPError::OutOfRange, TEXT("deadline_seconds must be 1..600"));
            Op.Id = TEXT("op-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
            Op.Name = Args->HasField(TEXT("name")) ? Args->GetStringField(TEXT("name")).Left(80) : TEXT("scenario");
            Op.Principal = Context.PrincipalId; Op.Session = Context.SessionId; Op.Context = Context; Op.Context.bAllowAsyncTask = false;
            Op.CreatedAt = Now(); Op.CreatedUtc = Utc();
            Event(Op, TEXT("queued"), FString::Printf(TEXT("%d step(s), deadline %.0fs"), Op.Steps.Num(), Op.Deadline));
            const FString Id = Op.Id; Operations.Add(Id, MoveTemp(Op));
            EnsureTicker();
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Scenario queued as %s; poll get_editor_operation."), *Id), OpJson(Operations[Id], false));
        });

    MCP_TOOL(Registry, "get_editor_operation")
        .Description(TEXT("Poll an owned operation: state (queued, running, cancel_requested, succeeded, failed, cancelled), progress, per-step states, frames captured, timestamps and optionally the event log. With include_frame=true the newest captured frame is returned inline. Finished operations remain readable for 10 minutes."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("operation_id"), TEXT("Operation identifier"), true)
        .BoolArg(TEXT("include_events"), TEXT("Include the bounded event log (default false)"))
        .BoolArg(TEXT("include_frame"), TEXT("Return the newest captured frame inline (default false)"))
        .OutputSchema(OpSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Operations require the game thread"));
            Tick(Now());
            const FOperation* Op = FindOwned(Args->GetStringField(TEXT("operation_id")), Context);
            if (!Op)
            {
                FMCPTaskManager::FTaskSnapshot T;
                if (FMCPTaskManager::Get().GetTask(Args->GetStringField(TEXT("operation_id")), T, Context))
                    return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Legacy task %s: %s."), *T.ToolName, *FMCPTaskManager::TaskStateToString(T.State)), TaskAsOperation(T));
                return Error(EMCPError::NotFound, TEXT("Unknown, expired or foreign operation"));
            }
            const auto Out = OpJson(*Op, Args->HasField(TEXT("include_events")) && Args->GetBoolField(TEXT("include_events")));
            if (Args->HasField(TEXT("include_frame")) && Args->GetBoolField(TEXT("include_frame")) && Op->Frames.Num())
            {
                const FFrameRecord& F = Op->Frames.Last();
                FMCPToolResult R = FMCPToolResult::WithImage(FString::Printf(TEXT("Operation %s; frame %s."), *Op->State, *F.FrameId), FBase64::Encode(F.Bytes.GetData(), (uint32)F.Bytes.Num()), F.Mime);
                R.StructuredContent = Out; return R;
            }
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Operation %s: step %d/%d, %d passed, %d failed, %d skipped."), *Op->State, Op->Current, Op->Steps.Num(), Op->Passed, Op->Failed, Op->Skipped), Out);
        });

    MCP_TOOL(Registry, "cancel_editor_operation")
        .Description(TEXT("Request cooperative cancellation of an owned operation. Queued work never starts; running work stops at its next step boundary and scenario-owned PIE is stopped. Already finished operations keep their terminal state (a committed success never becomes a rollback)."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("operation_id"), TEXT("Operation identifier"), true)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Operations require the game thread"));
            FOperation* Op = FindOwned(Args->GetStringField(TEXT("operation_id")), Context);
            if (!Op)
            {
                const FString TaskId = Args->GetStringField(TEXT("operation_id"));
                FMCPTaskManager::FTaskSnapshot T;
                if (!FMCPTaskManager::Get().GetTask(TaskId, T, Context)) return Error(EMCPError::NotFound, TEXT("Unknown, expired or foreign operation"));
                const bool bRequested = FMCPTaskManager::Get().CancelTask(TaskId, Context);
                FMCPTaskManager::Get().GetTask(TaskId, T, Context);
                return FMCPToolResult::SuccessStructured(bRequested ? TEXT("Cancellation requested for legacy task.") : FString::Printf(TEXT("Legacy task already %s; cancellation has no effect."), *FMCPTaskManager::TaskStateToString(T.State)), TaskAsOperation(T));
            }
            if (Op->EndedAt > 0.0) return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Operation already %s; cancellation has no effect."), *Op->State), OpJson(*Op, false));
            Op->bCancelRequested = true; if (Op->State == TEXT("running")) Op->State = TEXT("cancel_requested");
            Event(*Op, TEXT("cancel_requested"), TEXT("Cancellation requested by owner"));
            Tick(Now());
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Cancellation %s."), Op->State == TEXT("cancelled") ? TEXT("completed") : TEXT("requested")), OpJson(*Op, false));
        });

    MCP_TOOL(Registry, "list_editor_operations")
        .Description(TEXT("List this session's operations (running and recently finished), newest first, including legacy long-running tool tasks presented as kind=legacy_task."))
        .ReadOnly().Idempotent()
        .HandleCtx([](const TSharedPtr<FJsonObject>&, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Operations require the game thread"));
            Tick(Now());
            TArray<TSharedPtr<FJsonValue>> Items;
            for (const auto& P : Operations) if (P.Value.Principal == Context.PrincipalId && P.Value.Session == Context.SessionId) Items.Add(MakeShared<FJsonValueObject>(OpJson(P.Value, false)));
            for (const FMCPTaskManager::FTaskSnapshot& T : FMCPTaskManager::Get().ListTasks(Context)) Items.Add(MakeShared<FJsonValueObject>(TaskAsOperation(T)));
            Items.Sort([](const TSharedPtr<FJsonValue>& L, const TSharedPtr<FJsonValue>& R) { return L->AsObject()->GetStringField(TEXT("created_at")) > R->AsObject()->GetStringField(TEXT("created_at")); });
            auto Out = MakeShared<FJsonObject>(); Out->SetArrayField(TEXT("operations"), Items);
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d owned operation(s)."), Items.Num()), Out);
        });
}

void TickOperations(double NowSeconds) { check(IsInGameThread()); Tick(NowSeconds); }
void ClearSession(const FString& SessionId)
{
    check(IsInGameThread());
    for (auto It = Operations.CreateIterator(); It; ++It)
        if (It.Value().Session == SessionId) { if (It.Value().EndedAt == 0.0) { Abort(It.Value(), TEXT("session_ended")); Finish(It.Value(), TEXT("cancelled"), TEXT("Session ended")); } It.RemoveCurrent(); }
}
void Reset()
{
    check(IsInGameThread());
    for (auto& P : Operations) if (P.Value.EndedAt == 0.0) { Abort(P.Value, TEXT("server_shutdown")); Finish(P.Value, TEXT("cancelled"), TEXT("Server shutdown")); }
    Operations.Empty();
    if (TickerHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle); TickerHandle.Reset(); }
    RoundRobin = 0; LastTickMs = MaxTickMs = 0.0; TicksOverBudget = OperationsDeferred = TicksTotal = 0;
}
TSharedPtr<FJsonObject> DiagnosticsJson()
{
    auto Out = MakeShared<FJsonObject>(); int32 Running = 0; for (const auto& P : Operations) if (P.Value.EndedAt == 0.0) ++Running;
    Out->SetNumberField(TEXT("operations_retained"), Operations.Num()); Out->SetNumberField(TEXT("operations_running"), Running); Out->SetBoolField(TEXT("ticker_registered"), TickerHandle.IsValid());
    auto Sched = MakeShared<FJsonObject>();
    Sched->SetNumberField(TEXT("tick_budget_ms"), TickBudgetMs); Sched->SetNumberField(TEXT("last_tick_ms"), LastTickMs); Sched->SetNumberField(TEXT("max_tick_ms"), MaxTickMs);
    Sched->SetNumberField(TEXT("ticks_total"), (double)TicksTotal); Sched->SetNumberField(TEXT("ticks_over_budget"), (double)TicksOverBudget); Sched->SetNumberField(TEXT("operations_deferred"), (double)OperationsDeferred);
    Sched->SetStringField(TEXT("note"), TEXT("One step or driver call per operation per tick; when the budget is spent the rest wait for the next tick (round-robin start). max_tick_ms includes indivisible engine calls inside a step."));
    Out->SetObjectField(TEXT("scheduler"), Sched);
    return Out;
}

FString StartDrivenOperation(const FMCPRequestContext& Context, const FString& Kind, const FString& Name, double DeadlineSeconds,
    FMCPOperationDriver Driver, TFunction<void(const FString&)> OnAbort, TSharedPtr<FJsonObject> Details, FString& OutError)
{
    check(IsInGameThread());
    Tick(Now());
    int32 Owned = 0, Active = 0;
    for (const auto& P : Operations) if (P.Value.EndedAt == 0.0) { ++Active; if (P.Value.Principal == Context.PrincipalId && P.Value.Session == Context.SessionId) ++Owned; }
    if (Owned >= MaxOpsPerSession) { OutError = TEXT("This session already owns the maximum of 4 running operations"); return FString(); }
    if (Active >= MaxOpsGlobal) { OutError = TEXT("Editor-wide operation limit reached"); return FString(); }
    if (DeadlineSeconds < 1 || DeadlineSeconds > MaxDeadline) { OutError = TEXT("deadline_seconds must be 1..600"); return FString(); }
    FOperation Op;
    Op.Id = TEXT("op-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
    Op.Kind = Kind; Op.Name = Name.Left(80); Op.Deadline = DeadlineSeconds;
    Op.Principal = Context.PrincipalId; Op.Session = Context.SessionId; Op.Context = Context; Op.Context.bAllowAsyncTask = false;
    Op.CreatedAt = Now(); Op.CreatedUtc = Utc(); Op.Driver = MoveTemp(Driver); Op.OnAbort = MoveTemp(OnAbort); Op.Details = Details;
    Event(Op, TEXT("queued"), FString::Printf(TEXT("%s, deadline %.0fs"), *Kind, Op.Deadline));
    const FString Id = Op.Id; Operations.Add(Id, MoveTemp(Op));
    EnsureTicker();
    return Id;
}
TSharedPtr<FJsonObject> DescribeOperation(const FString& Id, const FMCPRequestContext& Context, bool bEvents)
{
    check(IsInGameThread());
    const FOperation* Op = Operations.Find(Id);
    if (!Op || Op->Principal != Context.PrincipalId || Op->Session != Context.SessionId) return nullptr;
    return OpJson(*Op, bEvents);
}
bool GetOperationMark(const FString& Id, const FMCPRequestContext& Context, FString& OutState, double& OutEndedAt, uint64& OutPaintAtEnd)
{
    check(IsInGameThread());
    const FOperation* Op = Operations.Find(Id);
    if (!Op || Op->Principal != Context.PrincipalId || Op->Session != Context.SessionId) return false;
    OutState = Op->State; OutEndedAt = Op->EndedAt; OutPaintAtEnd = Op->PaintAtEnd;
    return true;
}
}
