// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 14: stable world-scoped object references (V5-19) and owned editor snapshots with
// focused diffs (V5-20), initial slices.

#include "MCPSnapshots.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPResourceProvider.h"
#include "MCPEditorSurfaces.h"
#include "Common/MCPPropertyIO.h"

#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "FileHelpers.h"
#include "LevelEditorViewport.h"
#include "Misc/SecureHash.h"
#include "UObject/Package.h"

namespace MCPSnapshots
{
namespace
{
constexpr int32 MaxSnapshotsPerSession = 16, MaxSnapshotsGlobal = 128, MaxTargets = 500, MaxQueries = 64, MaxProperties = 32;
constexpr double DefaultTtl = 900.0;

struct FSnapshot
{
    FString Id, Principal, Session, WorldId, Hash, CapturedAt;
    double ExpiresAt = 0.0;
    TSharedPtr<FJsonObject> Data;
};
TMap<FString, FSnapshot> Snapshots;

FMCPToolResult Error(EMCPError Code, const FString& Message, const FString& Hint = FString()) { return FMCPToolResult::ErrorStructured(Code, Message, Hint); }

UWorld* ResolveWorld(const FString& WorldId, FString& OutError)
{
    if (!GEngine) { OutError = TEXT("No engine"); return nullptr; }
    if (WorldId.IsEmpty() || WorldId == TEXT("editor"))
    {
        UWorld* W = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
        if (!W) OutError = TEXT("No editor world");
        return W;
    }
    for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
        if (Ctx.World() && (Ctx.World()->GetPathName() == WorldId || (WorldId == TEXT("pie") && Ctx.WorldType == EWorldType::PIE))) return Ctx.World();
    OutError = FString::Printf(TEXT("World not found: %s (use list_worlds)"), *WorldId);
    return nullptr;
}

/** Opaque reference: actor GUID bound to its world path. Never a raw pointer or label. */
FString MakeRef(UWorld* World, AActor* Actor)
{
    return FString::Printf(TEXT("actor:%s:%s"), *World->GetPathName(), *Actor->GetActorGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
}
AActor* FindByRef(UWorld* World, const FString& Ref, FString& OutError)
{
    TArray<FString> Parts; Ref.ParseIntoArray(Parts, TEXT(":"), false);
    if (Parts.Num() != 3 || Parts[0] != TEXT("actor")) { OutError = TEXT("malformed reference"); return nullptr; }
    if (Parts[1] != World->GetPathName()) { OutError = TEXT("reference belongs to a different world"); return nullptr; }
    FGuid Guid; if (!FGuid::Parse(Parts[2], Guid)) { OutError = TEXT("malformed actor GUID"); return nullptr; }
    for (TActorIterator<AActor> It(World); It; ++It) if (IsValid(*It) && It->GetActorGuid() == Guid) return *It;
    OutError = TEXT("actor no longer exists in this world");
    return nullptr;
}
TSharedPtr<FJsonObject> Candidate(UWorld* World, AActor* A)
{
    auto C = MakeShared<FJsonObject>();
    C->SetStringField(TEXT("ref"), MakeRef(World, A)); C->SetStringField(TEXT("path"), A->GetPathName());
    C->SetStringField(TEXT("label"), A->GetActorLabel()); C->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
    return C;
}
/** Resolve one query: an existing ref, an exact object path, or a label (ambiguous when repeated). */
TSharedPtr<FJsonObject> ResolveQuery(UWorld* World, const FString& Query)
{
    auto Out = MakeShared<FJsonObject>(); Out->SetStringField(TEXT("query"), Query);
    TArray<TSharedPtr<FJsonValue>> Candidates;
    if (Query.StartsWith(TEXT("actor:")))
    {
        FString Err; AActor* A = FindByRef(World, Query, Err);
        Out->SetStringField(TEXT("status"), A ? TEXT("resolved") : TEXT("missing"));
        if (A) Candidates.Add(MakeShared<FJsonValueObject>(Candidate(World, A))); else Out->SetStringField(TEXT("reason"), Err);
    }
    else
    {
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (!IsValid(*It)) continue;
            if (It->GetPathName() == Query || It->GetActorLabel() == Query || It->GetName() == Query) Candidates.Add(MakeShared<FJsonValueObject>(Candidate(World, *It)));
            if (Candidates.Num() > 16) break;
        }
        Out->SetStringField(TEXT("status"), Candidates.Num() == 1 ? TEXT("resolved") : (Candidates.IsEmpty() ? TEXT("missing") : TEXT("ambiguous")));
        if (Candidates.Num() > 1) Out->SetStringField(TEXT("reason"), TEXT("more than one actor matches; pick a ref from candidates"));
    }
    Out->SetArrayField(TEXT("candidates"), Candidates);
    return Out;
}

TSharedPtr<FJsonObject> VectorJson(const FVector& V) { auto O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("x"), V.X); O->SetNumberField(TEXT("y"), V.Y); O->SetNumberField(TEXT("z"), V.Z); return O; }
TSharedPtr<FJsonObject> TransformJson(const FTransform& T)
{
    auto O = MakeShared<FJsonObject>(); O->SetObjectField(TEXT("location"), VectorJson(T.GetLocation()));
    const FRotator R = T.Rotator(); auto RJ = MakeShared<FJsonObject>(); RJ->SetNumberField(TEXT("pitch"), R.Pitch); RJ->SetNumberField(TEXT("yaw"), R.Yaw); RJ->SetNumberField(TEXT("roll"), R.Roll);
    O->SetObjectField(TEXT("rotation"), RJ); O->SetObjectField(TEXT("scale"), VectorJson(T.GetScale3D()));
    return O;
}
TSharedPtr<FJsonObject> ActorJson(UWorld* World, AActor* A, const TArray<FString>& Properties)
{
    auto O = Candidate(World, A);
    O->SetObjectField(TEXT("transform"), TransformJson(A->GetActorTransform()));
    O->SetBoolField(TEXT("hidden"), A->IsHiddenEd());
    if (USceneComponent* Root = A->GetRootComponent()) O->SetStringField(TEXT("mobility"), Root->Mobility == EComponentMobility::Movable ? TEXT("movable") : (Root->Mobility == EComponentMobility::Stationary ? TEXT("stationary") : TEXT("static")));
    TArray<TSharedPtr<FJsonValue>> Comps;
    for (UActorComponent* C : A->GetComponents()) if (C) Comps.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s:%s"), *C->GetName(), *C->GetClass()->GetName())));
    Comps.Sort([](const TSharedPtr<FJsonValue>& L, const TSharedPtr<FJsonValue>& R) { return L->AsString() < R->AsString(); });
    O->SetArrayField(TEXT("components"), Comps);
    if (Properties.Num())
    {
        auto P = MakeShared<FJsonObject>();
        for (const FString& Name : Properties)
            if (FProperty* Prop = A->GetClass()->FindPropertyByName(FName(*Name)))
                if (TSharedPtr<FJsonValue> V = MCPCommon::ExportPropertyToJson(Prop, A)) P->SetField(Name, V);
        O->SetObjectField(TEXT("properties"), P);
    }
    return O;
}
FString Sha1(const FString& Text) { FTCHARToUTF8 Bytes(*Text); return FSHA1::HashBuffer(Bytes.Get(), Bytes.Length()).ToString(); }

void Sweep()
{
    const double Now = FPlatformTime::Seconds();
    for (auto It = Snapshots.CreateIterator(); It; ++It) if (Now >= It.Value().ExpiresAt) It.RemoveCurrent();
}
FSnapshot* FindOwned(const FString& Id, const FMCPRequestContext& Context)
{
    FSnapshot* S = Snapshots.Find(Id);
    return S && S->Principal == Context.PrincipalId && S->Session == Context.SessionId ? S : nullptr;
}
bool NearlyEqual(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B, const TCHAR* Key, double Tol)
{
    return FMath::Abs(A->GetNumberField(Key) - B->GetNumberField(Key)) <= Tol;
}
bool TransformsEqual(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B, double Tol)
{
    for (const TCHAR* Part : { TEXT("location"), TEXT("scale") })
        for (const TCHAR* K : { TEXT("x"), TEXT("y"), TEXT("z") })
            if (!NearlyEqual(A->GetObjectField(Part), B->GetObjectField(Part), K, Tol)) return false;
    for (const TCHAR* K : { TEXT("pitch"), TEXT("yaw"), TEXT("roll") })
        if (!NearlyEqual(A->GetObjectField(TEXT("rotation")), B->GetObjectField(TEXT("rotation")), K, Tol)) return false;
    return true;
}
TSharedPtr<FJsonObject> Meta(const FSnapshot& S)
{
    auto O = MakeShared<FJsonObject>();
    O->SetStringField(TEXT("snapshot_id"), S.Id); O->SetStringField(TEXT("world_id"), S.WorldId); O->SetStringField(TEXT("hash"), S.Hash);
    O->SetStringField(TEXT("captured_at"), S.CapturedAt); O->SetNumberField(TEXT("expires_in_seconds"), FMath::Max(0.0, S.ExpiresAt - FPlatformTime::Seconds()));
    O->SetNumberField(TEXT("actor_count"), S.Data->GetArrayField(TEXT("actors")).Num());
    return O;
}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    MCP_TOOL(Registry, "resolve_object_refs")
        .Description(TEXT("Resolve up to 64 queries (actor labels, object paths, names, or previously returned refs) inside one explicit world to stable references of the form actor:<world path>:<actor GUID>. Each result is resolved, missing or ambiguous with candidates; labels shared by several actors are reported as ambiguous rather than picked silently. Refs are bound to their world and to the actor's GUID, so a reloaded map, a PIE copy or a replaced actor does not resolve. Nothing is loaded or modified."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("world_id"), TEXT("World path from list_worlds, 'editor' (default) or 'pie'"))
        .StringArrayArg(TEXT("queries"), TEXT("1..64 labels, paths, names or refs"), true)
        .OutputSchema(TEXT(R"({"type":"object","required":["world_id","results"],"properties":{"world_id":{"type":"string"},"results":{"type":"array"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Reference resolution requires the game thread"));
            FString Err; UWorld* World = ResolveWorld(Args->HasField(TEXT("world_id")) ? Args->GetStringField(TEXT("world_id")) : FString(), Err);
            if (!World) return Error(EMCPError::NotFound, Err);
            const auto Queries = Args->GetArrayField(TEXT("queries"));
            if (Queries.Num() < 1 || Queries.Num() > MaxQueries) return Error(EMCPError::OutOfRange, TEXT("queries must contain 1..64 entries"));
            TArray<TSharedPtr<FJsonValue>> Results; int32 Resolved = 0;
            for (const auto& Q : Queries)
            {
                if (Q->Type != EJson::String || Q->AsString().IsEmpty()) return Error(EMCPError::OutOfRange, TEXT("queries must be non-empty strings"));
                const auto R = ResolveQuery(World, Q->AsString()); if (R->GetStringField(TEXT("status")) == TEXT("resolved")) ++Resolved;
                Results.Add(MakeShared<FJsonValueObject>(R));
            }
            auto Out = MakeShared<FJsonObject>(); Out->SetStringField(TEXT("world_id"), World->GetPathName()); Out->SetArrayField(TEXT("results"), Results);
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d of %d queries resolved in %s."), Resolved, Queries.Num(), *World->GetMapName()), Out);
        });

    MCP_TOOL(Registry, "capture_editor_snapshot")
        .Description(TEXT("Capture an owned, expiring structural snapshot of actors in one explicit world: stable refs, paths, labels, classes, transforms, mobility, component lists and up to 32 named properties, plus selection, dirty packages, map and editor camera. targets is a list of refs/labels/paths, or the single word 'selection' or 'all' (bounded to 500 actors). Returns snapshot_id and a content hash. Structural only: no image, no asset contents; pair with capture_editor_surface for pixels. Snapshots live in memory for ttl_seconds (default 900) and are removed on session cleanup."))
        .ReadOnly()
        .StringArg(TEXT("world_id"), TEXT("World path from list_worlds, 'editor' (default) or 'pie'"))
        .StringArrayArg(TEXT("targets"), TEXT("Refs, labels or paths; or ['selection'] or ['all']"), true)
        .StringArrayArg(TEXT("properties"), TEXT("Up to 32 actor property names to include"))
        .IntArg(TEXT("ttl_seconds"), TEXT("60..3600, default 900"))
        .OutputSchema(TEXT(R"({"type":"object","required":["snapshot_id","world_id","hash","captured_at","actor_count"],"properties":{"snapshot_id":{"type":"string"},"world_id":{"type":"string"},"hash":{"type":"string"},"captured_at":{"type":"string"},"expires_in_seconds":{"type":"number"},"actor_count":{"type":"integer"},"unresolved":{"type":"array"},"truncated":{"type":"boolean"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Snapshots require the game thread"));
            Sweep();
            FString Err; UWorld* World = ResolveWorld(Args->HasField(TEXT("world_id")) ? Args->GetStringField(TEXT("world_id")) : FString(), Err);
            if (!World) return Error(EMCPError::NotFound, Err);
            const double Ttl = Args->HasField(TEXT("ttl_seconds")) ? Args->GetNumberField(TEXT("ttl_seconds")) : DefaultTtl;
            if (Ttl < 60 || Ttl > 3600) return Error(EMCPError::OutOfRange, TEXT("ttl_seconds must be 60..3600"));
            TArray<FString> Properties;
            if (Args->HasField(TEXT("properties"))) for (const auto& V : Args->GetArrayField(TEXT("properties"))) if (V->Type == EJson::String) Properties.Add(V->AsString());
            if (Properties.Num() > MaxProperties) return Error(EMCPError::OutOfRange, TEXT("at most 32 properties"));
            int32 Owned = 0; for (const auto& P : Snapshots) if (P.Value.Principal == Context.PrincipalId && P.Value.Session == Context.SessionId) ++Owned;
            if (Owned >= MaxSnapshotsPerSession || Snapshots.Num() >= MaxSnapshotsGlobal) return Error(EMCPError::Unsupported, TEXT("Snapshot capacity reached; wait for expiry or end the session"));
            const auto Targets = Args->GetArrayField(TEXT("targets"));
            if (Targets.Num() < 1 || Targets.Num() > MaxTargets) return Error(EMCPError::OutOfRange, TEXT("targets must contain 1..500 entries"));
            TArray<AActor*> Actors; TArray<TSharedPtr<FJsonValue>> Unresolved; bool bTruncated = false;
            const FString First = Targets[0]->Type == EJson::String ? Targets[0]->AsString() : FString();
            if (Targets.Num() == 1 && (First == TEXT("all") || First == TEXT("selection")))
            {
                if (First == TEXT("all")) { for (TActorIterator<AActor> It(World); It; ++It) { if (!IsValid(*It)) continue; if (Actors.Num() >= MaxTargets) { bTruncated = true; break; } Actors.Add(*It); } }
                else if (GEditor) { USelection* Sel = GEditor->GetSelectedActors(); for (int32 I = 0; Sel && I < Sel->Num(); ++I) if (AActor* A = Cast<AActor>(Sel->GetSelectedObject(I))) if (A->GetWorld() == World) Actors.Add(A); }
            }
            else
                for (const auto& T : Targets)
                {
                    if (T->Type != EJson::String) return Error(EMCPError::OutOfRange, TEXT("targets must be strings"));
                    const auto R = ResolveQuery(World, T->AsString());
                    if (R->GetStringField(TEXT("status")) != TEXT("resolved")) { Unresolved.Add(MakeShared<FJsonValueObject>(R)); continue; }
                    FString E2; AActor* A = FindByRef(World, R->GetArrayField(TEXT("candidates"))[0]->AsObject()->GetStringField(TEXT("ref")), E2);
                    if (A) Actors.AddUnique(A);
                }
            Actors.Sort([](const AActor& L, const AActor& R) { return L.GetPathName() < R.GetPathName(); });
            auto Data = MakeShared<FJsonObject>();
            TArray<TSharedPtr<FJsonValue>> ActorsJson; for (AActor* A : Actors) ActorsJson.Add(MakeShared<FJsonValueObject>(ActorJson(World, A, Properties)));
            Data->SetArrayField(TEXT("actors"), ActorsJson);
            Data->SetStringField(TEXT("map"), World->GetMapName()); Data->SetStringField(TEXT("world_id"), World->GetPathName());
            TArray<TSharedPtr<FJsonValue>> Selected;
            if (GEditor) { USelection* Sel = GEditor->GetSelectedActors(); for (int32 I = 0; Sel && I < Sel->Num(); ++I) if (AActor* A = Cast<AActor>(Sel->GetSelectedObject(I))) if (A->GetWorld() == World) Selected.Add(MakeShared<FJsonValueString>(MakeRef(World, A))); }
            Data->SetArrayField(TEXT("selection"), Selected);
            TArray<UPackage*> Dirty; FEditorFileUtils::GetDirtyWorldPackages(Dirty); FEditorFileUtils::GetDirtyContentPackages(Dirty);
            TArray<TSharedPtr<FJsonValue>> DirtyJson; for (int32 I = 0; I < Dirty.Num() && I < 64; ++I) if (Dirty[I]) DirtyJson.Add(MakeShared<FJsonValueString>(Dirty[I]->GetName()));
            Data->SetArrayField(TEXT("dirty_packages"), DirtyJson);
            if (GCurrentLevelEditingViewportClient)
            {
                auto Cam = MakeShared<FJsonObject>(); Cam->SetObjectField(TEXT("location"), VectorJson(GCurrentLevelEditingViewportClient->GetViewLocation()));
                const FRotator R = GCurrentLevelEditingViewportClient->GetViewRotation(); auto RJ = MakeShared<FJsonObject>(); RJ->SetNumberField(TEXT("pitch"), R.Pitch); RJ->SetNumberField(TEXT("yaw"), R.Yaw); RJ->SetNumberField(TEXT("roll"), R.Roll);
                Cam->SetObjectField(TEXT("rotation"), RJ); Data->SetObjectField(TEXT("editor_camera"), Cam);
            }
            FSnapshot S; S.Id = TEXT("snap-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
            S.Principal = Context.PrincipalId; S.Session = Context.SessionId; S.WorldId = World->GetPathName();
            S.CapturedAt = FDateTime::UtcNow().ToIso8601(); S.ExpiresAt = FPlatformTime::Seconds() + Ttl; S.Data = Data;
            S.Hash = Sha1(JsonToString(Data));
            auto Out = Meta(S); Out->SetArrayField(TEXT("unresolved"), Unresolved); Out->SetBoolField(TEXT("truncated"), bTruncated);
            const FString Id = S.Id; Snapshots.Add(Id, MoveTemp(S));
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Snapshot of %d actor(s) in %s captured (%d unresolved target(s))."), ActorsJson.Num(), *World->GetMapName(), Unresolved.Num()), Out);
        });

    MCP_TOOL(Registry, "get_editor_snapshot")
        .Description(TEXT("Return an owned snapshot's structural content (actors, selection, dirty packages, camera) with its metadata."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("snapshot_id"), TEXT("Snapshot identifier"), true)
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Snapshots require the game thread"));
            Sweep(); const FSnapshot* S = FindOwned(Args->GetStringField(TEXT("snapshot_id")), Context);
            if (!S) return Error(EMCPError::NotFound, TEXT("Unknown, expired or foreign snapshot"));
            auto Out = Meta(*S); Out->SetObjectField(TEXT("data"), S->Data);
            return FMCPToolResult::SuccessStructured(TEXT("Owned snapshot."), Out);
        });

    MCP_TOOL(Registry, "compare_editor_snapshots")
        .Description(TEXT("Compare two owned snapshots of the same world: added and removed actors (by stable ref), and for shared actors changed transforms (with a stated tolerance), labels, classes, hidden flags, mobility, component lists and included properties; plus selection and dirty-package changes. Identical snapshots produce an empty diff. Structural by default; pass before_frame_id/after_frame_id (owned observer or retained capture frames of equal size) to add a deterministic pixel comparison (changed_pct, changed_bbox, 8x8 grid) as `visual`, or give only the frame ids for a visual-only result."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("before_id"), TEXT("Earlier snapshot (optional when only frames are compared)"))
        .StringArg(TEXT("after_id"), TEXT("Later snapshot (optional when only frames are compared)"))
        .NumberArg(TEXT("transform_tolerance"), TEXT("Absolute tolerance per component, 0..1000 (default 0.001)"))
        .StringArg(TEXT("before_frame_id"), TEXT("v5: earlier owned frame (observer or retained capture) for a pixel comparison"))
        .StringArg(TEXT("after_frame_id"), TEXT("v5: later owned frame; both frames must share size"))
        .IntArg(TEXT("pixel_threshold"), TEXT("v5: per-channel difference above which a pixel counts as changed, 0..255 (default 16)"))
        .OutputSchema(TEXT(R"({"type":"object","required":["before_id","after_id","identical","added","removed","changed","counts"],"properties":{"before_id":{"type":"string"},"after_id":{"type":"string"},"identical":{"type":"boolean"},"added":{"type":"array"},"removed":{"type":"array"},"changed":{"type":"array"},"selection_changed":{"type":"boolean"},"dirty_packages_added":{"type":"array"},"dirty_packages_removed":{"type":"array"},"counts":{"type":"object"},"transform_tolerance":{"type":"number"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return Error(EMCPError::RequiresGameThread, TEXT("Snapshots require the game thread"));
            Sweep();
            // v5 increment 23: visual regions ride on the same comparison tool instead of a second generic diff tool.
            TSharedPtr<FJsonObject> Visual;
            const bool bFrames = Args->HasField(TEXT("before_frame_id")) || Args->HasField(TEXT("after_frame_id"));
            if (bFrames)
            {
                if (!Args->HasField(TEXT("before_frame_id")) || !Args->HasField(TEXT("after_frame_id"))) return Error(EMCPError::OutOfRange, TEXT("before_frame_id and after_frame_id must be given together"));
                FString VErr;
                Visual = MCPEditorSurfaces::CompareFrames(Args->GetStringField(TEXT("before_frame_id")), Args->GetStringField(TEXT("after_frame_id")), Context, Args->HasField(TEXT("pixel_threshold")) ? (int32)Args->GetNumberField(TEXT("pixel_threshold")) : 16, VErr);
                if (!Visual.IsValid()) return Error(VErr.Contains(TEXT("Unknown")) ? EMCPError::NotFound : EMCPError::Unsupported, VErr);
            }
            const bool bSnapshots = Args->HasField(TEXT("before_id")) || Args->HasField(TEXT("after_id"));
            if (!bSnapshots && !bFrames) return Error(EMCPError::OutOfRange, TEXT("Provide before_id/after_id, before_frame_id/after_frame_id, or both"));
            if (!bSnapshots)
            {
                auto VOut = MakeShared<FJsonObject>(); VOut->SetObjectField(TEXT("visual"), Visual); VOut->SetStringField(TEXT("kind"), TEXT("visual_only"));
                return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Frames differ in %.2f%% of pixels%s."), Visual->GetNumberField(TEXT("changed_pct")), Visual->GetBoolField(TEXT("identical")) ? TEXT(" (identical)") : TEXT("")), VOut);
            }
            const FSnapshot* A = FindOwned(Args->GetStringField(TEXT("before_id")), Context); const FSnapshot* B = FindOwned(Args->GetStringField(TEXT("after_id")), Context);
            if (!A || !B) return Error(EMCPError::NotFound, TEXT("Unknown, expired or foreign snapshot"));
            if (A->WorldId != B->WorldId) return Error(EMCPError::Unsupported, TEXT("Snapshots belong to different worlds"));
            const double Tol = Args->HasField(TEXT("transform_tolerance")) ? Args->GetNumberField(TEXT("transform_tolerance")) : 0.001;
            if (Tol < 0 || Tol > 1000) return Error(EMCPError::OutOfRange, TEXT("transform_tolerance must be 0..1000"));
            TMap<FString, TSharedPtr<FJsonObject>> Before, After;
            for (const auto& V : A->Data->GetArrayField(TEXT("actors"))) Before.Add(V->AsObject()->GetStringField(TEXT("ref")), V->AsObject());
            for (const auto& V : B->Data->GetArrayField(TEXT("actors"))) After.Add(V->AsObject()->GetStringField(TEXT("ref")), V->AsObject());
            TArray<TSharedPtr<FJsonValue>> Added, Removed, Changed;
            for (const auto& P : After) if (!Before.Contains(P.Key)) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("ref"), P.Key); O->SetStringField(TEXT("label"), P.Value->GetStringField(TEXT("label"))); O->SetStringField(TEXT("class"), P.Value->GetStringField(TEXT("class"))); Added.Add(MakeShared<FJsonValueObject>(O)); }
            for (const auto& P : Before) if (!After.Contains(P.Key)) { auto O = MakeShared<FJsonObject>(); O->SetStringField(TEXT("ref"), P.Key); O->SetStringField(TEXT("label"), P.Value->GetStringField(TEXT("label"))); O->SetStringField(TEXT("class"), P.Value->GetStringField(TEXT("class"))); Removed.Add(MakeShared<FJsonValueObject>(O)); }
            for (const auto& P : Before)
            {
                const TSharedPtr<FJsonObject>* Bp = After.Find(P.Key); if (!Bp) continue;
                const auto& X = P.Value; const auto& Y = *Bp; TArray<TSharedPtr<FJsonValue>> Fields;
                auto Field = [&](const TCHAR* Name, const TSharedPtr<FJsonValue>& Old, const TSharedPtr<FJsonValue>& New) { auto F = MakeShared<FJsonObject>(); F->SetStringField(TEXT("field"), Name); F->SetField(TEXT("before"), Old); F->SetField(TEXT("after"), New); Fields.Add(MakeShared<FJsonValueObject>(F)); };
                if (!TransformsEqual(X->GetObjectField(TEXT("transform")), Y->GetObjectField(TEXT("transform")), Tol)) Field(TEXT("transform"), X->TryGetField(TEXT("transform")), Y->TryGetField(TEXT("transform")));
                for (const TCHAR* K : { TEXT("label"), TEXT("class"), TEXT("path"), TEXT("mobility") })
                    if (X->HasField(K) && Y->HasField(K) && X->GetStringField(K) != Y->GetStringField(K)) Field(K, X->TryGetField(K), Y->TryGetField(K));
                if (X->GetBoolField(TEXT("hidden")) != Y->GetBoolField(TEXT("hidden"))) Field(TEXT("hidden"), X->TryGetField(TEXT("hidden")), Y->TryGetField(TEXT("hidden")));
                if (X->HasField(TEXT("components")) && Y->HasField(TEXT("components")))
                {
                    TArray<FString> CX, CY; for (const auto& V : X->GetArrayField(TEXT("components"))) CX.Add(V->AsString()); for (const auto& V : Y->GetArrayField(TEXT("components"))) CY.Add(V->AsString());
                    if (CX != CY) Field(TEXT("components"), X->TryGetField(TEXT("components")), Y->TryGetField(TEXT("components")));
                }
                if (X->HasTypedField<EJson::Object>(TEXT("properties")) && Y->HasTypedField<EJson::Object>(TEXT("properties")))
                {
                    const auto PX = X->GetObjectField(TEXT("properties")), PY = Y->GetObjectField(TEXT("properties"));
                    for (const auto& PP : PX->Values)
                    {
                        const TSharedPtr<FJsonValue> Old = PP.Value, New = PY->TryGetField(PP.Key);
                        const bool bSame = New.IsValid() && ((Old->Type == EJson::Number && New->Type == EJson::Number) ? FMath::IsNearlyEqual(Old->AsNumber(), New->AsNumber(), 1.e-6) : FJsonValue::CompareEqual(*Old, *New));
                        if (!bSame) Field(*FString::Printf(TEXT("properties.%s"), *PP.Key), Old, New);
                    }
                }
                if (Fields.Num()) { auto C = MakeShared<FJsonObject>(); C->SetStringField(TEXT("ref"), P.Key); C->SetStringField(TEXT("label"), Y->GetStringField(TEXT("label"))); C->SetArrayField(TEXT("fields"), Fields); Changed.Add(MakeShared<FJsonValueObject>(C)); }
            }
            TSet<FString> SelA, SelB, DirtyA, DirtyB;
            for (const auto& V : A->Data->GetArrayField(TEXT("selection"))) SelA.Add(V->AsString()); for (const auto& V : B->Data->GetArrayField(TEXT("selection"))) SelB.Add(V->AsString());
            for (const auto& V : A->Data->GetArrayField(TEXT("dirty_packages"))) DirtyA.Add(V->AsString()); for (const auto& V : B->Data->GetArrayField(TEXT("dirty_packages"))) DirtyB.Add(V->AsString());
            TArray<TSharedPtr<FJsonValue>> DirtyAdded, DirtyRemoved;
            for (const FString& D : DirtyB) if (!DirtyA.Contains(D)) DirtyAdded.Add(MakeShared<FJsonValueString>(D));
            for (const FString& D : DirtyA) if (!DirtyB.Contains(D)) DirtyRemoved.Add(MakeShared<FJsonValueString>(D));
            const bool bSelectionChanged = !(SelA.Num() == SelB.Num() && SelA.Includes(SelB));
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("before_id"), A->Id); Out->SetStringField(TEXT("after_id"), B->Id);
            Out->SetArrayField(TEXT("added"), Added); Out->SetArrayField(TEXT("removed"), Removed); Out->SetArrayField(TEXT("changed"), Changed);
            Out->SetBoolField(TEXT("selection_changed"), bSelectionChanged); Out->SetArrayField(TEXT("dirty_packages_added"), DirtyAdded); Out->SetArrayField(TEXT("dirty_packages_removed"), DirtyRemoved);
            auto Counts = MakeShared<FJsonObject>(); Counts->SetNumberField(TEXT("added"), Added.Num()); Counts->SetNumberField(TEXT("removed"), Removed.Num()); Counts->SetNumberField(TEXT("changed"), Changed.Num());
            Out->SetObjectField(TEXT("counts"), Counts); Out->SetNumberField(TEXT("transform_tolerance"), Tol);
            if (Visual.IsValid()) Out->SetObjectField(TEXT("visual"), Visual);
            const bool bIdentical = Added.IsEmpty() && Removed.IsEmpty() && Changed.IsEmpty() && !bSelectionChanged && DirtyAdded.IsEmpty() && DirtyRemoved.IsEmpty();
            Out->SetBoolField(TEXT("identical"), bIdentical); Out->SetBoolField(TEXT("same_hash"), A->Hash == B->Hash);
            return FMCPToolResult::SuccessStructured(bIdentical ? TEXT("Snapshots are structurally identical.") : FString::Printf(TEXT("%d added, %d removed, %d changed actor(s)%s."), Added.Num(), Removed.Num(), Changed.Num(), bSelectionChanged ? TEXT("; selection changed") : TEXT("")), Out);
        });
}

void RegisterResources(FMCPResourceProvider& Provider)
{
    FMCPResourceDefinition Def; Def.Uri = TEXT("unreal://snapshots/{snapshot_id}"); Def.Name = TEXT("Editor snapshot");
    Def.Description = TEXT("Owned structural snapshot content (actors, selection, dirty packages, camera) with metadata; owner only."); Def.MimeType = TEXT("application/json"); Def.bTemplate = true;
    FMCPResourceReaderCtx Reader; Reader.BindLambda([](const FString& Uri, const FMCPRequestContext& Context)
    {
        Sweep();
        const FSnapshot* S = FindOwned(Uri.Mid(FString(TEXT("unreal://snapshots/")).Len()), Context);
        if (!S) return FMCPResourceContent::NotFound(Uri, TEXT("Unknown, expired or foreign snapshot"));
        auto J = Meta(*S); J->SetObjectField(TEXT("data"), S->Data);
        FMCPResourceContent C; C.Uri = Uri; C.MimeType = TEXT("application/json"); C.Text = JsonToString(J); return C;
    });
    Provider.RegisterResourceCtx(Def, Reader);
}

void ClearSession(const FString& SessionId)
{
    check(IsInGameThread());
    for (auto It = Snapshots.CreateIterator(); It; ++It) if (It.Value().Session == SessionId) It.RemoveCurrent();
}
void Reset() { check(IsInGameThread()); Snapshots.Empty(); }
TSharedPtr<FJsonObject> DiagnosticsJson()
{
    auto Out = MakeShared<FJsonObject>(); Out->SetNumberField(TEXT("snapshots_retained"), Snapshots.Num()); Out->SetNumberField(TEXT("snapshots_capacity"), MaxSnapshotsGlobal);
    return Out;
}
}
