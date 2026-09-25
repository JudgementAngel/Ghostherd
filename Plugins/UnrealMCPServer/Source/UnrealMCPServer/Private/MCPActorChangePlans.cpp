#include "MCPActorChangePlans.h"
#include "MCPGeneralPlans.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"

namespace MCPActorChangePlans
{
namespace
{
constexpr double TransformTolerance = 1.e-6;
struct FPlan
{
    FString Id, Hash, Principal, Session, ActorPath, WorldPath;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AStaticMeshActor> Actor;
    TWeakObjectPtr<UStaticMeshComponent> Root;
    TWeakObjectPtr<UStaticMesh> Mesh;
    FTransform Before, After;
    double ExpiresAt = 0;
    FString State = TEXT("pending"), AppliedKey;
    FMCPToolResult Result;
};
TMap<FString, FPlan> Plans;
bool Applying = false;

void Sweep()
{
    const double Now = FPlatformTime::Seconds();
    for (auto It = Plans.CreateIterator(); It; ++It)
        if (Now >= It.Value().ExpiresAt) It.RemoveCurrent();
}
FMCPToolResult Error(const FString& Message)
{
    return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, Message);
}
TSharedPtr<FJsonObject> VectorJson(const FVector& V)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetNumberField(TEXT("x"), V.X); Out->SetNumberField(TEXT("y"), V.Y); Out->SetNumberField(TEXT("z"), V.Z);
    return Out;
}
TSharedPtr<FJsonObject> TransformJson(const FTransform& T)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetObjectField(TEXT("location"), VectorJson(T.GetLocation()));
    Out->SetObjectField(TEXT("scale"), VectorJson(T.GetScale3D()));
    auto R = MakeShared<FJsonObject>(); const FRotator Rot = T.Rotator();
    R->SetNumberField(TEXT("pitch"), Rot.Pitch); R->SetNumberField(TEXT("yaw"), Rot.Yaw); R->SetNumberField(TEXT("roll"), Rot.Roll);
    Out->SetObjectField(TEXT("rotation"), R);
    return Out;
}
FVector Vector(const TSharedPtr<FJsonObject>& V)
{
    return FVector(V->GetNumberField(TEXT("x")), V->GetNumberField(TEXT("y")), V->GetNumberField(TEXT("z")));
}
bool Supported(AStaticMeshActor* Actor)
{
    if (!IsValid(Actor) || Actor->IsActorBeingDestroyed() || Actor->GetClass() != AStaticMeshActor::StaticClass()) return false;
    auto* Root = Actor->GetStaticMeshComponent();
    return IsValid(Root) && Actor->GetRootComponent() == Root && Root->IsRegistered()
        && Root->Mobility == EComponentMobility::Movable && !Root->IsSimulatingPhysics()
        && !Root->GetAttachParent() && Root->GetAttachChildren().IsEmpty();
}
FString Conflict(const FPlan& Plan)
{
    if (!GEditor || GEditor->IsPlaySessionInProgress()) return TEXT("Editor unavailable or PIE active");
    UWorld* World = GEditor->GetEditorWorldContext().World();
    AStaticMeshActor* Actor = Plan.Actor.Get();
    if (!World || Plan.World.Get() != World || World->WorldType != EWorldType::Editor || World->GetPathName() != Plan.WorldPath)
        return TEXT("Editor world changed");
    if (!Supported(Actor) || Actor->GetWorld() != World || Actor->GetPathName() != Plan.ActorPath
        || Actor->GetStaticMeshComponent() != Plan.Root.Get() || Actor->GetStaticMeshComponent()->GetStaticMesh() != Plan.Mesh.Get())
        return TEXT("Actor identity, root, mesh or supported configuration changed");
    if (!Actor->GetActorTransform().Equals(Plan.Before, TransformTolerance)) return TEXT("Actor transform changed since planning");
    return FString();
}
TSharedPtr<FJsonObject> Describe(const FPlan& Plan)
{
    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("plan_id"), Plan.Id); Out->SetStringField(TEXT("expected_hash"), Plan.Hash);
    Out->SetStringField(TEXT("kind"), TEXT("actor_transform")); Out->SetStringField(TEXT("state"), Plan.State);
    Out->SetStringField(TEXT("actor_path"), Plan.ActorPath); Out->SetStringField(TEXT("world_path"), Plan.WorldPath);
    Out->SetObjectField(TEXT("before"), TransformJson(Plan.Before)); Out->SetObjectField(TEXT("after"), TransformJson(Plan.After));
    Out->SetNumberField(TEXT("expires_in_seconds"), FMath::Max(0.0, Plan.ExpiresAt - FPlatformTime::Seconds()));
    const FString Reason = Plan.State == TEXT("pending") ? Conflict(Plan) : TEXT("Plan already consumed");
    Out->SetBoolField(TEXT("applicable"), Reason.IsEmpty()); Out->SetStringField(TEXT("conflict"), Reason);
    Out->SetStringField(TEXT("required_scope"), TEXT("scene"));
    Out->SetBoolField(TEXT("writes_files"), false);
    Out->SetNumberField(TEXT("transform_tolerance"), TransformTolerance);
    return Out;
}
FPlan* FindOwned(const FString& Id, const FMCPRequestContext& Context)
{
    FPlan* Plan = Plans.Find(Id);
    return Plan && Plan->Principal == Context.PrincipalId && Plan->Session == Context.SessionId ? Plan : nullptr;
}
const TCHAR* PlanSchema = TEXT(R"({"type":"object","required":["plan_id","expected_hash","state","actor_path","world_path","before","after","applicable"],"properties":{"plan_id":{"type":"string"},"expected_hash":{"type":"string"},"state":{"type":"string"},"actor_path":{"type":"string"},"world_path":{"type":"string"},"before":{"type":"object"},"after":{"type":"object"},"applicable":{"type":"boolean"}}})");
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    auto PositionSchema = StringToJson(TEXT(R"({"type":"object","additionalProperties":false,"required":["x","y","z"],"properties":{"x":{"type":"number","minimum":-100000000,"maximum":100000000},"y":{"type":"number","minimum":-100000000,"maximum":100000000},"z":{"type":"number","minimum":-100000000,"maximum":100000000}}})"));
    auto RotationSchema = StringToJson(TEXT(R"({"type":"object","additionalProperties":false,"required":["pitch","yaw","roll"],"properties":{"pitch":{"type":"number","minimum":-360000,"maximum":360000},"yaw":{"type":"number","minimum":-360000,"maximum":360000},"roll":{"type":"number","minimum":-360000,"maximum":360000}}})"));
    auto ScaleSchema = StringToJson(TEXT(R"({"type":"object","additionalProperties":false,"required":["x","y","z"],"properties":{"x":{"type":"number","exclusiveMinimum":0,"maximum":1000},"y":{"type":"number","exclusiveMinimum":0,"maximum":1000},"z":{"type":"number","exclusiveMinimum":0,"maximum":1000}}})"));
    FMCPToolBuilder(Registry, TEXT("plan_actor_transform"))
        .Description(TEXT("Capture an owner-bound, expiring absolute transform plan without editing the actor. Initial support: exact native movable StaticMeshActor, no physics or attachments/children, in the current editor world. Use an exact actor object path, never a label. Apply requires Scene scope, the returned expected_hash and an idempotency key."))
        .ReadOnly().RequiresPieOff()
        .StringArg(TEXT("actor_path"), TEXT("Exact loaded actor object path from list_actors"), true)
        .ObjectArg(TEXT("location"), TEXT("Absolute world location in centimeters"), PositionSchema, true)
        .ObjectArg(TEXT("rotation"), TEXT("Absolute rotation in degrees"), RotationSchema, true)
        .ObjectArg(TEXT("scale"), TEXT("Absolute positive world scale"), ScaleSchema, true)
        .IntArg(TEXT("ttl_seconds"), TEXT("Plan lifetime: 1..600 seconds, default 300"))
        .OutputSchema(PlanSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread() || Applying) return Error(TEXT("Plan service busy or called off game thread"));
            Sweep();
            int32 Ttl = 300;
            if (Args->HasField(TEXT("ttl_seconds")))
            {
                const double Requested = Args->GetNumberField(TEXT("ttl_seconds"));
                if (Requested < 1 || Requested > 600) return Error(TEXT("ttl_seconds must be 1..600"));
                Ttl = static_cast<int32>(Requested);
            }
            int32 Owned = 0;
            for (const auto& Pair : Plans) if (Pair.Value.Principal == Context.PrincipalId && Pair.Value.Session == Context.SessionId) ++Owned;
            if (Plans.Num() >= 256 || Owned >= 32) return Error(TEXT("Plan capacity reached; wait for expiry or end the session"));
            UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (!World || World->WorldType != EWorldType::Editor || GEditor->IsPlaySessionInProgress()) return Error(TEXT("An idle editor world is required"));
            AStaticMeshActor* Actor = nullptr;
            const FString Path = Args->GetStringField(TEXT("actor_path"));
            for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
                if (It->GetPathName() == Path) { Actor = *It; break; }
            if (!Supported(Actor)) return Error(TEXT("Target is missing or unsupported: require an exact native movable StaticMeshActor without physics or attachments/children"));
            FPlan Plan;
            Plan.Id = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
            Plan.Principal = Context.PrincipalId; Plan.Session = Context.SessionId;
            Plan.Actor = Actor; Plan.Root = Actor->GetStaticMeshComponent(); Plan.Mesh = Actor->GetStaticMeshComponent()->GetStaticMesh();
            Plan.World = World; Plan.WorldPath = World->GetPathName(); Plan.ActorPath = Path;
            Plan.Before = Actor->GetActorTransform();
            const auto R = Args->GetObjectField(TEXT("rotation"));
            Plan.After = FTransform(FRotator(R->GetNumberField(TEXT("pitch")), R->GetNumberField(TEXT("yaw")), R->GetNumberField(TEXT("roll"))),
                Vector(Args->GetObjectField(TEXT("location"))), Vector(Args->GetObjectField(TEXT("scale"))));
            if (Plan.Before.ContainsNaN() || Plan.After.ContainsNaN()) return Error(TEXT("Actor transforms must be finite"));
            Plan.ExpiresAt = FPlatformTime::Seconds() + Ttl;
            auto Fingerprint = MakeShared<FJsonObject>();
            Fingerprint->SetStringField(TEXT("plan_id"), Plan.Id); Fingerprint->SetStringField(TEXT("actor_path"), Path);
            Fingerprint->SetStringField(TEXT("world_path"), Plan.WorldPath);
            Fingerprint->SetObjectField(TEXT("before"), TransformJson(Plan.Before)); Fingerprint->SetObjectField(TEXT("after"), TransformJson(Plan.After));
            FTCHARToUTF8 Bytes(*JsonToString(Fingerprint));
            Plan.Hash = FSHA1::HashBuffer(Bytes.Get(), Bytes.Length()).ToString();
            const auto Out = Describe(Plan);
            const FString Id = Plan.Id;
            Plans.Add(Id, MoveTemp(Plan));
            return FMCPToolResult::SuccessStructured(TEXT("Transform planned; actor and package state were not edited."), Out);
        });
    FMCPToolBuilder(Registry, TEXT("get_change_plan"))
        .Description(TEXT("Inspect an owned change plan (transform plan or plan_actor_changes plan): expiry, current applicability, operations, effect journal and recovery verdict. Plans are local to this editor process and originating principal/session."))
        .ReadOnly().Idempotent().StringArg(TEXT("plan_id"), TEXT("Plan identifier"), true).OutputSchema(PlanSchema)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread() || Applying) return Error(TEXT("Plan service busy or called off game thread"));
            Sweep(); const FPlan* Plan = FindOwned(Args->GetStringField(TEXT("plan_id")), Context);
            if (!Plan) { FMCPToolResult General; if (MCPGeneralPlans::Describe(Args->GetStringField(TEXT("plan_id")), Context, General)) return General; return FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Unknown or expired plan")); }
            return FMCPToolResult::SuccessStructured(TEXT("Owned plan"), Describe(*Plan));
        });
    FMCPToolBuilder(Registry, TEXT("apply_change_plan"))
        .Description(TEXT("Apply an owned change plan (transform plan or plan_actor_changes plan) only while its targets still match the captured state. Requires expected_hash and a non-empty idempotency_key; retries with the same key return the recorded result without reapplying. For plan_actor_changes plans the result carries an effect journal with before/after read-back per effect; when an operation fails, the earlier effects are rolled back in reverse order and recovery reports verified, partial or failed. Transform plans keep their original behaviour (no automatic recovery)."))
        .Idempotent().RequiresPieOff()
        .StringArg(TEXT("plan_id"), TEXT("Plan identifier"), true)
        .StringArg(TEXT("expected_hash"), TEXT("Exact fingerprint returned by planning"), true)
        .StringArg(TEXT("idempotency_key"), TEXT("1..128 character key, unique across this owner's retained plans"), true)
        .OutputSchema(TEXT(R"({"type":"object","required":["plan_id","verified","replayed","state"],"properties":{"plan_id":{"type":"string"},"verified":{"type":"boolean"},"replayed":{"type":"boolean"},"state":{"type":"string"},"observed":{"type":"object"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread() || Applying) return Error(TEXT("Plan service busy or called off game thread"));
            if (!Context.HasScope(EMCPScope::Scene)) return Error(TEXT("Scene scope required"));
            Sweep();
            FPlan* Plan = FindOwned(Args->GetStringField(TEXT("plan_id")), Context);
            if (!Plan)
            {
                FMCPToolResult General; // v5 increment 25: general actor change plans share this entry point
                if (MCPGeneralPlans::Apply(Args->GetStringField(TEXT("plan_id")), Args->GetStringField(TEXT("expected_hash")), Args->GetStringField(TEXT("idempotency_key")), Context, General)) return General;
                return FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Unknown or expired plan"));
            }
            if (Args->GetStringField(TEXT("expected_hash")) != Plan->Hash) return Error(TEXT("Plan fingerprint mismatch"));
            const FString Key = Args->GetStringField(TEXT("idempotency_key"));
            if (Key.IsEmpty() || Key.Len() > 128) return Error(TEXT("idempotency_key must contain 1..128 characters"));
            if (Plan->State != TEXT("pending"))
            {
                if (Plan->AppliedKey != Key) return Error(TEXT("Consumed plan cannot use a different idempotency key"));
                FMCPToolResult Replay = Plan->Result;
                auto Copy = MakeShared<FJsonObject>(); FJsonObject::Duplicate(Replay.StructuredContent, Copy);
                Copy->SetBoolField(TEXT("replayed"), true); Replay.StructuredContent = Copy;
                return Replay;
            }
            for (const auto& Pair : Plans)
                if (Pair.Value.Principal == Context.PrincipalId && Pair.Value.Session == Context.SessionId && Pair.Value.AppliedKey == Key)
                    return Error(TEXT("Idempotency key already belongs to a different retained plan"));
            const FString Reason = Conflict(*Plan);
            if (!Reason.IsEmpty()) return Error(Reason);
            if (Context.IsCancelled()) return Error(TEXT("Cancelled before apply"));
            TGuardValue<bool> Guard(Applying, true);
            // Work on an owned value copy: editor callbacks may terminate a session.
            FPlan Working = *Plan;
            Working.State = TEXT("applying"); Working.AppliedKey = Key;
            Plan->State = Working.State; Plan->AppliedKey = Key;
            auto* Actor = Working.Actor.Get(); auto* Root = Working.Root.Get();
            Actor->Modify();
            if (Working.Actor.IsValid() && Working.Root.IsValid() && Supported(Actor)) Root->Modify();
            const bool Set = Working.Actor.IsValid() && Working.Root.IsValid() && Supported(Actor)
                && Actor->SetActorTransform(Working.After, false, nullptr, ETeleportType::TeleportPhysics);
            const bool Verified = Set && Working.Actor.IsValid() && Working.Root.IsValid()
                && Supported(Actor) && Actor->GetWorld() == Working.World.Get() && Actor->GetRootComponent() == Root
                && Root->GetStaticMesh() == Working.Mesh.Get() && Actor->GetActorTransform().Equals(Working.After, TransformTolerance);
            Working.State = Verified ? TEXT("applied") : TEXT("failed");
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("plan_id"), Working.Id); Out->SetStringField(TEXT("state"), Working.State);
            Out->SetBoolField(TEXT("verified"), Verified); Out->SetBoolField(TEXT("replayed"), false);
            Out->SetStringField(TEXT("recovery"), TEXT("not_attempted"));
            if (Working.Actor.IsValid()) Out->SetObjectField(TEXT("observed"), TransformJson(Actor->GetActorTransform()));
            Working.Result = FMCPToolResult::SuccessStructured(Verified ? TEXT("Planned transform applied and read back successfully.")
                : TEXT("Transform verification failed; effects may remain. The plan is consumed and will not be reapplied."), Out);
            Working.Result.bIsError = !Verified;
            const FMCPToolResult Result = Working.Result;
            if (Plans.Contains(Working.Id)) Plans[Working.Id] = MoveTemp(Working);
            return Result;
        });
}
void RegisterResources(FMCPResourceProvider& Provider)
{
    FMCPResourceDefinition Def; Def.Uri = TEXT("unreal://plans/{plan_id}"); Def.Name = TEXT("Change plan");
    Def.Description = TEXT("Owned actor transform plan: preview, expiry and current applicability; owner only."); Def.MimeType = TEXT("application/json"); Def.bTemplate = true;
    FMCPResourceReaderCtx Reader; Reader.BindLambda([](const FString& Uri, const FMCPRequestContext& Context)
    {
        Sweep();
        const FPlan* Plan = FindOwned(Uri.Mid(FString(TEXT("unreal://plans/")).Len()), Context);
        if (!Plan)
        {
            FString GeneralJson;
            if (MCPGeneralPlans::ResourceText(Uri.Mid(FString(TEXT("unreal://plans/")).Len()), Context, GeneralJson)) { FMCPResourceContent C; C.Uri = Uri; C.MimeType = TEXT("application/json"); C.Text = GeneralJson; return C; }
            return FMCPResourceContent::NotFound(Uri, TEXT("Unknown, expired or foreign plan"));
        }
        FMCPResourceContent C; C.Uri = Uri; C.MimeType = TEXT("application/json"); C.Text = JsonToString(Describe(*Plan)); return C;
    });
    Provider.RegisterResourceCtx(Def, Reader);
}

void ClearSession(const FString& SessionId)
{
    check(IsInGameThread());
    for (auto It = Plans.CreateIterator(); It; ++It) if (It.Value().Session == SessionId) It.RemoveCurrent();
    MCPGeneralPlans::ClearSession(SessionId);
}
void Reset() { check(IsInGameThread()); Plans.Empty(); MCPGeneralPlans::Reset(); }
TSharedPtr<FJsonObject> DiagnosticsJson()
{
    auto Out = MakeShared<FJsonObject>(); int32 Pending = 0;
    for (const auto& P : Plans) if (P.Value.State == TEXT("pending")) ++Pending;
    Out->SetNumberField(TEXT("plans_retained"), Plans.Num()); Out->SetNumberField(TEXT("plans_pending"), Pending);
    Out->SetNumberField(TEXT("plans_capacity"), 256); Out->SetBoolField(TEXT("applying"), Applying);
    Out->SetObjectField(TEXT("general"), MCPGeneralPlans::DiagnosticsJson());
    return Out;
}
}
