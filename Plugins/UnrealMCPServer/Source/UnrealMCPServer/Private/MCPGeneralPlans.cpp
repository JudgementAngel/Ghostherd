// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 25: general actor change plans (V5-06 preflight plans, V5-07 verified recovery, V5-08 apply).
//
// A plan is a typed list of operations bound to the state it was planned against. Planning edits nothing.
// Applying runs the operations in order inside the registry's editor transaction and writes an effect
// journal: for every effect the before value, the after value read back from the editor, and whether the
// read-back matched. If an operation fails, the effects already applied are inverted in reverse order and
// each restore is verified; the result states exactly what survived. revert_change_plan does the same for
// a successfully applied plan. Recovery is reported as verified, partial or not_attempted; nothing is
// claimed beyond what was read back.

#include "MCPGeneralPlans.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPRequestContext.h"
#include "Common/MCPPropertyIO.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace MCPGeneralPlans
{
namespace
{
constexpr int32 MaxOps = 32, MaxPlansPerSession = 32, MaxPlansGlobal = 256, MaxCapturedProperties = 200;
constexpr double DefaultTtl = 300.0, MaxTtl = 600.0, TransformTolerance = 1e-4;

struct FOp
{
    FString Kind;                 // set_property | set_transform | rename | create | delete
    FString ActorPath, Component, Property, Label, ClassPath;
    TSharedPtr<FJsonValue> Value; // set_property target value
    FTransform Transform;         // set_transform / create
    bool bHasTransform = false;
    // captured before-state
    TWeakObjectPtr<AActor> Actor; TWeakObjectPtr<UObject> PropertyOwner;
    TSharedPtr<FJsonValue> Before; FTransform BeforeTransform; FString BeforeLabel;
    TSharedPtr<FJsonObject> BeforeProperties; // delete: exported editable properties of actor and root
    FString BeforeClassPath; FTransform BeforeTransformForDelete;
};
struct FEffect
{
    int32 Op = -1; FString Kind, Target, State = TEXT("applied"), Error; // applied | failed | reverted | revert_failed
    TSharedPtr<FJsonValue> Before, After; bool bVerified = false;
    TWeakObjectPtr<AActor> Created; // create: the spawned actor; delete: the recreated actor after revert
    FString CreatedPath;
};
struct FPlan
{
    FString Id, Hash, Principal, Session, WorldPath, State = TEXT("pending"), AppliedKey, Recovery = TEXT("not_attempted");
    TWeakObjectPtr<UWorld> World;
    TArray<FOp> Ops; TArray<FEffect> Journal;
    double ExpiresAt = 0.0, CreatedAt = 0.0; FString CreatedUtc, AppliedUtc, RevertedUtc;
    FMCPToolResult Result;
};
TMap<FString, FPlan> Plans;
bool Applying = false;

FMCPToolResult Err(EMCPError Code, const FString& Message, const FString& Hint = FString()) { return FMCPToolResult::ErrorStructured(Code, Message, Hint); }
void Sweep() { const double Now = FPlatformTime::Seconds(); for (auto It = Plans.CreateIterator(); It; ++It) if (Now >= It.Value().ExpiresAt) It.RemoveCurrent(); }
FPlan* FindOwned(const FString& Id, const FMCPRequestContext& Context)
{
    FPlan* P = Plans.Find(Id);
    return P && P->Principal == Context.PrincipalId && P->Session == Context.SessionId ? P : nullptr;
}
UWorld* EditorWorld() { return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr; }
AActor* FindActor(UWorld* World, const FString& Path)
{
    if (!World || Path.IsEmpty()) return nullptr;
    AActor* A = FindObject<AActor>(nullptr, *Path);
    return A && A->GetWorld() == World && IsValid(A) ? A : nullptr;
}
TSharedPtr<FJsonObject> TransformJson(const FTransform& T)
{
    auto O = MakeShared<FJsonObject>();
    auto V = [](const FVector& X) { auto J = MakeShared<FJsonObject>(); J->SetNumberField(TEXT("x"), X.X); J->SetNumberField(TEXT("y"), X.Y); J->SetNumberField(TEXT("z"), X.Z); return J; };
    const FRotator R = T.Rotator(); auto RJ = MakeShared<FJsonObject>(); RJ->SetNumberField(TEXT("pitch"), R.Pitch); RJ->SetNumberField(TEXT("yaw"), R.Yaw); RJ->SetNumberField(TEXT("roll"), R.Roll);
    O->SetObjectField(TEXT("location"), V(T.GetLocation())); O->SetObjectField(TEXT("rotation"), RJ); O->SetObjectField(TEXT("scale"), V(T.GetScale3D()));
    return O;
}
bool ParseTransform(const TSharedPtr<FJsonObject>& O, FTransform& Out, FString& Error)
{
    const TSharedPtr<FJsonObject>* L; const TSharedPtr<FJsonObject>* R; const TSharedPtr<FJsonObject>* S;
    if (!O->TryGetObjectField(TEXT("location"), L) || !O->TryGetObjectField(TEXT("rotation"), R) || !O->TryGetObjectField(TEXT("scale"), S)) { Error = TEXT("location, rotation and scale are required"); return false; }
    auto Num = [](const TSharedPtr<FJsonObject>& J, const TCHAR* K) { return J->HasField(K) ? J->GetNumberField(K) : 0.0; };
    const FVector Loc(Num(*L, TEXT("x")), Num(*L, TEXT("y")), Num(*L, TEXT("z"))), Scale(Num(*S, TEXT("x")), Num(*S, TEXT("y")), Num(*S, TEXT("z")));
    const FRotator Rot(Num(*R, TEXT("pitch")), Num(*R, TEXT("yaw")), Num(*R, TEXT("roll")));
    if (Loc.GetAbsMax() > 1e8 || Scale.GetMin() <= 0.0 || Scale.GetAbsMax() > 1000.0 || FMath::Abs(Rot.Pitch) > 360000 || FMath::Abs(Rot.Yaw) > 360000 || FMath::Abs(Rot.Roll) > 360000) { Error = TEXT("transform out of bounds (|location| <= 1e8, 0 < scale <= 1000, |rotation| <= 360000)"); return false; }
    Out = FTransform(Rot, Loc, Scale); return true;
}
/** Resolve the object that owns the property: the actor itself or one of its components by name. */
UObject* PropertyOwner(AActor* Actor, const FString& Component, FString& Error)
{
    if (Component.IsEmpty()) return Actor;
    TInlineComponentArray<UActorComponent*> Comps; Actor->GetComponents(Comps);
    for (UActorComponent* C : Comps) if (C && C->GetName() == Component) return C;
    Error = FString::Printf(TEXT("Component '%s' not found on %s"), *Component, *Actor->GetActorLabel()); return nullptr;
}
FProperty* FindEditableProperty(UObject* Owner, const FString& Name, FString& Error)
{
    FProperty* P = Owner->GetClass()->FindPropertyByName(*Name);
    if (!P) { Error = FString::Printf(TEXT("Property '%s' not found on %s"), *Name, *Owner->GetClass()->GetName()); return nullptr; }
    if (!P->HasAnyPropertyFlags(CPF_Edit) || P->HasAnyPropertyFlags(CPF_EditConst)) { Error = FString::Printf(TEXT("Property '%s' is not editable"), *Name); return nullptr; }
    return P;
}
bool SetProp(UObject* Owner, FProperty* P, const TSharedPtr<FJsonValue>& Value, FString& Error)
{
    Owner->Modify(); Owner->PreEditChange(P);
    const bool bOk = MCPCommon::SetPropertyFromJson(P, Owner, Owner, Value, Error);
    FPropertyChangedEvent Ev(P); Owner->PostEditChangeProperty(Ev);
    return bOk;
}
bool JsonEquals(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
{
    if (!A.IsValid() || !B.IsValid()) return A.IsValid() == B.IsValid();
    auto Ser = [](const TSharedPtr<FJsonValue>& V) { auto W = MakeShared<FJsonObject>(); W->SetField(TEXT("v"), V); return JsonToString(W); };
    if (A->Type == EJson::Number && B->Type == EJson::Number) return FMath::IsNearlyEqual(A->AsNumber(), B->AsNumber(), 1e-4);
    return Ser(A) == Ser(B);
}
TSharedPtr<FJsonObject> ExportEditable(UObject* Obj, int32& Budget)
{
    auto Out = MakeShared<FJsonObject>();
    for (TFieldIterator<FProperty> It(Obj->GetClass()); It && Budget > 0; ++It)
    {
        FProperty* P = *It;
        if (!P->HasAnyPropertyFlags(CPF_Edit) || P->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated | CPF_DisableEditOnInstance)) continue;
        if (CastField<FObjectPropertyBase>(P) && !CastField<FSoftObjectProperty>(P)) { if (!CastField<FObjectProperty>(P)) continue; }
        TSharedPtr<FJsonValue> V = MCPCommon::ExportPropertyToJson(P, Obj);
        if (V.IsValid() && V->Type != EJson::Null) { Out->SetField(P->GetName(), V); --Budget; }
    }
    return Out;
}
TSharedPtr<FJsonObject> EffectJson(const FEffect& E)
{
    auto J = MakeShared<FJsonObject>();
    J->SetNumberField(TEXT("op"), E.Op); J->SetStringField(TEXT("kind"), E.Kind); J->SetStringField(TEXT("target"), E.Target); J->SetStringField(TEXT("state"), E.State);
    J->SetBoolField(TEXT("verified"), E.bVerified);
    if (E.Before.IsValid()) J->SetField(TEXT("before"), E.Before); if (E.After.IsValid()) J->SetField(TEXT("after"), E.After);
    if (!E.Error.IsEmpty()) J->SetStringField(TEXT("error"), E.Error); if (!E.CreatedPath.IsEmpty()) J->SetStringField(TEXT("created_path"), E.CreatedPath);
    return J;
}
TSharedPtr<FJsonObject> OpJson(const FOp& Op)
{
    auto J = MakeShared<FJsonObject>(); J->SetStringField(TEXT("kind"), Op.Kind);
    if (!Op.ActorPath.IsEmpty()) J->SetStringField(TEXT("actor_path"), Op.ActorPath); if (!Op.Component.IsEmpty()) J->SetStringField(TEXT("component"), Op.Component);
    if (!Op.Property.IsEmpty()) J->SetStringField(TEXT("property"), Op.Property); if (Op.Value.IsValid()) J->SetField(TEXT("value"), Op.Value);
    if (!Op.Label.IsEmpty()) J->SetStringField(TEXT("label"), Op.Label); if (!Op.ClassPath.IsEmpty()) J->SetStringField(TEXT("class"), Op.ClassPath);
    if (Op.bHasTransform) J->SetObjectField(TEXT("transform"), TransformJson(Op.Transform));
    auto Before = MakeShared<FJsonObject>();
    if (Op.Kind == TEXT("set_property")) { if (Op.Before.IsValid()) Before->SetField(TEXT("value"), Op.Before); }
    else if (Op.Kind == TEXT("set_transform")) Before->SetObjectField(TEXT("transform"), TransformJson(Op.BeforeTransform));
    else if (Op.Kind == TEXT("rename")) Before->SetStringField(TEXT("label"), Op.BeforeLabel);
    else if (Op.Kind == TEXT("delete")) { Before->SetStringField(TEXT("class"), Op.BeforeClassPath); Before->SetStringField(TEXT("label"), Op.BeforeLabel); Before->SetObjectField(TEXT("transform"), TransformJson(Op.BeforeTransformForDelete)); Before->SetNumberField(TEXT("captured_properties"), Op.BeforeProperties.IsValid() ? Op.BeforeProperties->Values.Num() : 0); }
    J->SetObjectField(TEXT("before"), Before);
    return J;
}
/** Current applicability: every referenced actor still exists in the planned world and matches its captured state. */
FString Conflict(const FPlan& P)
{
    UWorld* World = P.World.Get();
    if (!World || World != EditorWorld()) return TEXT("Planned world is no longer the editor world");
    for (int32 i = 0; i < P.Ops.Num(); ++i)
    {
        const FOp& Op = P.Ops[i];
        if (Op.Kind == TEXT("create")) continue;
        AActor* A = Op.Actor.Get();
        if (!A || !IsValid(A) || A->GetWorld() != World) return FString::Printf(TEXT("op %d: actor %s no longer exists in the planned world"), i, *Op.ActorPath);
        if (Op.Kind == TEXT("set_transform") && !A->GetActorTransform().Equals(Op.BeforeTransform, TransformTolerance)) return FString::Printf(TEXT("op %d: transform changed since planning"), i);
        if (Op.Kind == TEXT("rename") && A->GetActorLabel() != Op.BeforeLabel) return FString::Printf(TEXT("op %d: label changed since planning"), i);
        if (Op.Kind == TEXT("set_property"))
        {
            UObject* Owner = Op.PropertyOwner.Get(); FString E;
            FProperty* Prop = Owner ? FindEditableProperty(Owner, Op.Property, E) : nullptr;
            if (!Prop) return FString::Printf(TEXT("op %d: %s"), i, E.IsEmpty() ? TEXT("property owner gone") : *E);
            if (!JsonEquals(MCPCommon::ExportPropertyToJson(Prop, Owner), Op.Before)) return FString::Printf(TEXT("op %d: property %s changed since planning"), i, *Op.Property);
        }
    }
    return FString();
}
TSharedPtr<FJsonObject> DescribeJson(const FPlan& P)
{
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("plan_id"), P.Id); O->SetStringField(TEXT("expected_hash"), P.Hash); O->SetStringField(TEXT("state"), P.State); O->SetStringField(TEXT("kind"), TEXT("actor_changes"));
    O->SetStringField(TEXT("world_path"), P.WorldPath); O->SetStringField(TEXT("created_at"), P.CreatedUtc);
    if (!P.AppliedUtc.IsEmpty()) O->SetStringField(TEXT("applied_at"), P.AppliedUtc); if (!P.RevertedUtc.IsEmpty()) O->SetStringField(TEXT("reverted_at"), P.RevertedUtc);
    O->SetNumberField(TEXT("ttl_remaining_seconds"), FMath::Max(0.0, P.ExpiresAt - FPlatformTime::Seconds()));
    const FString C = P.State == TEXT("pending") ? Conflict(P) : FString();
    O->SetBoolField(TEXT("applicable"), P.State == TEXT("pending") && C.IsEmpty()); if (!C.IsEmpty()) O->SetStringField(TEXT("conflict"), C);
    TArray<TSharedPtr<FJsonValue>> Ops; for (const FOp& Op : P.Ops) Ops.Add(MakeShared<FJsonValueObject>(OpJson(Op))); O->SetArrayField(TEXT("operations"), Ops);
    TArray<TSharedPtr<FJsonValue>> J; for (const FEffect& E : P.Journal) J.Add(MakeShared<FJsonValueObject>(EffectJson(E))); O->SetArrayField(TEXT("effect_journal"), J);
    O->SetStringField(TEXT("recovery"), P.Recovery); O->SetBoolField(TEXT("mutated_by_planning"), false);
    O->SetBoolField(TEXT("revertible"), P.State == TEXT("applied"));
    return O;
}

/** Apply one operation, filling the effect. Returns false on failure (effect.State=failed). */
bool ApplyOp(FPlan& P, int32 Index, FEffect& E)
{
    FOp& Op = P.Ops[Index]; E.Op = Index; E.Kind = Op.Kind; E.Target = Op.ActorPath;
    UWorld* World = P.World.Get(); FString Error;
    if (Op.Kind == TEXT("create"))
    {
        UClass* Cls = LoadClass<AActor>(nullptr, *Op.ClassPath);
        if (!Cls) { E.State = TEXT("failed"); E.Error = TEXT("Class not found: ") + Op.ClassPath; return false; }
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        AActor* A = World->SpawnActor<AActor>(Cls, Op.Transform, Params);
        if (!A) { E.State = TEXT("failed"); E.Error = TEXT("SpawnActor returned null"); return false; }
        if (!Op.Label.IsEmpty()) A->SetActorLabel(Op.Label);
        E.Created = A; E.CreatedPath = A->GetPathName(); E.Target = E.CreatedPath;
        E.After = MakeShared<FJsonValueObject>(TransformJson(A->GetActorTransform()));
        E.bVerified = A->GetActorTransform().Equals(Op.Transform, TransformTolerance) && (Op.Label.IsEmpty() || A->GetActorLabel() == Op.Label);
        return E.bVerified;
    }
    AActor* A = Op.Actor.Get();
    if (!A || !IsValid(A)) { E.State = TEXT("failed"); E.Error = TEXT("Actor gone"); return false; }
    if (Op.Kind == TEXT("set_transform"))
    {
        E.Before = MakeShared<FJsonValueObject>(TransformJson(A->GetActorTransform()));
        A->Modify(); if (A->GetRootComponent()) A->GetRootComponent()->Modify();
        if (!A->SetActorTransform(Op.Transform, false, nullptr, ETeleportType::TeleportPhysics)) { E.State = TEXT("failed"); E.Error = TEXT("SetActorTransform refused"); return false; }
        E.After = MakeShared<FJsonValueObject>(TransformJson(A->GetActorTransform()));
        E.bVerified = A->GetActorTransform().Equals(Op.Transform, TransformTolerance); if (!E.bVerified) { E.State = TEXT("failed"); E.Error = TEXT("read-back differs"); }
        return E.bVerified;
    }
    if (Op.Kind == TEXT("rename"))
    {
        E.Before = MakeShared<FJsonValueString>(A->GetActorLabel()); A->Modify(); A->SetActorLabel(Op.Label);
        E.After = MakeShared<FJsonValueString>(A->GetActorLabel()); E.bVerified = A->GetActorLabel() == Op.Label; if (!E.bVerified) { E.State = TEXT("failed"); E.Error = TEXT("label read-back differs"); }
        return E.bVerified;
    }
    if (Op.Kind == TEXT("set_property"))
    {
        UObject* Owner = Op.PropertyOwner.Get(); FProperty* Prop = Owner ? FindEditableProperty(Owner, Op.Property, Error) : nullptr;
        if (!Prop) { E.State = TEXT("failed"); E.Error = Error.IsEmpty() ? TEXT("property owner gone") : Error; return false; }
        E.Target = Op.ActorPath + (Op.Component.IsEmpty() ? TEXT("") : TEXT(".") + Op.Component) + TEXT(":") + Op.Property;
        E.Before = MCPCommon::ExportPropertyToJson(Prop, Owner);
        if (!SetProp(Owner, Prop, Op.Value, Error)) { E.State = TEXT("failed"); E.Error = Error; return false; }
        E.After = MCPCommon::ExportPropertyToJson(Prop, Owner);
        E.bVerified = JsonEquals(E.After, Op.Value); if (!E.bVerified) { E.State = TEXT("failed"); E.Error = TEXT("read-back differs from requested value"); }
        return E.bVerified;
    }
    if (Op.Kind == TEXT("delete"))
    {
        E.Before = MakeShared<FJsonValueObject>(TransformJson(A->GetActorTransform()));
        A->Modify();
        if (!World->DestroyActor(A)) { E.State = TEXT("failed"); E.Error = TEXT("DestroyActor refused"); return false; }
        E.bVerified = !Op.Actor.IsValid() || !IsValid(Op.Actor.Get()); E.After = MakeShared<FJsonValueString>(TEXT("destroyed"));
        return E.bVerified;
    }
    E.State = TEXT("failed"); E.Error = TEXT("unknown operation kind"); return false;
}
/** Invert one applied effect; verifies the restore. */
bool RevertEffect(FPlan& P, FEffect& E)
{
    FOp& Op = P.Ops[E.Op]; UWorld* World = P.World.Get(); FString Error;
    if (E.State != TEXT("applied")) return E.State == TEXT("reverted");
    bool bOk = false;
    if (Op.Kind == TEXT("create"))
    {
        AActor* A = E.Created.Get();
        bOk = !A || !IsValid(A) || (World && World->DestroyActor(A));
        if (bOk) bOk = !E.Created.IsValid() || !IsValid(E.Created.Get());
        if (!bOk) Error = TEXT("Could not destroy created actor");
    }
    else if (Op.Kind == TEXT("delete"))
    {
        UClass* Cls = LoadClass<AActor>(nullptr, *Op.BeforeClassPath);
        AActor* A = Cls && World ? World->SpawnActor<AActor>(Cls, Op.BeforeTransformForDelete) : nullptr;
        if (A)
        {
            A->SetActorLabel(Op.BeforeLabel);
            int32 Failed = 0, Applied = 0;
            if (Op.BeforeProperties.IsValid())
            {
                for (const auto& KV : Op.BeforeProperties->Values)
                {
                    FString PErr; FProperty* Prop = A->GetClass()->FindPropertyByName(*KV.Key);
                    if (!Prop) { ++Failed; continue; }
                    if (SetProp(A, Prop, KV.Value, PErr)) ++Applied; else ++Failed;
                }
            }
            E.Created = A; E.CreatedPath = A->GetPathName(); Op.Actor = A;
            bOk = A->GetActorTransform().Equals(Op.BeforeTransformForDelete, TransformTolerance) && A->GetActorLabel() == Op.BeforeLabel;
            if (Failed > 0) { Error = FString::Printf(TEXT("recreated with class, label and transform; %d of %d captured properties could not be restored"), Failed, Failed + Applied); }
            if (!bOk) Error = TEXT("recreated actor does not match the captured transform or label");
        }
        else Error = TEXT("Could not respawn the deleted actor");
    }
    else
    {
        AActor* A = Op.Actor.Get();
        if (!A || !IsValid(A)) Error = TEXT("Actor gone; cannot restore");
        else if (Op.Kind == TEXT("set_transform")) { A->Modify(); bOk = A->SetActorTransform(Op.BeforeTransform, false, nullptr, ETeleportType::TeleportPhysics) && A->GetActorTransform().Equals(Op.BeforeTransform, TransformTolerance); if (!bOk) Error = TEXT("transform restore read-back differs"); }
        else if (Op.Kind == TEXT("rename")) { A->Modify(); A->SetActorLabel(Op.BeforeLabel); bOk = A->GetActorLabel() == Op.BeforeLabel; if (!bOk) Error = TEXT("label restore read-back differs"); }
        else if (Op.Kind == TEXT("set_property"))
        {
            UObject* Owner = Op.PropertyOwner.Get(); FProperty* Prop = Owner ? FindEditableProperty(Owner, Op.Property, Error) : nullptr;
            if (Prop) { bOk = SetProp(Owner, Prop, Op.Before, Error) && JsonEquals(MCPCommon::ExportPropertyToJson(Prop, Owner), Op.Before); if (!bOk && Error.IsEmpty()) Error = TEXT("property restore read-back differs"); }
        }
    }
    E.State = bOk && Error.IsEmpty() ? TEXT("reverted") : (bOk ? TEXT("reverted_partial") : TEXT("revert_failed")); E.Error = Error;
    return bOk;
}
FString RevertJournal(FPlan& P)
{
    int32 Verified = 0, Partial = 0, Failed = 0, Total = 0;
    for (int32 i = P.Journal.Num() - 1; i >= 0; --i)
    {
        FEffect& E = P.Journal[i];
        if (E.State != TEXT("applied")) continue;
        ++Total;
        RevertEffect(P, E);
        if (E.State == TEXT("reverted")) ++Verified; else if (E.State == TEXT("reverted_partial")) ++Partial; else ++Failed;
    }
    if (Total == 0) return TEXT("not_needed");
    if (Failed == 0 && Partial == 0) return TEXT("verified");
    if (Failed == 0) return TEXT("partial");
    return Verified + Partial > 0 ? TEXT("partial") : TEXT("failed");
}
const TCHAR* TransformSchema = TEXT(R"({"type":"object","additionalProperties":false,"required":["location","rotation","scale"],"properties":{"location":{"type":"object"},"rotation":{"type":"object"},"scale":{"type":"object"}}})");
} // namespace

void RegisterAll(FMCPToolRegistry& Registry)
{
    auto OpsSchema = StringToJson(TEXT(R"({"type":"array","minItems":1,"maxItems":32,"items":{"type":"object","required":["kind"],"properties":{"kind":{"type":"string","enum":["set_property","set_transform","rename","create","delete"]},"actor_path":{"type":"string"},"component":{"type":"string"},"property":{"type":"string"},"value":{},"label":{"type":"string"},"class":{"type":"string"},"transform":{"type":"object"}}}})"));
    MCP_TOOL(Registry, "plan_actor_changes")
        .Description(TEXT("Plan a typed list of actor changes without editing anything (V5-06). Operations: set_property {actor_path, component?, property, value}, set_transform {actor_path, transform{location,rotation,scale}}, rename {actor_path, label}, create {class, label?, transform}, delete {actor_path}. Planning captures the before-state of every target (for delete: class, label, transform and up to 200 editable properties) and binds the plan to it; apply_change_plan refuses when any target changed. Returns plan_id and expected_hash. Apply with apply_change_plan (Scene scope, expected_hash, idempotency_key); the result carries an effect journal with before/after read-back per effect and a recovery verdict; revert_change_plan applies the recorded inverse. Plans expire after ttl_seconds (default 300, max 600); 32 per session."))
        .ReadOnly()
        .ObjectArg(TEXT("operations"), TEXT("Ordered operations (1..32)"), OpsSchema, true)
        .IntArg(TEXT("ttl_seconds"), TEXT("Plan lifetime 1..600 (default 300)"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread() || Applying) return Err(EMCPError::RequiresGameThread, TEXT("Plan service busy or called off game thread"));
            if (GEditor && GEditor->IsPlaySessionInProgress()) return Err(EMCPError::RequiresPieOff, TEXT("Stop PIE before planning actor changes"));
            Sweep();
            int32 Owned = 0; for (const auto& P : Plans) if (P.Value.Principal == Context.PrincipalId && P.Value.Session == Context.SessionId) ++Owned;
            if (Owned >= MaxPlansPerSession) return Err(EMCPError::Unsupported, TEXT("This session already retains 32 plans"));
            if (Plans.Num() >= MaxPlansGlobal) return Err(EMCPError::Unsupported, TEXT("Editor-wide plan limit reached"));
            UWorld* World = EditorWorld(); if (!World) return Err(EMCPError::Internal, TEXT("No editor world"));
            const TArray<TSharedPtr<FJsonValue>>* Raw; if (!Args->TryGetArrayField(TEXT("operations"), Raw) || Raw->Num() == 0 || Raw->Num() > MaxOps) return Err(EMCPError::OutOfRange, TEXT("operations must contain 1..32 entries"));
            FPlan P; P.World = World; P.WorldPath = World->GetPathName();
            for (int32 i = 0; i < Raw->Num(); ++i)
            {
                const TSharedPtr<FJsonObject> O = (*Raw)[i]->AsObject(); if (!O.IsValid()) return Err(EMCPError::OutOfRange, FString::Printf(TEXT("op %d is not an object"), i));
                FOp Op; Op.Kind = O->GetStringField(TEXT("kind")); O->TryGetStringField(TEXT("actor_path"), Op.ActorPath); O->TryGetStringField(TEXT("component"), Op.Component); O->TryGetStringField(TEXT("property"), Op.Property); O->TryGetStringField(TEXT("label"), Op.Label); O->TryGetStringField(TEXT("class"), Op.ClassPath);
                if (O->HasField(TEXT("value"))) Op.Value = O->TryGetField(TEXT("value"));
                FString E;
                if (O->HasTypedField<EJson::Object>(TEXT("transform"))) { if (!ParseTransform(O->GetObjectField(TEXT("transform")), Op.Transform, E)) return Err(EMCPError::OutOfRange, FString::Printf(TEXT("op %d: %s"), i, *E)); Op.bHasTransform = true; }
                if (Op.Kind == TEXT("create"))
                {
                    if (Op.ClassPath.IsEmpty() || !Op.bHasTransform) return Err(EMCPError::OutOfRange, FString::Printf(TEXT("op %d: create needs class and transform"), i));
                    if (!LoadClass<AActor>(nullptr, *Op.ClassPath)) return Err(EMCPError::NotFound, FString::Printf(TEXT("op %d: class not found: %s"), i, *Op.ClassPath));
                    P.Ops.Add(Op); continue;
                }
                AActor* A = FindActor(World, Op.ActorPath);
                if (!A) return Err(EMCPError::NotFound, FString::Printf(TEXT("op %d: actor not found in the editor world: %s"), i, *Op.ActorPath), TEXT("Use list_actors for exact loaded actor paths."));
                Op.Actor = A;
                if (Op.Kind == TEXT("set_transform")) { if (!Op.bHasTransform) return Err(EMCPError::OutOfRange, FString::Printf(TEXT("op %d: set_transform needs transform"), i)); if (!A->GetRootComponent() || A->GetRootComponent()->Mobility != EComponentMobility::Movable) return Err(EMCPError::Unsupported, FString::Printf(TEXT("op %d: actor root is not movable"), i)); Op.BeforeTransform = A->GetActorTransform(); }
                else if (Op.Kind == TEXT("rename")) { if (Op.Label.IsEmpty() || Op.Label.Len() > 200) return Err(EMCPError::OutOfRange, FString::Printf(TEXT("op %d: rename needs label (1..200)"), i)); Op.BeforeLabel = A->GetActorLabel(); }
                else if (Op.Kind == TEXT("set_property"))
                {
                    if (Op.Property.IsEmpty() || !Op.Value.IsValid()) return Err(EMCPError::OutOfRange, FString::Printf(TEXT("op %d: set_property needs property and value"), i));
                    UObject* Owner = PropertyOwner(A, Op.Component, E); if (!Owner) return Err(EMCPError::NotFound, FString::Printf(TEXT("op %d: %s"), i, *E));
                    FProperty* Prop = FindEditableProperty(Owner, Op.Property, E); if (!Prop) return Err(EMCPError::NotFound, FString::Printf(TEXT("op %d: %s"), i, *E));
                    Op.PropertyOwner = Owner; Op.Before = MCPCommon::ExportPropertyToJson(Prop, Owner);
                }
                else if (Op.Kind == TEXT("delete"))
                {
                    Op.BeforeClassPath = A->GetClass()->GetPathName(); Op.BeforeLabel = A->GetActorLabel(); Op.BeforeTransformForDelete = A->GetActorTransform();
                    int32 Budget = MaxCapturedProperties; Op.BeforeProperties = ExportEditable(A, Budget);
                }
                else return Err(EMCPError::OutOfRange, FString::Printf(TEXT("op %d: unknown kind %s"), i, *Op.Kind));
                P.Ops.Add(Op);
            }
            const double Ttl = Args->HasField(TEXT("ttl_seconds")) ? Args->GetNumberField(TEXT("ttl_seconds")) : DefaultTtl;
            if (Ttl < 1 || Ttl > MaxTtl) return Err(EMCPError::OutOfRange, TEXT("ttl_seconds must be 1..600"));
            P.Id = TEXT("plan-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
            P.Principal = Context.PrincipalId; P.Session = Context.SessionId; P.CreatedAt = FPlatformTime::Seconds(); P.CreatedUtc = FDateTime::UtcNow().ToIso8601(); P.ExpiresAt = P.CreatedAt + Ttl;
            TArray<TSharedPtr<FJsonValue>> OpsJ; for (const FOp& Op : P.Ops) OpsJ.Add(MakeShared<FJsonValueObject>(OpJson(Op)));
            auto HashSrc = MakeShared<FJsonObject>(); HashSrc->SetStringField(TEXT("id"), P.Id); HashSrc->SetArrayField(TEXT("ops"), OpsJ); HashSrc->SetStringField(TEXT("world"), P.WorldPath);
            const FString HashText = JsonToString(HashSrc); FTCHARToUTF8 Bytes(*HashText);
            P.Hash = FSHA1::HashBuffer(Bytes.Get(), Bytes.Length()).ToString();
            const FString Id = P.Id; Plans.Add(Id, MoveTemp(P));
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Planned %d operation(s) as %s; nothing was edited. Apply with apply_change_plan."), OpsJ.Num(), *Id), DescribeJson(Plans[Id]));
        });

    MCP_TOOL(Registry, "revert_change_plan")
        .Description(TEXT("Apply the recorded inverse of an applied actor change plan in reverse order and verify each restore (V5-07). Created actors are destroyed; deleted actors are recreated from the captured class, label, transform and editable properties (partial when a property cannot be restored); property, transform and label changes are restored to their captured values. Returns the updated effect journal and a recovery verdict: verified, partial, failed or not_needed. Requires Scene scope; refuses plans that were never applied or already reverted."))
        .Idempotent().RequiresPieOff()
        .StringArg(TEXT("plan_id"), TEXT("Applied plan identifier"), true)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread() || Applying) return Err(EMCPError::RequiresGameThread, TEXT("Plan service busy or called off game thread"));
            if (!Context.HasScope(EMCPScope::Scene)) return Err(EMCPError::ScopeDenied, TEXT("Scene scope required"));
            Sweep(); FPlan* P = FindOwned(Args->GetStringField(TEXT("plan_id")), Context);
            if (!P) return Err(EMCPError::NotFound, TEXT("Unknown, expired or foreign plan"));
            if (P->State != TEXT("applied")) return Err(EMCPError::Unsupported, FString::Printf(TEXT("Plan is %s; only applied plans can be reverted"), *P->State));
            if (!P->World.IsValid() || P->World.Get() != EditorWorld()) return Err(EMCPError::Unsupported, TEXT("Planned world is no longer the editor world"));
            TGuardValue<bool> Guard(Applying, true);
            const FString Verdict = RevertJournal(*P);
            P->State = Verdict == TEXT("verified") || Verdict == TEXT("not_needed") ? TEXT("reverted") : TEXT("revert_partial");
            P->Recovery = Verdict; P->RevertedUtc = FDateTime::UtcNow().ToIso8601();
            FMCPToolResult R = FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Revert %s."), *Verdict), DescribeJson(*P));
            R.bIsError = Verdict == TEXT("failed");
            return R;
        });
}

bool Describe(const FString& PlanId, const FMCPRequestContext& Context, FMCPToolResult& Out)
{
    Sweep(); if (!Plans.Contains(PlanId)) return false;
    const FPlan* P = FindOwned(PlanId, Context);
    Out = P ? FMCPToolResult::SuccessStructured(TEXT("Owned plan"), DescribeJson(*P)) : Err(EMCPError::NotFound, TEXT("Unknown or expired plan"));
    return true;
}
bool ResourceText(const FString& PlanId, const FMCPRequestContext& Context, FString& OutJson)
{
    Sweep(); const FPlan* P = FindOwned(PlanId, Context); if (!P) return false;
    OutJson = JsonToString(DescribeJson(*P)); return true;
}
bool Apply(const FString& PlanId, const FString& ExpectedHash, const FString& Key, const FMCPRequestContext& Context, FMCPToolResult& Out)
{
    Sweep(); if (!Plans.Contains(PlanId)) return false;
    FPlan* P = FindOwned(PlanId, Context);
    if (!P) { Out = Err(EMCPError::NotFound, TEXT("Unknown or expired plan")); return true; }
    if (Applying) { Out = Err(EMCPError::Unsupported, TEXT("Plan service busy")); return true; }
    if (!Context.HasScope(EMCPScope::Scene)) { Out = Err(EMCPError::ScopeDenied, TEXT("Scene scope required")); return true; }
    if (ExpectedHash != P->Hash) { Out = Err(EMCPError::Unsupported, TEXT("Plan fingerprint mismatch")); return true; }
    if (Key.IsEmpty() || Key.Len() > 128) { Out = Err(EMCPError::OutOfRange, TEXT("idempotency_key must contain 1..128 characters")); return true; }
    if (P->State != TEXT("pending"))
    {
        if (P->AppliedKey != Key) { Out = Err(EMCPError::Unsupported, TEXT("Consumed plan cannot use a different idempotency key")); return true; }
        FMCPToolResult Replay = P->Result; auto Copy = MakeShared<FJsonObject>(); FJsonObject::Duplicate(Replay.StructuredContent, Copy);
        Copy->SetBoolField(TEXT("replayed"), true); Replay.StructuredContent = Copy; Out = Replay; return true;
    }
    for (const auto& Pair : Plans) if (Pair.Value.Principal == Context.PrincipalId && Pair.Value.Session == Context.SessionId && Pair.Value.AppliedKey == Key) { Out = Err(EMCPError::Unsupported, TEXT("Idempotency key already belongs to a different retained plan")); return true; }
    const FString Reason = Conflict(*P); if (!Reason.IsEmpty()) { Out = Err(EMCPError::Unsupported, TEXT("Plan no longer applicable: ") + Reason); return true; }
    if (GEditor && GEditor->IsPlaySessionInProgress()) { Out = Err(EMCPError::RequiresPieOff, TEXT("Stop PIE before applying")); return true; }
    TGuardValue<bool> Guard(Applying, true);
    P->State = TEXT("applying"); P->AppliedKey = Key; P->Journal.Empty();
    int32 FailedAt = -1;
    for (int32 i = 0; i < P->Ops.Num(); ++i)
    {
        FEffect E; const bool bOk = ApplyOp(*P, i, E);
        P->Journal.Add(E);
        if (!bOk) { FailedAt = i; break; }
    }
    FString Verdict = TEXT("not_needed");
    if (FailedAt >= 0) { Verdict = RevertJournal(*P); P->Recovery = Verdict; P->State = TEXT("failed"); }
    else { P->State = TEXT("applied"); P->Recovery = TEXT("not_attempted"); }
    P->AppliedUtc = FDateTime::UtcNow().ToIso8601();
    auto O = DescribeJson(*P);
    O->SetBoolField(TEXT("verified"), FailedAt < 0); O->SetBoolField(TEXT("replayed"), false); O->SetNumberField(TEXT("effects_applied"), FailedAt < 0 ? P->Ops.Num() : FailedAt);
    if (FailedAt >= 0) { O->SetNumberField(TEXT("failed_op"), FailedAt); O->SetStringField(TEXT("failed_error"), P->Journal[FailedAt].Error); }
    P->Result = FMCPToolResult::SuccessStructured(FailedAt < 0 ? FString::Printf(TEXT("Applied and read back %d effect(s)."), P->Ops.Num())
        : FString::Printf(TEXT("Operation %d failed (%s); %d earlier effect(s) rolled back, recovery %s."), FailedAt, *P->Journal[FailedAt].Error, FailedAt, *Verdict), O);
    P->Result.bIsError = FailedAt >= 0;
    Out = P->Result; return true;
}
void ClearSession(const FString& SessionId) { for (auto It = Plans.CreateIterator(); It; ++It) if (It.Value().Session == SessionId) It.RemoveCurrent(); }
void Reset() { Plans.Empty(); }
TSharedPtr<FJsonObject> DiagnosticsJson()
{
    auto Out = MakeShared<FJsonObject>(); int32 Pending = 0, Applied = 0; for (const auto& P : Plans) { if (P.Value.State == TEXT("pending")) ++Pending; if (P.Value.State == TEXT("applied")) ++Applied; }
    Out->SetNumberField(TEXT("general_plans_retained"), Plans.Num()); Out->SetNumberField(TEXT("general_plans_pending"), Pending); Out->SetNumberField(TEXT("general_plans_applied"), Applied);
    return Out;
}
}
