// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 17: validate_animation_setup (V5-24 initial slice). Composes the existing checks
// into one non-mutating, structured report: skeleton agreement across assets, state machine
// completeness (animations, rules, reachability), montage validity and compile status.

#include "Tools/MCPAnimTools.h"
#include "Tools/Animation/AnimCommon.h"
#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_TransitionResult.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"

namespace MCPAnimTools::Validation
{
namespace
{
void Issue(TArray<TSharedPtr<FJsonValue>>& Issues, const FString& Rule, const FString& Severity, const FString& Asset, const FString& Where, const FString& Message)
{
    auto I = MakeShared<FJsonObject>();
    I->SetStringField(TEXT("rule"), Rule); I->SetStringField(TEXT("severity"), Severity); I->SetStringField(TEXT("asset"), Asset);
    if (!Where.IsEmpty()) I->SetStringField(TEXT("where"), Where);
    I->SetStringField(TEXT("message"), Message);
    Issues.Add(MakeShared<FJsonValueObject>(I));
}
UObject* LoadAny(const FString& Path)
{
    FString P = Path; if (!P.Contains(TEXT("."))) P = FString::Printf(TEXT("%s.%s"), *P, *FPackageName::GetShortName(P));
    return LoadObject<UObject>(nullptr, *P);
}
const USkeleton* SkeletonOf(UObject* Obj)
{
    if (const UAnimBlueprint* BP = Cast<UAnimBlueprint>(Obj)) return BP->TargetSkeleton;
    if (const UAnimationAsset* A = Cast<UAnimationAsset>(Obj)) return A->GetSkeleton();
    if (const USkeletalMesh* M = Cast<USkeletalMesh>(Obj)) return M->GetSkeleton();
    if (const USkeleton* S = Cast<USkeleton>(Obj)) return S;
    return nullptr;
}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    MCP_TOOL(Registry, "validate_animation_setup")
        .Description(TEXT("Validate a complete animation setup without modifying anything. Provide any of: anim_blueprint, skeletal_mesh, montages[], sequences[]. Reports skeleton_mismatch across all supplied assets (against the first skeleton found, or expected_skeleton), missing_asset paths, anim_blueprint_not_compiled, state machine issues per machine (state_without_animation, transition_without_rule for non-automatic transitions whose rule graph has no logic, unreachable_state from the entry state, no_entry_state, machine_without_states), montage issues (montage_no_slots, montage_no_sections, montage_zero_length, plus montage_validate findings) and sequence_zero_length. Issues carry rule, severity, asset, where and message; counts per rule; deterministic=true. Nothing is compiled or saved."))
        .ReadOnly().Idempotent()
        .StringArg(TEXT("anim_blueprint"), TEXT("AnimBlueprint asset path"))
        .StringArg(TEXT("skeletal_mesh"), TEXT("Skeletal mesh asset path expected to drive the setup"))
        .StringArg(TEXT("expected_skeleton"), TEXT("Skeleton asset path every asset must target (default: the first skeleton found)"))
        .StringArrayArg(TEXT("montages"), TEXT("AnimMontage asset paths (up to 32)"))
        .StringArrayArg(TEXT("sequences"), TEXT("AnimSequence asset paths (up to 64)"))
        .OutputSchema(TEXT(R"({"type":"object","required":["issues","counts","assets_checked","deterministic"],"properties":{"issues":{"type":"array"},"counts":{"type":"object"},"assets_checked":{"type":"integer"},"skeleton":{"type":"string"},"machines":{"type":"array"},"deterministic":{"type":"boolean"}}})"))
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Validation requires the game thread"));
            TArray<TSharedPtr<FJsonValue>> Issues, Machines; TMap<FString, int32> Counts; int32 Checked = 0;
            auto Count = [&](const FString& Rule) { Counts.FindOrAdd(Rule)++; };
            auto Add = [&](const FString& Rule, const FString& Sev, const FString& Asset, const FString& Where, const FString& Msg) { Issue(Issues, Rule, Sev, Asset, Where, Msg); Count(Rule); };
            struct FEntry { FString Path, Kind; UObject* Obj = nullptr; };
            TArray<FEntry> Entries;
            auto Collect = [&](const TCHAR* Field, const TCHAR* Kind, int32 Max)
            {
                if (!Args->HasField(Field)) return;
                if (Args->HasTypedField<EJson::String>(Field)) { if (!Args->GetStringField(Field).IsEmpty()) Entries.Add({ Args->GetStringField(Field), Kind }); return; }
                const auto Arr = Args->GetArrayField(Field);
                for (int32 I = 0; I < Arr.Num() && I < Max; ++I) if (Arr[I]->Type == EJson::String) Entries.Add({ Arr[I]->AsString(), Kind });
            };
            Collect(TEXT("anim_blueprint"), TEXT("anim_blueprint"), 1); Collect(TEXT("skeletal_mesh"), TEXT("skeletal_mesh"), 1);
            Collect(TEXT("montages"), TEXT("montage"), 32); Collect(TEXT("sequences"), TEXT("sequence"), 64);
            if (Entries.IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("Provide at least one of anim_blueprint, skeletal_mesh, montages, sequences"));
            const USkeleton* Expected = nullptr; FString ExpectedPath;
            if (Args->HasField(TEXT("expected_skeleton")))
            {
                Expected = Cast<USkeleton>(LoadAny(Args->GetStringField(TEXT("expected_skeleton"))));
                if (!Expected) Add(TEXT("missing_asset"), TEXT("error"), Args->GetStringField(TEXT("expected_skeleton")), FString(), TEXT("expected_skeleton could not be loaded"));
                else ExpectedPath = Expected->GetPathName();
            }
            for (FEntry& E : Entries)
            {
                E.Obj = LoadAny(E.Path);
                if (!E.Obj) { Add(TEXT("missing_asset"), TEXT("error"), E.Path, FString(), FString::Printf(TEXT("%s could not be loaded"), *E.Path)); continue; }
                ++Checked;
                const USkeleton* S = SkeletonOf(E.Obj);
                if (!S) { Add(TEXT("no_skeleton"), TEXT("error"), E.Obj->GetPathName(), FString(), TEXT("asset has no skeleton")); continue; }
                if (!Expected) { Expected = S; ExpectedPath = S->GetPathName(); }
                else if (S != Expected) Add(TEXT("skeleton_mismatch"), TEXT("error"), E.Obj->GetPathName(), FString(), FString::Printf(TEXT("targets %s, expected %s"), *S->GetPathName(), *ExpectedPath));
            }
            for (const FEntry& E : Entries)
            {
                if (!E.Obj) continue;
                if (UAnimBlueprint* BP = Cast<UAnimBlueprint>(E.Obj))
                {
                    const FString A = BP->GetPathName();
                    if (!BP->GeneratedClass) Add(TEXT("anim_blueprint_not_compiled"), TEXT("error"), A, FString(), TEXT("no generated class; compile_anim_blueprint first"));
                    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
                    int32 MachineCount = 0;
                    for (UEdGraph* G : Graphs)
                    {
                        UAnimationStateMachineGraph* SM = Cast<UAnimationStateMachineGraph>(G); if (!SM) continue;
                        ++MachineCount;
                        const FString Machine = SM->GetName();
                        auto MJ = MakeShared<FJsonObject>(); MJ->SetStringField(TEXT("machine"), Machine);
                        TArray<UAnimStateNode*> States; TArray<UAnimStateTransitionNode*> Transitions;
                        for (UEdGraphNode* N : SM->Nodes) { if (auto* St = Cast<UAnimStateNode>(N)) States.Add(St); else if (auto* Tr = Cast<UAnimStateTransitionNode>(N)) Transitions.Add(Tr); }
                        MJ->SetNumberField(TEXT("states"), States.Num()); MJ->SetNumberField(TEXT("transitions"), Transitions.Num());
                        if (States.IsEmpty()) Add(TEXT("machine_without_states"), TEXT("warning"), A, Machine, TEXT("state machine has no states"));
                        UAnimStateNodeBase* Entry = nullptr;
                        if (SM->EntryNode) for (UEdGraphPin* P : SM->EntryNode->Pins) if (P && P->Direction == EGPD_Output) for (UEdGraphPin* L : P->LinkedTo) if (L) Entry = Cast<UAnimStateNodeBase>(L->GetOwningNode());
                        if (!Entry && States.Num()) Add(TEXT("no_entry_state"), TEXT("error"), A, Machine, TEXT("entry node is not connected to a state"));
                        else if (Entry) MJ->SetStringField(TEXT("entry_state"), Entry->GetStateName());
                        TSet<UAnimStateNodeBase*> Reachable; TArray<UAnimStateNodeBase*> Queue; if (Entry) { Reachable.Add(Entry); Queue.Add(Entry); }
                        while (Queue.Num())
                        {
                            UAnimStateNodeBase* Cur = Queue.Pop(); TArray<UAnimStateTransitionNode*> Outs; Cur->GetTransitionList(Outs);
                            for (UAnimStateTransitionNode* T : Outs) if (T && T->GetPreviousState() == Cur) if (UAnimStateNodeBase* Next = T->GetNextState()) if (!Reachable.Contains(Next)) { Reachable.Add(Next); Queue.Add(Next); }
                        }
                        for (UAnimStateNode* St : States)
                        {
                            const FString Where = FString::Printf(TEXT("%s/%s"), *Machine, *St->GetStateName());
                            if (!Reachable.Contains(St)) Add(TEXT("unreachable_state"), TEXT("warning"), A, Where, Entry ? TEXT("state cannot be reached from the entry state") : TEXT("no entry state is connected, so no state can be reached"));
                            bool bHasAnim = false; int32 Players = 0;
                            if (St->BoundGraph) for (UEdGraphNode* N : St->BoundGraph->Nodes) if (auto* AG = Cast<UAnimGraphNode_Base>(N)) if (AG->GetAnimationAssetClass()) { ++Players; if (AG->GetAnimationAsset()) bHasAnim = true; }
                            if (!bHasAnim) Add(TEXT("state_without_animation"), TEXT("warning"), A, Where, Players ? TEXT("asset player has no animation assigned") : TEXT("state graph has no asset player"));
                        }
                        for (UAnimStateTransitionNode* T : Transitions)
                        {
                            const FString Where = FString::Printf(TEXT("%s/%s->%s"), *Machine, T->GetPreviousState() ? *T->GetPreviousState()->GetStateName() : TEXT("?"), T->GetNextState() ? *T->GetNextState()->GetStateName() : TEXT("?"));
                            if (T->bAutomaticRuleBasedOnSequencePlayerInState) continue;
                            bool bHasLogic = false;
                            if (T->BoundGraph) for (UEdGraphNode* N : T->BoundGraph->Nodes)
                                if (auto* R = Cast<UAnimGraphNode_TransitionResult>(N)) { for (UEdGraphPin* P : R->Pins) if (P && P->Direction == EGPD_Input && (P->LinkedTo.Num() || (!P->DefaultValue.IsEmpty() && P->DefaultValue != TEXT("false") && P->DefaultValue != TEXT("False")))) bHasLogic = true; }
                            if (!bHasLogic) Add(TEXT("transition_without_rule"), TEXT("warning"), A, Where, TEXT("transition can never fire: rule result is unconnected and false, and it is not automatic"));
                        }
                        Machines.Add(MakeShared<FJsonValueObject>(MJ));
                    }
                    if (MachineCount == 0) Add(TEXT("no_state_machine"), TEXT("info"), A, FString(), TEXT("anim blueprint has no state machine graph"));
                }
                else if (UAnimMontage* M = Cast<UAnimMontage>(E.Obj))
                {
                    const FString A = M->GetPathName();
                    if (M->SlotAnimTracks.IsEmpty()) Add(TEXT("montage_no_slots"), TEXT("error"), A, FString(), TEXT("montage has no slot tracks"));
                    if (M->CompositeSections.IsEmpty()) Add(TEXT("montage_no_sections"), TEXT("warning"), A, FString(), TEXT("montage has no sections"));
                    if (M->GetPlayLength() <= 0.f) Add(TEXT("montage_zero_length"), TEXT("error"), A, FString(), TEXT("montage has zero length"));
                    auto V = MakeShared<FJsonObject>(); V->SetStringField(TEXT("asset_path"), A);
                    const FMCPToolResult R = FMCPToolRegistry::Get().ExecuteTool(TEXT("montage_validate"), V, Context);
                    if (R.bIsError) Add(TEXT("montage_validate"), TEXT("error"), A, FString(), R.Content.Num() ? R.Content[0].Text.Left(300) : TEXT("montage_validate failed"));
                    else if (R.StructuredContent.IsValid())
                    {
                        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
                        for (const TCHAR* Key : { TEXT("errors"), TEXT("warnings"), TEXT("issues") })
                            if (R.StructuredContent->TryGetArrayField(Key, Arr)) for (const auto& X : *Arr) Add(TEXT("montage_validate"), FString(Key) == TEXT("errors") ? TEXT("error") : TEXT("warning"), A, FString(), X->Type == EJson::String ? X->AsString() : JsonToString(X->AsObject()));
                    }
                }
                else if (UAnimSequenceBase* Seq = Cast<UAnimSequenceBase>(E.Obj))
                {
                    if (Seq->GetPlayLength() <= 0.f) Add(TEXT("sequence_zero_length"), TEXT("error"), Seq->GetPathName(), FString(), TEXT("sequence has zero length"));
                }
            }
            auto Out = MakeShared<FJsonObject>();
            Out->SetArrayField(TEXT("issues"), Issues); auto CJ = MakeShared<FJsonObject>(); for (const auto& P : Counts) CJ->SetNumberField(P.Key, P.Value); Out->SetObjectField(TEXT("counts"), CJ);
            Out->SetNumberField(TEXT("assets_checked"), Checked); if (!ExpectedPath.IsEmpty()) Out->SetStringField(TEXT("skeleton"), ExpectedPath);
            Out->SetArrayField(TEXT("machines"), Machines); Out->SetBoolField(TEXT("deterministic"), true);
            int32 Errors = 0; for (const auto& V : Issues) if (V->AsObject()->GetStringField(TEXT("severity")) == TEXT("error")) ++Errors;
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Animation setup: %d asset(s) checked, %d issue(s), %d error(s). Not compiled."), Checked, Issues.Num(), Errors), Out);
        });
}
}
