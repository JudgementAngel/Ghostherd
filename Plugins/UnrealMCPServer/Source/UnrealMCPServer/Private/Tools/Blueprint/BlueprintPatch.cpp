// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 13: typed, previewable Blueprint graph patches (V5-22 core). The patch is an
// orchestrator: it preflights every operation against the live graph, then applies through the
// existing, individually tested node tools via nested dispatch, and reads the result back with
// validate_blueprint's revision fingerprint.

#include "Tools/Blueprint/BlueprintCommon.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Internationalization/Regex.h"

namespace MCPBlueprintTools::Patch
{
using namespace MCPBlueprintTools::Common;

namespace
{
constexpr int32 MaxOperations = 64;

struct FOp
{
    FString Kind, Alias, Node, Pin, ToNode, ToPin, Function, TargetClass, Variable, Event, Name, Value, Note;
    double X = 0, Y = 0; bool bHasPos = false;
};

FString ParseOps(const TArray<TSharedPtr<FJsonValue>>& Raw, TArray<FOp>& Out)
{
    if (Raw.Num() < 1 || Raw.Num() > MaxOperations) return FString::Printf(TEXT("operations must contain 1..%d entries"), MaxOperations);
    static const TSet<FString> Kinds = { TEXT("add_call_function"), TEXT("add_variable_get"), TEXT("add_variable_set"), TEXT("add_event"), TEXT("add_custom_event"), TEXT("add_branch"),
        TEXT("remove_node"), TEXT("connect"), TEXT("disconnect"), TEXT("set_default"), TEXT("move") };
    for (int32 I = 0; I < Raw.Num(); ++I)
    {
        const TSharedPtr<FJsonObject> O = Raw[I].IsValid() ? Raw[I]->AsObject() : nullptr;
        if (!O.IsValid()) return FString::Printf(TEXT("operations[%d] is not an object"), I);
        FOp Op;
        O->TryGetStringField(TEXT("op"), Op.Kind); O->TryGetStringField(TEXT("alias"), Op.Alias); O->TryGetStringField(TEXT("node"), Op.Node);
        O->TryGetStringField(TEXT("pin"), Op.Pin); O->TryGetStringField(TEXT("to_node"), Op.ToNode); O->TryGetStringField(TEXT("to_pin"), Op.ToPin);
        O->TryGetStringField(TEXT("function"), Op.Function); O->TryGetStringField(TEXT("target_class"), Op.TargetClass); O->TryGetStringField(TEXT("variable"), Op.Variable);
        O->TryGetStringField(TEXT("event"), Op.Event); O->TryGetStringField(TEXT("name"), Op.Name); O->TryGetStringField(TEXT("value"), Op.Value);
        if (O->HasTypedField<EJson::Number>(TEXT("x")) || O->HasTypedField<EJson::Number>(TEXT("y"))) { Op.bHasPos = true; Op.X = O->HasField(TEXT("x")) ? O->GetNumberField(TEXT("x")) : 0; Op.Y = O->HasField(TEXT("y")) ? O->GetNumberField(TEXT("y")) : 0; }
        if (!Kinds.Contains(Op.Kind)) return FString::Printf(TEXT("operations[%d].op is not a supported operation"), I);
        if (Op.Kind.StartsWith(TEXT("add_")) && Op.Alias.IsEmpty()) return FString::Printf(TEXT("operations[%d]: add operations need an alias for later references"), I);
        Out.Add(Op);
    }
    return FString();
}

bool IsGuid(const FString& S) { FGuid G; return FGuid::Parse(S, G); }

/** Preflight against the live graph plus aliases introduced earlier in the patch. */
TArray<FString> Preflight(UBlueprint* BP, UEdGraph* Graph, TArray<FOp>& Ops)
{
    TArray<FString> Problems; TSet<FString> Aliases; TSet<FString> Removed;
    const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
    auto Ref = [&](const FString& R, int32 I, const TCHAR* Field, UEdGraphNode*& OutNode) -> bool
    {
        OutNode = nullptr;
        if (R.StartsWith(TEXT("$"))) { if (!Aliases.Contains(R.Mid(1))) { Problems.Add(FString::Printf(TEXT("[%d] %s: alias '%s' not defined earlier in the patch"), I, Field, *R)); return false; } return true; }
        if (!IsGuid(R)) { Problems.Add(FString::Printf(TEXT("[%d] %s: '%s' is neither a node GUID nor a $alias"), I, Field, *R)); return false; }
        if (Removed.Contains(R)) { Problems.Add(FString::Printf(TEXT("[%d] %s: node '%s' is removed earlier in the patch"), I, Field, *R)); return false; }
        OutNode = FindNodeByGuid(Graph, R);
        if (!OutNode) { Problems.Add(FString::Printf(TEXT("[%d] %s: node '%s' not found in graph"), I, Field, *R)); return false; }
        return true;
    };
    for (int32 I = 0; I < Ops.Num(); ++I)
    {
        FOp& Op = Ops[I];
        if (Op.Kind.StartsWith(TEXT("add_")))
        {
            if (Aliases.Contains(Op.Alias)) { Problems.Add(FString::Printf(TEXT("[%d] alias '%s' already used"), I, *Op.Alias)); continue; }
            if (Op.Kind == TEXT("add_call_function"))
            {
                UClass* Class = Op.TargetClass.IsEmpty() ? nullptr : FindClassByName(Op.TargetClass);
                if (!Class) { Problems.Add(FString::Printf(TEXT("[%d] add_call_function: target_class '%s' not found"), I, *Op.TargetClass)); continue; }
                if (!Class->FindFunctionByName(FName(*Op.Function))) { Problems.Add(FString::Printf(TEXT("[%d] add_call_function: function '%s' not found on %s"), I, *Op.Function, *Class->GetName())); continue; }
            }
            else if (Op.Kind == TEXT("add_variable_get") || Op.Kind == TEXT("add_variable_set"))
            {
                bool bFound = false; for (const FBPVariableDescription& V : BP->NewVariables) if (V.VarName == FName(*Op.Variable)) bFound = true;
                if (!bFound && !FindFProperty<FProperty>(BP->ParentClass, FName(*Op.Variable))) { Problems.Add(FString::Printf(TEXT("[%d] %s: variable '%s' not found"), I, *Op.Kind, *Op.Variable)); continue; }
            }
            else if (Op.Kind == TEXT("add_event") && Op.Event.IsEmpty()) { Problems.Add(FString::Printf(TEXT("[%d] add_event: event is required"), I)); continue; }
            else if (Op.Kind == TEXT("add_custom_event") && Op.Name.IsEmpty()) { Problems.Add(FString::Printf(TEXT("[%d] add_custom_event: name is required"), I)); continue; }
            Aliases.Add(Op.Alias); continue;
        }
        UEdGraphNode* A = nullptr; UEdGraphNode* B = nullptr;
        if (Op.Kind == TEXT("remove_node")) { if (Ref(Op.Node, I, TEXT("node"), A) && !Op.Node.StartsWith(TEXT("$"))) Removed.Add(Op.Node); continue; }
        if (Op.Kind == TEXT("move")) { Ref(Op.Node, I, TEXT("node"), A); continue; }
        if (Op.Kind == TEXT("disconnect") || Op.Kind == TEXT("set_default"))
        {
            if (!Ref(Op.Node, I, TEXT("node"), A)) continue;
            if (Op.Pin.IsEmpty()) { Problems.Add(FString::Printf(TEXT("[%d] %s: pin is required"), I, *Op.Kind)); continue; }
            if (A && !A->FindPin(FName(*Op.Pin))) Problems.Add(FString::Printf(TEXT("[%d] %s: pin '%s' not found on node"), I, *Op.Kind, *Op.Pin));
            if (!A) Op.Note = TEXT("pin validated at apply time (node created by this patch)");
            continue;
        }
        if (Op.Kind == TEXT("connect"))
        {
            const bool bA = Ref(Op.Node, I, TEXT("node"), A), bB = Ref(Op.ToNode, I, TEXT("to_node"), B);
            if (!bA || !bB) continue;
            if (Op.Pin.IsEmpty() || Op.ToPin.IsEmpty()) { Problems.Add(FString::Printf(TEXT("[%d] connect: pin and to_pin are required"), I)); continue; }
            if (A && B)
            {
                UEdGraphPin* PA = A->FindPin(FName(*Op.Pin)); UEdGraphPin* PB = B->FindPin(FName(*Op.ToPin));
                if (!PA || !PB) { Problems.Add(FString::Printf(TEXT("[%d] connect: pin '%s' or '%s' not found"), I, *Op.Pin, *Op.ToPin)); continue; }
                const FPinConnectionResponse R = K2->CanCreateConnection(PA, PB);
                if (R.Response == CONNECT_RESPONSE_DISALLOW) { Problems.Add(FString::Printf(TEXT("[%d] connect: schema refuses %s.%s -> %s.%s: %s"), I, *A->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Op.Pin, *B->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Op.ToPin, *R.Message.ToString())); continue; }
                if (R.Response == CONNECT_RESPONSE_BREAK_OTHERS_A || R.Response == CONNECT_RESPONSE_BREAK_OTHERS_B || R.Response == CONNECT_RESPONSE_BREAK_OTHERS_AB) Op.Note = TEXT("existing links on one side will be replaced");
            }
            else Op.Note = TEXT("schema check at apply time (node created by this patch)");
        }
    }
    return Problems;
}

TSharedPtr<FJsonObject> OpJson(const FOp& Op, int32 Index)
{
    auto O = MakeShared<FJsonObject>(); O->SetNumberField(TEXT("index"), Index); O->SetStringField(TEXT("op"), Op.Kind);
    for (const auto& P : { TPair<const TCHAR*, const FString*>(TEXT("alias"), &Op.Alias), TPair<const TCHAR*, const FString*>(TEXT("node"), &Op.Node), TPair<const TCHAR*, const FString*>(TEXT("pin"), &Op.Pin),
        TPair<const TCHAR*, const FString*>(TEXT("to_node"), &Op.ToNode), TPair<const TCHAR*, const FString*>(TEXT("to_pin"), &Op.ToPin), TPair<const TCHAR*, const FString*>(TEXT("function"), &Op.Function),
        TPair<const TCHAR*, const FString*>(TEXT("target_class"), &Op.TargetClass), TPair<const TCHAR*, const FString*>(TEXT("variable"), &Op.Variable), TPair<const TCHAR*, const FString*>(TEXT("event"), &Op.Event),
        TPair<const TCHAR*, const FString*>(TEXT("name"), &Op.Name), TPair<const TCHAR*, const FString*>(TEXT("value"), &Op.Value), TPair<const TCHAR*, const FString*>(TEXT("note"), &Op.Note) })
        if (!P.Value->IsEmpty()) O->SetStringField(P.Key, *P.Value);
    return O;
}

FString ExtractNodeId(const FMCPToolResult& R)
{
    if (R.StructuredContent.IsValid())
    {
        FString Id;
        if (R.StructuredContent->TryGetStringField(TEXT("nodeId"), Id) || R.StructuredContent->TryGetStringField(TEXT("node_id"), Id)) return Id;
    }
    for (const FMCPContentBlock& B : R.Content)
    {
        if (B.Type != TEXT("text")) continue;
        FRegexMatcher M(FRegexPattern(TEXT("[0-9A-Fa-f]{32}|[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}")), B.Text);
        if (M.FindNext()) return M.GetCaptureGroup(0);
    }
    return FString();
}

FString Revision(const FString& AssetPath, const FMCPRequestContext& Context)
{
    auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("asset_path"), AssetPath);
    const FMCPToolResult R = FMCPToolRegistry::Get().ExecuteTool(TEXT("validate_blueprint"), A, Context);
    return !R.bIsError && R.StructuredContent.IsValid() ? R.StructuredContent->GetStringField(TEXT("revision")) : FString();
}

FMCPToolResult Run(const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context, bool bPreview)
{
    if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Blueprint patches require the game thread"));
    const FString AssetPath = Args->GetStringField(TEXT("asset_path"));
    const FString GraphName = Args->HasField(TEXT("graph_name")) ? Args->GetStringField(TEXT("graph_name")) : TEXT("EventGraph");
    UBlueprint* BP = FindBlueprint(AssetPath);
    if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
    UEdGraph* Graph = FindGraphInBlueprint(BP, GraphName);
    if (!Graph) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Graph not found: %s"), *GraphName));
    TArray<FOp> Ops;
    const FString ParseError = ParseOps(Args->GetArrayField(TEXT("operations")), Ops);
    if (!ParseError.IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, ParseError);
    const FString RevisionBefore = Revision(AssetPath, Context);
    FString Expected;
    if (Args->TryGetStringField(TEXT("expected_revision"), Expected) && !Expected.IsEmpty() && Expected != RevisionBefore)
        return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("Blueprint graphs changed since the expected revision; validate again before applying"), FString::Printf(TEXT("current revision %s"), *RevisionBefore));
    const TArray<FString> Problems = Preflight(BP, Graph, Ops);
    const bool bCompile = Args->HasField(TEXT("compile")) && Args->GetBoolField(TEXT("compile"));

    auto Out = MakeShared<FJsonObject>();
    Out->SetStringField(TEXT("asset_path"), BP->GetPathName()); Out->SetStringField(TEXT("graph"), GraphName);
    Out->SetStringField(TEXT("mode"), bPreview ? TEXT("preview") : TEXT("apply")); Out->SetStringField(TEXT("revision_before"), RevisionBefore);
    TArray<TSharedPtr<FJsonValue>> Planned; for (int32 I = 0; I < Ops.Num(); ++I) Planned.Add(MakeShared<FJsonValueObject>(OpJson(Ops[I], I)));
    Out->SetArrayField(TEXT("operations"), Planned);
    TArray<TSharedPtr<FJsonValue>> PJ; for (const FString& P : Problems) PJ.Add(MakeShared<FJsonValueString>(P));
    Out->SetArrayField(TEXT("problems"), PJ); Out->SetBoolField(TEXT("applicable"), Problems.IsEmpty());
    Out->SetStringField(TEXT("required_scope"), TEXT("scene")); Out->SetBoolField(TEXT("writes_files"), false); Out->SetBoolField(TEXT("compiles"), bCompile && !bPreview);
    if (bPreview || !Problems.IsEmpty())
    {
        Out->SetBoolField(TEXT("applied"), false);
        FMCPToolResult R = FMCPToolResult::SuccessStructured(Problems.IsEmpty() ? FString::Printf(TEXT("Patch of %d operation(s) is applicable at revision %s; nothing was changed."), Ops.Num(), *RevisionBefore.Left(12))
            : FString::Printf(TEXT("Patch refused in preflight with %d problem(s); nothing was changed."), Problems.Num()), Out);
        R.bIsError = !Problems.IsEmpty(); return R;
    }

    // Apply through the existing node tools (nested dispatch inherits scope, session and cancellation).
    auto& Registry = FMCPToolRegistry::Get();
    TMap<FString, FString> Aliases;
    auto Resolve = [&](const FString& R) { return R.StartsWith(TEXT("$")) ? Aliases.FindRef(R.Mid(1)) : R; };
    TArray<TSharedPtr<FJsonValue>> Results; FString Failure; int32 FailedIndex = -1;
    for (int32 I = 0; I < Ops.Num() && Failure.IsEmpty(); ++I)
    {
        const FOp& Op = Ops[I];
        auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("asset_path"), AssetPath); A->SetStringField(TEXT("graph_name"), GraphName);
        FString Tool;
        if (Op.Kind == TEXT("add_call_function")) { Tool = TEXT("add_function_call_node"); A->SetStringField(TEXT("function_name"), Op.Function); A->SetStringField(TEXT("target"), Op.TargetClass); }
        else if (Op.Kind == TEXT("add_variable_get")) { Tool = TEXT("add_variable_get_node"); A->SetStringField(TEXT("variable_name"), Op.Variable); }
        else if (Op.Kind == TEXT("add_variable_set")) { Tool = TEXT("add_variable_set_node"); A->SetStringField(TEXT("variable_name"), Op.Variable); }
        else if (Op.Kind == TEXT("add_event")) { Tool = TEXT("add_event_node"); A->SetStringField(TEXT("event_name"), Op.Event); A->RemoveField(TEXT("graph_name")); }
        else if (Op.Kind == TEXT("add_custom_event")) { Tool = TEXT("add_custom_event"); A->SetStringField(TEXT("event_name"), Op.Name); A->RemoveField(TEXT("graph_name")); }
        else if (Op.Kind == TEXT("add_branch")) { Tool = TEXT("add_branch_node"); }
        else if (Op.Kind == TEXT("remove_node")) { Tool = TEXT("remove_node"); A->SetStringField(TEXT("node_id"), Resolve(Op.Node)); }
        else if (Op.Kind == TEXT("connect")) { Tool = TEXT("connect_pins"); A->SetStringField(TEXT("source_node_id"), Resolve(Op.Node)); A->SetStringField(TEXT("source_pin_name"), Op.Pin); A->SetStringField(TEXT("target_node_id"), Resolve(Op.ToNode)); A->SetStringField(TEXT("target_pin_name"), Op.ToPin); }
        else if (Op.Kind == TEXT("disconnect")) { Tool = TEXT("disconnect_pin"); A->SetStringField(TEXT("node_id"), Resolve(Op.Node)); A->SetStringField(TEXT("pin_name"), Op.Pin); }
        else if (Op.Kind == TEXT("set_default")) { Tool = TEXT("set_pin_default_value"); A->SetStringField(TEXT("node_id"), Resolve(Op.Node)); A->SetStringField(TEXT("pin_name"), Op.Pin); A->SetStringField(TEXT("default_value"), Op.Value); }
        else if (Op.Kind == TEXT("move")) { Tool = TEXT("move_node"); A->SetStringField(TEXT("node_id"), Resolve(Op.Node)); A->SetNumberField(TEXT("x"), Op.X); A->SetNumberField(TEXT("y"), Op.Y); }
        if (Op.bHasPos && Op.Kind.StartsWith(TEXT("add_"))) { A->SetNumberField(TEXT("node_x"), Op.X); A->SetNumberField(TEXT("node_y"), Op.Y); }
        const FMCPToolResult R = Registry.ExecuteTool(Tool, A, Context);
        auto Rec = OpJson(Op, I); Rec->SetStringField(TEXT("tool"), Tool);
        if (R.bIsError) { Failure = R.Content.Num() ? R.Content[0].Text : TEXT("nested tool failed"); FailedIndex = I; Rec->SetBoolField(TEXT("verified"), false); Rec->SetStringField(TEXT("error"), Failure); Results.Add(MakeShared<FJsonValueObject>(Rec)); break; }
        if (Op.Kind.StartsWith(TEXT("add_")))
        {
            const FString Id = ExtractNodeId(R);
            if (Id.IsEmpty() || !FindNodeByGuid(Graph, Id)) { Failure = TEXT("created node could not be read back"); FailedIndex = I; Rec->SetBoolField(TEXT("verified"), false); Results.Add(MakeShared<FJsonValueObject>(Rec)); break; }
            Aliases.Add(Op.Alias, Id); Rec->SetStringField(TEXT("node_id"), Id); Rec->SetBoolField(TEXT("verified"), true);
        }
        else if (Op.Kind == TEXT("connect"))
        {
            UEdGraphNode* NA = FindNodeByGuid(Graph, Resolve(Op.Node)); UEdGraphNode* NB = FindNodeByGuid(Graph, Resolve(Op.ToNode));
            UEdGraphPin* PA = NA ? NA->FindPin(FName(*Op.Pin)) : nullptr; UEdGraphPin* PB = NB ? NB->FindPin(FName(*Op.ToPin)) : nullptr;
            Rec->SetBoolField(TEXT("verified"), PA && PB && PA->LinkedTo.Contains(PB));
        }
        else if (Op.Kind == TEXT("remove_node")) Rec->SetBoolField(TEXT("verified"), FindNodeByGuid(Graph, Resolve(Op.Node)) == nullptr);
        else if (Op.Kind == TEXT("set_default"))
        {
            UEdGraphNode* N = FindNodeByGuid(Graph, Resolve(Op.Node)); UEdGraphPin* P = N ? N->FindPin(FName(*Op.Pin)) : nullptr;
            if (P) Rec->SetStringField(TEXT("observed"), P->DefaultValue);
            Rec->SetBoolField(TEXT("verified"), P != nullptr);
        }
        else Rec->SetBoolField(TEXT("verified"), true);
        Results.Add(MakeShared<FJsonValueObject>(Rec));
    }
    FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
    auto AliasJson = MakeShared<FJsonObject>(); for (const auto& P : Aliases) AliasJson->SetStringField(P.Key, P.Value);
    Out->SetObjectField(TEXT("aliases"), AliasJson);
    Out->SetArrayField(TEXT("results"), Results);
    Out->SetStringField(TEXT("revision_after"), Revision(AssetPath, Context));
    Out->SetBoolField(TEXT("applied"), Failure.IsEmpty());
    Out->SetBoolField(TEXT("atomic"), Failure.IsEmpty() || Results.Num() <= 1);
    Out->SetStringField(TEXT("recovery"), Failure.IsEmpty() ? TEXT("editor_undo_available") : TEXT("not_attempted"));
    if (!Failure.IsEmpty())
    {
        Out->SetStringField(TEXT("failed_operation"), FString::Printf(TEXT("[%d] %s"), FailedIndex, *Failure));
        FMCPToolResult R = FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Patch failed at operation %d after %d applied: %s. Surviving effects are listed; use editor Undo to revert this call."), FailedIndex, Results.Num() - 1, *Failure), Out);
        R.bIsError = true; return R;
    }
    if (bCompile)
    {
        auto C = MakeShared<FJsonObject>(); C->SetStringField(TEXT("asset_path"), AssetPath);
        const FMCPToolResult CR = Registry.ExecuteTool(TEXT("compile_blueprint"), C, Context);
        Out->SetBoolField(TEXT("compiled"), !CR.bIsError);
        if (CR.StructuredContent.IsValid()) Out->SetObjectField(TEXT("compile"), CR.StructuredContent);
        else if (CR.Content.Num()) Out->SetStringField(TEXT("compile_text"), CR.Content[0].Text.Left(2000));
    }
    return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Applied %d operation(s); revision %s -> %s%s."), Results.Num(), *RevisionBefore.Left(12), *Out->GetStringField(TEXT("revision_after")).Left(12), bCompile ? (Out->GetBoolField(TEXT("compiled")) ? TEXT(", compiled") : TEXT(", compile FAILED")) : TEXT(", not compiled")), Out);
}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    auto OpSchema = StringToJson(TEXT(R"({"type":"object","required":["op"],"properties":{"op":{"type":"string","enum":["add_call_function","add_variable_get","add_variable_set","add_event","add_custom_event","add_branch","remove_node","connect","disconnect","set_default","move"]},"alias":{"type":"string"},"node":{"type":"string"},"pin":{"type":"string"},"to_node":{"type":"string"},"to_pin":{"type":"string"},"function":{"type":"string"},"target_class":{"type":"string"},"variable":{"type":"string"},"event":{"type":"string"},"name":{"type":"string"},"value":{"type":"string"},"x":{"type":"number"},"y":{"type":"number"}}})"));
    auto OpsSchema = MakeShared<FJsonObject>(); OpsSchema->SetStringField(TEXT("type"), TEXT("array")); OpsSchema->SetObjectField(TEXT("items"), OpSchema);
    OpsSchema->SetNumberField(TEXT("minItems"), 1); OpsSchema->SetNumberField(TEXT("maxItems"), MaxOperations);
    MCP_TOOL(Registry, "apply_blueprint_patch")
        .Description(TEXT("Typed Blueprint graph patch with preflight. operations[] (1..64, in order): add_call_function{alias,function,target_class}, add_variable_get/add_variable_set{alias,variable}, add_event{alias,event}, add_custom_event{alias,name}, add_branch{alias}, remove_node{node}, connect{node,pin,to_node,to_pin}, disconnect{node,pin}, set_default{node,pin,value}, move{node,x,y}. Node references are GUIDs or $alias for nodes added earlier in the patch. mode=preview (default, or dry_run=true) validates every reference, function, variable, pin and schema connection against the live graph and returns applicability, problems and the revision from validate_blueprint without changing anything. mode=apply refuses a stale expected_revision, applies through the existing node tools, reads back links/defaults, returns revision_after and aliases, and optionally compiles (compile=true) returning the compiler result. Mid-patch failure lists surviving operations (atomic=false); recovery is editor Undo. Requires Scene scope."))
        .RequiresPieOff()
        .StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint"), true)
        .StringArg(TEXT("graph_name"), TEXT("Graph name (default EventGraph)"))
        .EnumArg(TEXT("mode"), TEXT("preview (default) or apply"), { TEXT("preview"), TEXT("apply") })
        .StringArg(TEXT("expected_revision"), TEXT("Revision from validate_blueprint or a previous preview; apply refuses on mismatch"))
        .BoolArg(TEXT("compile"), TEXT("Compile after a successful apply (default false)"))
        .ObjectArg(TEXT("operations"), TEXT("Ordered typed operations"), OpsSchema, true)
        .Example(TEXT(R"({"asset_path":"/Game/BP_Door","mode":"preview","operations":[{"op":"add_event","alias":"begin","event":"BeginPlay"},{"op":"add_call_function","alias":"log","function":"PrintString","target_class":"KismetSystemLibrary","x":400},{"op":"connect","node":"$begin","pin":"then","to_node":"$log","to_pin":"execute"},{"op":"set_default","node":"$log","pin":"InString","value":"Door ready"}]})"))
        .OutputSchema(TEXT(R"({"type":"object","required":["asset_path","graph","mode","revision_before","operations","problems","applicable","applied"],"properties":{"asset_path":{"type":"string"},"graph":{"type":"string"},"mode":{"type":"string"},"revision_before":{"type":"string"},"revision_after":{"type":"string"},"operations":{"type":"array"},"problems":{"type":"array"},"applicable":{"type":"boolean"},"applied":{"type":"boolean"},"results":{"type":"array"},"aliases":{"type":"object"},"atomic":{"type":"boolean"},"recovery":{"type":"string"},"failed_operation":{"type":"string"},"compiles":{"type":"boolean"},"compiled":{"type":"boolean"},"compile":{"type":"object"},"required_scope":{"type":"string"},"writes_files":{"type":"boolean"}}})"))
        .Preview([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context) { return Run(Args, Context, true); })
        .HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context)
        {
            const FString Mode = Args->HasField(TEXT("mode")) ? Args->GetStringField(TEXT("mode")) : TEXT("preview");
            return Run(Args, Context, Mode != TEXT("apply"));
        });
}
}
