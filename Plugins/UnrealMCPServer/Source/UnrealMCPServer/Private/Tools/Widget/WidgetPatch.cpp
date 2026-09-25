// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 11: typed, previewable widget patches (V5-23 core).

#include "Tools/Widget/WidgetCommon.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/Widget.h"
#include "Misc/SecureHash.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintOperationUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"

namespace MCPWidgetTools::Patch
{
using namespace MCPWidgetTools::Common;

namespace
{
constexpr int32 MaxOperations = 64;

/** Deterministic fingerprint of the hierarchy: name, class and parent of every widget. */
FString TreeRevision(UWidgetBlueprint* WBP)
{
    TArray<FString> Lines;
    WBP->WidgetTree->ForEachWidget([&](UWidget* W)
    {
        Lines.Add(FString::Printf(TEXT("%s|%s|%s"), *W->GetName(), *W->GetClass()->GetPathName(), W->GetParent() ? *W->GetParent()->GetName() : TEXT("")));
    });
    Lines.Sort();
    const FString Joined = FString::Join(Lines, TEXT("\n"));
    FTCHARToUTF8 Bytes(*Joined);
    return FSHA1::HashBuffer(Bytes.Get(), Bytes.Length()).ToString();
}

/** Simulated tree used to preflight a whole patch before anything is touched. */
struct FSim
{
    struct FNode { FString Parent; bool bPanel = false; bool bExisting = true; UClass* Class = nullptr; };
    TMap<FString, FNode> Nodes;
    FString Root;

    static FSim FromTree(UWidgetBlueprint* WBP)
    {
        FSim S;
        WBP->WidgetTree->ForEachWidget([&](UWidget* W)
        {
            FNode N; N.Parent = W->GetParent() ? W->GetParent()->GetName() : FString(); N.bPanel = W->IsA<UPanelWidget>(); N.Class = W->GetClass();
            S.Nodes.Add(W->GetName(), N);
        });
        if (WBP->WidgetTree->RootWidget) S.Root = WBP->WidgetTree->RootWidget->GetName();
        return S;
    }
    bool IsDescendant(const FString& Candidate, const FString& Ancestor) const
    {
        FString Cur = Candidate;
        for (int32 Guard = 0; Guard < 512 && !Cur.IsEmpty(); ++Guard)
        {
            const FNode* N = Nodes.Find(Cur); if (!N) return false;
            if (N->Parent == Ancestor) return true;
            Cur = N->Parent;
        }
        return false;
    }
    void RemoveSubtree(const FString& Name)
    {
        TArray<FString> Doomed; Doomed.Add(Name);
        for (const auto& Pair : Nodes) if (IsDescendant(Pair.Key, Name)) Doomed.Add(Pair.Key);
        for (const FString& D : Doomed) Nodes.Remove(D);
    }
};

bool ValidWidgetName(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > 128) return false;
    for (TCHAR C : Name) if (!(FChar::IsAlnum(C) || C == TEXT('_'))) return false;
    return !FChar::IsDigit(Name[0]);
}

struct FOp
{
    FString Kind, Name, NewName, Parent, Type, ClassPath, Property, Value;
    int32 Index = -1; bool bHasIndex = false; bool bIsVariable = false; bool bHasVariable = false;
    FString Note; // preflight notes such as deferred validation
};

FString ParseOps(const TArray<TSharedPtr<FJsonValue>>& Raw, TArray<FOp>& Out)
{
    if (Raw.Num() < 1 || Raw.Num() > MaxOperations) return FString::Printf(TEXT("operations must contain 1..%d entries"), MaxOperations);
    for (int32 I = 0; I < Raw.Num(); ++I)
    {
        const TSharedPtr<FJsonObject> O = Raw[I].IsValid() ? Raw[I]->AsObject() : nullptr;
        if (!O.IsValid()) return FString::Printf(TEXT("operations[%d] is not an object"), I);
        FOp Op;
        O->TryGetStringField(TEXT("op"), Op.Kind); O->TryGetStringField(TEXT("name"), Op.Name); O->TryGetStringField(TEXT("new_name"), Op.NewName);
        O->TryGetStringField(TEXT("parent"), Op.Parent); O->TryGetStringField(TEXT("type"), Op.Type); O->TryGetStringField(TEXT("class_path"), Op.ClassPath);
        O->TryGetStringField(TEXT("property"), Op.Property); O->TryGetStringField(TEXT("value"), Op.Value);
        if (O->HasTypedField<EJson::Number>(TEXT("index"))) { Op.bHasIndex = true; Op.Index = (int32)O->GetNumberField(TEXT("index")); }
        if (O->HasTypedField<EJson::Boolean>(TEXT("is_variable"))) { Op.bHasVariable = true; Op.bIsVariable = O->GetBoolField(TEXT("is_variable")); }
        static const TSet<FString> Kinds = { TEXT("add"), TEXT("remove"), TEXT("rename"), TEXT("reparent"), TEXT("set_property"), TEXT("set_slot_property") };
        if (!Kinds.Contains(Op.Kind)) return FString::Printf(TEXT("operations[%d].op must be add, remove, rename, reparent, set_property or set_slot_property"), I);
        if (Op.Name.IsEmpty()) return FString::Printf(TEXT("operations[%d].name is required"), I);
        Out.Add(Op);
    }
    return FString();
}

/** Validate the whole patch against a simulated tree. Returns problems with operation indices. */
TArray<FString> Preflight(UWidgetBlueprint* WBP, TArray<FOp>& Ops)
{
    TArray<FString> Problems;
    FSim Sim = FSim::FromTree(WBP);
    for (int32 I = 0; I < Ops.Num(); ++I)
    {
        FOp& Op = Ops[I];
        auto Problem = [&](const FString& Msg) { Problems.Add(FString::Printf(TEXT("[%d] %s: %s"), I, *Op.Kind, *Msg)); };
        const FSim::FNode* Node = Sim.Nodes.Find(Op.Name);
        if (Op.Kind == TEXT("add"))
        {
            if (!ValidWidgetName(Op.Name)) { Problem(TEXT("name must be an identifier")); continue; }
            if (Node) { Problem(FString::Printf(TEXT("widget '%s' already exists"), *Op.Name)); continue; }
            UClass* Class = nullptr; bool bPanel = false;
            if (!Op.ClassPath.IsEmpty()) { FString Err; Class = ResolveUserWidgetClass(Op.ClassPath, Err); if (!Class) { Problem(Err.IsEmpty() ? TEXT("class_path not resolvable") : Err); continue; } }
            else { Class = WidgetClassFromTypeName(Op.Type); if (!Class) { Problem(FString::Printf(TEXT("unknown widget type '%s'"), *Op.Type)); continue; } bPanel = Class->IsChildOf(UPanelWidget::StaticClass()); }
            const FString ParentName = Op.Parent.IsEmpty() ? Sim.Root : Op.Parent;
            const FSim::FNode* P = Sim.Nodes.Find(ParentName);
            if (ParentName.IsEmpty()) { if (Sim.Nodes.Num() > 0) { Problem(TEXT("tree has widgets but no root")); continue; } }
            else if (!P || !P->bPanel) { Problem(FString::Printf(TEXT("parent '%s' not found or not a panel"), *ParentName)); continue; }
            FSim::FNode N; N.Parent = ParentName; N.bPanel = bPanel; N.bExisting = false; N.Class = Class; Sim.Nodes.Add(Op.Name, N);
            if (ParentName.IsEmpty()) Sim.Root = Op.Name;
            continue;
        }
        if (!Node) { Problem(FString::Printf(TEXT("widget '%s' not found"), *Op.Name)); continue; }
        if (Op.Kind == TEXT("remove"))
        {
            if (Op.Name == Sim.Root) { Problem(TEXT("the root widget cannot be removed by a patch")); continue; }
            Sim.RemoveSubtree(Op.Name); continue;
        }
        if (Op.Kind == TEXT("rename"))
        {
            if (!ValidWidgetName(Op.NewName)) { Problem(TEXT("new_name must be an identifier")); continue; }
            if (Sim.Nodes.Contains(Op.NewName)) { Problem(FString::Printf(TEXT("widget '%s' already exists"), *Op.NewName)); continue; }
            if (Node->bExisting)
            {
                FText VerifyErr;
                if (UWidget* Existing = FindWidgetByName(WBP, Op.Name))
                    if (!FWidgetBlueprintOperationUtils::VerifyWidgetRename(WBP, Existing, FText::FromString(Op.NewName), VerifyErr)) { Problem(VerifyErr.ToString()); continue; }
            }
            FSim::FNode Copy = *Node; Sim.Nodes.Remove(Op.Name); Sim.Nodes.Add(Op.NewName, Copy);
            for (auto& Pair : Sim.Nodes) if (Pair.Value.Parent == Op.Name) Pair.Value.Parent = Op.NewName;
            if (Sim.Root == Op.Name) Sim.Root = Op.NewName;
            continue;
        }
        if (Op.Kind == TEXT("reparent"))
        {
            const FSim::FNode* P = Sim.Nodes.Find(Op.Parent);
            if (Op.Parent.IsEmpty() || !P || !P->bPanel) { Problem(FString::Printf(TEXT("parent '%s' not found or not a panel"), *Op.Parent)); continue; }
            if (Op.Parent == Op.Name || Sim.IsDescendant(Op.Parent, Op.Name)) { Problem(TEXT("cannot reparent a widget under itself")); continue; }
            if (Op.Name == Sim.Root) { Problem(TEXT("the root widget cannot be reparented")); continue; }
            Sim.Nodes[Op.Name].Parent = Op.Parent; continue;
        }
        if (Op.Kind == TEXT("set_property") || Op.Kind == TEXT("set_slot_property"))
        {
            if (Op.Property.IsEmpty()) { Problem(TEXT("property is required")); continue; }
            if (Op.Kind == TEXT("set_property"))
            {
                UObject* Probe = Node->bExisting ? (UObject*)FindWidgetByName(WBP, Op.Name) : (Node->Class ? Node->Class->GetDefaultObject() : nullptr);
                FResolvedProperty R; FString Err;
                if (!Probe || !ResolvePropertyPath(Probe, Op.Property, R, Err)) { Problem(FString::Printf(TEXT("property '%s' not found on '%s'%s"), *Op.Property, *Op.Name, Err.IsEmpty() ? TEXT("") : *(TEXT(": ") + Err))); continue; }
            }
            else
            {
                UWidget* Existing = Node->bExisting ? FindWidgetByName(WBP, Op.Name) : nullptr;
                if (Existing && Existing->Slot)
                {
                    FResolvedProperty R; FString Err;
                    if (!ResolvePropertyPath(Existing->Slot, Op.Property, R, Err)) { Problem(FString::Printf(TEXT("slot property '%s' not found on '%s'"), *Op.Property, *Op.Name)); continue; }
                }
                else Op.Note = TEXT("slot property validated at apply time (widget or slot created by this patch)");
            }
        }
    }
    return Problems;
}

TSharedPtr<FJsonObject> OpJson(const FOp& Op, int32 Index)
{
    auto O = MakeShared<FJsonObject>();
    O->SetNumberField(TEXT("index"), Index); O->SetStringField(TEXT("op"), Op.Kind); O->SetStringField(TEXT("name"), Op.Name);
    if (!Op.NewName.IsEmpty()) O->SetStringField(TEXT("new_name"), Op.NewName);
    if (!Op.Parent.IsEmpty()) O->SetStringField(TEXT("parent"), Op.Parent);
    if (!Op.Type.IsEmpty()) O->SetStringField(TEXT("type"), Op.Type);
    if (!Op.ClassPath.IsEmpty()) O->SetStringField(TEXT("class_path"), Op.ClassPath);
    if (!Op.Property.IsEmpty()) O->SetStringField(TEXT("property"), Op.Property);
    if (!Op.Value.IsEmpty()) O->SetStringField(TEXT("value"), Op.Value);
    if (!Op.Note.IsEmpty()) O->SetStringField(TEXT("note"), Op.Note);
    return O;
}

FMCPToolResult Run(const TSharedPtr<FJsonObject>& Args, bool bPreview)
{
    if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Widget patches require the game thread"));
    const FString AssetPath = Args->GetStringField(TEXT("asset_path"));
    UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
    if (!WBP || !WBP->WidgetTree) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));
    TArray<FOp> Ops;
    const FString ParseError = ParseOps(Args->GetArrayField(TEXT("operations")), Ops);
    if (!ParseError.IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, ParseError);
    const FString RevisionBefore = TreeRevision(WBP);
    FString Expected;
    if (Args->TryGetStringField(TEXT("expected_revision"), Expected) && !Expected.IsEmpty() && Expected != RevisionBefore)
        return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Widget tree changed since the expected revision; preview again before applying"),
            FString::Printf(TEXT("current revision %s"), *RevisionBefore));
    const TArray<FString> Problems = Preflight(WBP, Ops);
    const FString SavePolicy = Args->HasField(TEXT("save_policy")) ? Args->GetStringField(TEXT("save_policy")) : TEXT("leave_dirty");

    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    Out->SetStringField(TEXT("mode"), bPreview ? TEXT("preview") : TEXT("apply"));
    Out->SetStringField(TEXT("revision_before"), RevisionBefore);
    Out->SetStringField(TEXT("save_policy"), SavePolicy);
    TArray<TSharedPtr<FJsonValue>> Planned; for (int32 I = 0; I < Ops.Num(); ++I) Planned.Add(MakeShared<FJsonValueObject>(OpJson(Ops[I], I)));
    Out->SetArrayField(TEXT("operations"), Planned);
    TArray<TSharedPtr<FJsonValue>> ProblemsJson; for (const FString& P : Problems) ProblemsJson.Add(MakeShared<FJsonValueString>(P));
    Out->SetArrayField(TEXT("problems"), ProblemsJson);
    Out->SetBoolField(TEXT("applicable"), Problems.IsEmpty());
    Out->SetStringField(TEXT("required_scope"), TEXT("scene"));
    Out->SetBoolField(TEXT("writes_files"), !bPreview && SavePolicy == TEXT("save"));
    if (bPreview)
    {
        Out->SetBoolField(TEXT("applied"), false);
        FMCPToolResult R = FMCPToolResult::SuccessStructured(Problems.IsEmpty()
            ? FString::Printf(TEXT("Patch of %d operation(s) is applicable at revision %s; nothing was changed."), Ops.Num(), *RevisionBefore.Left(12))
            : FString::Printf(TEXT("Patch refused in preflight with %d problem(s); nothing was changed."), Problems.Num()), Out);
        R.bIsError = !Problems.IsEmpty();
        return R;
    }
    if (!Problems.IsEmpty())
    {
        Out->SetBoolField(TEXT("applied"), false);
        FMCPToolResult R = FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Patch refused before any change; %d problem(s)."), Problems.Num()), Out);
        R.bIsError = true; return R;
    }

    // Apply. The registry already holds the undo step for this call; each operation is a
    // standard editor edit. A mid-patch failure is reported with the surviving operations.
    WBP->Modify();
    TArray<TSharedPtr<FJsonValue>> Applied; FString Failure; int32 FailedIndex = -1;
    for (int32 I = 0; I < Ops.Num() && Failure.IsEmpty(); ++I)
    {
        const FOp& Op = Ops[I];
        auto Rec = OpJson(Op, I);
        if (Op.Kind == TEXT("add"))
        {
            UWidget* Parent = Op.Parent.IsEmpty() ? WBP->WidgetTree->RootWidget.Get() : FindWidgetByName(WBP, Op.Parent);
            FString Err; UWidget* NewWidget = ConstructWidgetForTree(WBP, Op.Type, Op.ClassPath, Op.Name, Err);
            if (!NewWidget) { Failure = Err; FailedIndex = I; break; }
            if (Op.bHasVariable) NewWidget->bIsVariable = Op.bIsVariable;
            FText AddErr;
            if (!FWidgetBlueprintOperationUtils::AddWidget(WBP, NewWidget, Parent, Op.bHasIndex ? Op.Index : -1, AddErr)) { Failure = AddErr.ToString(); FailedIndex = I; break; }
            Rec->SetBoolField(TEXT("verified"), FindWidgetByName(WBP, Op.Name) == NewWidget && (!Parent || NewWidget->GetParent() == Parent));
        }
        else if (Op.Kind == TEXT("remove"))
        {
            UWidget* W = FindWidgetByName(WBP, Op.Name);
            if (!W) { Failure = TEXT("widget vanished before removal"); FailedIndex = I; break; }
            W->Modify(); if (W->GetParent()) W->GetParent()->Modify();
            FText RemoveErr; // Engine path: also retires the widget variable GUID and references.
            if (!FWidgetBlueprintOperationUtils::RemoveWidget(WBP, W, RemoveErr)) { Failure = RemoveErr.IsEmpty() ? TEXT("engine refused the removal") : RemoveErr.ToString(); FailedIndex = I; break; }
            Rec->SetBoolField(TEXT("verified"), FindWidgetByName(WBP, Op.Name) == nullptr);
        }
        else if (Op.Kind == TEXT("rename"))
        {
            UWidget* W = FindWidgetByName(WBP, Op.Name);
            if (!W) { Failure = TEXT("widget vanished before rename"); FailedIndex = I; break; }
            W->Modify();
            FText VerifyErr; // Engine path: retargets the variable GUID, graph nodes, bindings and animations.
            if (!FWidgetBlueprintOperationUtils::VerifyWidgetRename(WBP, W, FText::FromString(Op.NewName), VerifyErr) || !FWidgetBlueprintOperationUtils::RenameWidget(WBP, W, Op.NewName))
            { Failure = VerifyErr.IsEmpty() ? TEXT("engine refused the rename") : VerifyErr.ToString(); FailedIndex = I; break; }
            Rec->SetBoolField(TEXT("verified"), FindWidgetByName(WBP, Op.NewName) == W && FindWidgetByName(WBP, Op.Name) == nullptr);
        }
        else if (Op.Kind == TEXT("reparent"))
        {
            UWidget* W = FindWidgetByName(WBP, Op.Name); UPanelWidget* P = Cast<UPanelWidget>(FindWidgetByName(WBP, Op.Parent));
            if (!W || !P) { Failure = TEXT("widget or parent vanished before reparent"); FailedIndex = I; break; }
            W->Modify(); P->Modify(); if (W->GetParent()) W->GetParent()->Modify();
            if (W->GetParent()) W->RemoveFromParent();
            UPanelSlot* NewSlot = Op.bHasIndex && Op.Index >= 0 && Op.Index <= P->GetChildrenCount() ? P->InsertChildAt(Op.Index, W) : P->AddChild(W);
            if (!NewSlot) { Failure = TEXT("parent refused the child"); FailedIndex = I; break; }
            Rec->SetBoolField(TEXT("verified"), W->GetParent() == P);
        }
        else
        {
            UWidget* W = FindWidgetByName(WBP, Op.Name);
            if (!W) { Failure = TEXT("widget vanished before property edit"); FailedIndex = I; break; }
            UObject* Target = Op.Kind == TEXT("set_property") ? (UObject*)W : (UObject*)W->Slot;
            if (!Target) { Failure = FString::Printf(TEXT("'%s' has no slot"), *Op.Name); FailedIndex = I; break; }
            Target->Modify();
            FString Err;
            if (!SetPropertyFromString(Target, Op.Property, Op.Value, Err)) { Failure = Err; FailedIndex = I; break; }
            FString Observed; FString ReadErr; FResolvedProperty Resolved;
            if (ResolvePropertyPath(Target, Op.Property, Resolved, ReadErr))
            {
                // FText exports as an empty string through ExportText for plain runtime text; read the display string directly.
                if (const FTextProperty* TextProp = CastField<FTextProperty>(Resolved.Property))
                    Observed = TextProp->GetPropertyValue(TextProp->ContainerPtrToValuePtr<void>(Resolved.Container)).ToString();
                else GetPropertyAsString(Target, Op.Property, Observed, ReadErr);
                Rec->SetStringField(TEXT("observed"), Observed);
            }
            Rec->SetBoolField(TEXT("verified"), ReadErr.IsEmpty());
        }
        Applied.Add(MakeShared<FJsonValueObject>(Rec));
    }
    WBP->WidgetTree->Modify();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
    const FString RevisionAfter = TreeRevision(WBP);
    Out->SetArrayField(TEXT("results"), Applied);
    Out->SetStringField(TEXT("revision_after"), RevisionAfter);
    Out->SetBoolField(TEXT("applied"), Failure.IsEmpty());
    Out->SetBoolField(TEXT("atomic"), Failure.IsEmpty() || Applied.IsEmpty());
    Out->SetStringField(TEXT("recovery"), Failure.IsEmpty() ? TEXT("editor_undo_available") : TEXT("not_attempted"));
    if (!Failure.IsEmpty())
    {
        Out->SetStringField(TEXT("failed_operation"), FString::Printf(TEXT("[%d] %s"), FailedIndex, *Failure));
        FMCPToolResult R = FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Patch failed at operation %d after %d applied: %s. Surviving effects are listed; use editor Undo to revert this call."), FailedIndex, Applied.Num(), *Failure), Out);
        R.bIsError = true; return R;
    }
    if (SavePolicy == TEXT("save")) { SaveWidgetBlueprint(WBP, true); Out->SetBoolField(TEXT("saved"), true); }
    else Out->SetBoolField(TEXT("saved"), false);
    return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Applied %d operation(s); revision %s -> %s%s."), Applied.Num(), *RevisionBefore.Left(12), *RevisionAfter.Left(12), SavePolicy == TEXT("save") ? TEXT(", saved") : TEXT(", left dirty")), Out);
}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    auto OpSchema = StringToJson(TEXT(R"({"type":"object","required":["op","name"],"properties":{"op":{"type":"string","enum":["add","remove","rename","reparent","set_property","set_slot_property"]},"name":{"type":"string"},"new_name":{"type":"string"},"parent":{"type":"string"},"type":{"type":"string"},"class_path":{"type":"string"},"property":{"type":"string"},"value":{"type":"string"},"index":{"type":"integer","minimum":0,"maximum":4096},"is_variable":{"type":"boolean"}}})"));
    auto OpsSchema = MakeShared<FJsonObject>();
    OpsSchema->SetStringField(TEXT("type"), TEXT("array")); OpsSchema->SetObjectField(TEXT("items"), OpSchema);
    OpsSchema->SetNumberField(TEXT("minItems"), 1); OpsSchema->SetNumberField(TEXT("maxItems"), MaxOperations);
    MCP_TOOL(Registry, "apply_widget_patch")
        .Description(TEXT("Typed widget patch with preflight. operations[] are add, remove, rename, reparent, set_property and set_slot_property (values in Unreal text syntax). mode=preview (default, or dry_run=true) simulates the whole patch against the current tree, resolves names, parents and property paths, and returns applicability, problems and the tree revision without changing anything. mode=apply re-runs the preflight, refuses if expected_revision no longer matches, applies in order under the registry undo step, reads each change back, and reports revision_after; a mid-patch failure lists surviving operations in results (atomic=false) and recovery=not_attempted. save_policy defaults to leave_dirty. Requires Scene scope; compile separately with compile_widget_blueprint."))
        .RequiresPieOff()
        .StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
        .EnumArg(TEXT("mode"), TEXT("preview (default) or apply"), { TEXT("preview"), TEXT("apply") })
        .StringArg(TEXT("expected_revision"), TEXT("Tree revision returned by a previous preview; apply refuses on mismatch"))
        .EnumArg(TEXT("save_policy"), TEXT("leave_dirty (default) or save"), { TEXT("leave_dirty"), TEXT("save") })
        .ObjectArg(TEXT("operations"), TEXT("Ordered typed operations (1..64)"), OpsSchema, true)
        .Example(TEXT(R"({"asset_path":"/Game/UI/WBP_HUD","mode":"preview","operations":[{"op":"add","type":"TextBlock","name":"Txt_Score","parent":"RootPanel","is_variable":true},{"op":"set_property","name":"Txt_Score","property":"Text","value":"SCORE 0"}]})"))
        .OutputSchema(TEXT(R"({"type":"object","required":["asset_path","mode","revision_before","operations","problems","applicable","applied"],"properties":{"asset_path":{"type":"string"},"mode":{"type":"string"},"revision_before":{"type":"string"},"revision_after":{"type":"string"},"operations":{"type":"array"},"problems":{"type":"array"},"applicable":{"type":"boolean"},"applied":{"type":"boolean"},"results":{"type":"array"},"atomic":{"type":"boolean"},"recovery":{"type":"string"},"failed_operation":{"type":"string"},"saved":{"type":"boolean"},"save_policy":{"type":"string"},"required_scope":{"type":"string"},"writes_files":{"type":"boolean"}}})"))
        .Preview([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&) { return Run(Args, true); })
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext&)
        {
            const FString Mode = Args->HasField(TEXT("mode")) ? Args->GetStringField(TEXT("mode")) : TEXT("preview");
            return Run(Args, Mode != TEXT("apply"));
        });
}
}
