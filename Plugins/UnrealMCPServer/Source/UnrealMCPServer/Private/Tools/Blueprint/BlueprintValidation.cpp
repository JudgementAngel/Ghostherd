// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 12: structured, non-compiling Blueprint graph validation with revision fingerprints
// (first slice of V5-22). Enhances the inherited validate_blueprint in place; compile stays a
// separate, explicitly mutating step (compile_blueprint).

#include "Tools/Blueprint/BlueprintCommon.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Tunnel.h"
#include "K2Node_Knot.h"
#include "EdGraphNode_Comment.h"
#include "Misc/SecureHash.h"

namespace MCPBlueprintTools::Validation
{
using namespace MCPBlueprintTools::Common;

namespace
{
struct FGraphRef { UEdGraph* Graph = nullptr; FString Kind; };

TArray<FGraphRef> CollectGraphs(UBlueprint* BP)
{
    TArray<FGraphRef> Out;
    for (UEdGraph* G : BP->UbergraphPages) if (G) Out.Add({ G, TEXT("ubergraph") });
    for (UEdGraph* G : BP->FunctionGraphs) if (G) Out.Add({ G, TEXT("function") });
    for (UEdGraph* G : BP->MacroGraphs) if (G) Out.Add({ G, TEXT("macro") });
    for (const FBPInterfaceDescription& I : BP->ImplementedInterfaces) for (UEdGraph* G : I.Graphs) if (G) Out.Add({ G, TEXT("interface") });
    return Out;
}

FString Sha1(const FString& Text)
{
    FTCHARToUTF8 Bytes(*Text);
    return FSHA1::HashBuffer(Bytes.Get(), Bytes.Length()).ToString();
}

/** Deterministic fingerprint: every node's GUID and class plus every link, sorted. */
FString GraphRevision(UEdGraph* Graph)
{
    TArray<FString> Lines;
    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node) continue;
        Lines.Add(FString::Printf(TEXT("N|%s|%s"), *Node->NodeGuid.ToString(), *Node->GetClass()->GetPathName()));
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin) continue;
            if (Pin->Direction == EGPD_Input && !Pin->DefaultValue.IsEmpty() && Pin->LinkedTo.Num() == 0)
                Lines.Add(FString::Printf(TEXT("D|%s|%s|%s"), *Node->NodeGuid.ToString(), *Pin->PinName.ToString(), *Pin->DefaultValue));
            if (Pin->Direction != EGPD_Output) continue;
            for (UEdGraphPin* Linked : Pin->LinkedTo)
                if (Linked && Linked->GetOwningNodeUnchecked())
                    Lines.Add(FString::Printf(TEXT("L|%s|%s|%s|%s"), *Node->NodeGuid.ToString(), *Pin->PinName.ToString(), *Linked->GetOwningNode()->NodeGuid.ToString(), *Linked->PinName.ToString()));
        }
    }
    Lines.Sort();
    return Sha1(FString::Join(Lines, TEXT("\n")));
}

void AddIssue(TArray<TSharedPtr<FJsonValue>>& Issues, const FString& Rule, const FString& Severity, const FString& GraphName, UEdGraphNode* Node, const FString& PinName, const FString& Message)
{
    auto I = MakeShared<FJsonObject>();
    I->SetStringField(TEXT("rule"), Rule); I->SetStringField(TEXT("severity"), Severity); I->SetStringField(TEXT("graph"), GraphName);
    if (Node)
    {
        I->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
        I->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
        I->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
    }
    if (!PinName.IsEmpty()) I->SetStringField(TEXT("pin"), PinName);
    I->SetStringField(TEXT("message"), Message);
    Issues.Add(MakeShared<FJsonValueObject>(I));
}

bool IsEntryNode(const UEdGraphNode* Node)
{
    return Node->IsA<UK2Node_Event>() || Node->IsA<UK2Node_FunctionEntry>() || Node->IsA<UK2Node_Tunnel>();
}
bool IsReferencePin(const UEdGraphPin* Pin)
{
    const FName& C = Pin->PinType.PinCategory;
    return C == UEdGraphSchema_K2::PC_Object || C == UEdGraphSchema_K2::PC_Class || C == UEdGraphSchema_K2::PC_Interface
        || C == UEdGraphSchema_K2::PC_SoftObject || C == UEdGraphSchema_K2::PC_SoftClass;
}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
    MCP_TOOL(Registry, "validate_blueprint")
        .ReadOnly().Idempotent()
        .Description(TEXT("Structured, non-compiling Blueprint graph validation. Walks the ubergraph, function, macro and interface graphs (or one graph_name) and reports issues with graph, node GUID, node title and pin: orphan_node (no links at all), exec_in_unconnected (unreachable impure node), exec_out_unconnected (dangling execution output, informational), missing_required_input (object/class reference input with neither link nor default), incompatible_link (existing link the K2 schema no longer accepts), orphaned_pin (pin no longer on the signature), disabled_node (template events are disabled, never orphans) and stale_compile_message (message left by the last compile, not re-evaluated). Returns per-graph and overall revision fingerprints for later patches. Nothing is compiled or modified; use compile_blueprint for compiler diagnostics."))
        .StringArg(TEXT("asset_path"), TEXT("Content path of the Blueprint to validate"), true)
        .StringArg(TEXT("graph_name"), TEXT("Validate only this graph (default: all graphs)"))
        .IntArg(TEXT("limit"), TEXT("Maximum issues to return, 1..2000 (default 500)"))
        .OutputSchema(TEXT(R"({"type":"object","required":["asset_path","revision","graphs","issues","counts","node_count","deterministic","warnings"],"properties":{"asset_path":{"type":"string"},"revision":{"type":"string"},"graphs":{"type":"array"},"issues":{"type":"array"},"counts":{"type":"object"},"node_count":{"type":"integer"},"deterministic":{"type":"boolean"},"truncated":{"type":"boolean"},"warnings":{"type":"array"},"errors":{"type":"array"}}})"))
        .Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
        {
            if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Validation requires the game thread"));
            const FString AssetPath = Args->GetStringField(TEXT("asset_path"));
            UBlueprint* BP = FindBlueprint(AssetPath);
            if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
            const int32 Limit = Args->HasField(TEXT("limit")) ? (int32)Args->GetNumberField(TEXT("limit")) : 500;
            if (Limit < 1 || Limit > 2000) return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange, TEXT("limit must be 1..2000"));
            FString OnlyGraph; Args->TryGetStringField(TEXT("graph_name"), OnlyGraph);

            TArray<FGraphRef> Graphs = CollectGraphs(BP);
            if (!OnlyGraph.IsEmpty())
            {
                Graphs = Graphs.FilterByPredicate([&](const FGraphRef& G) { return G.Graph->GetName() == OnlyGraph; });
                if (Graphs.IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Graph not found: %s"), *OnlyGraph));
            }
            const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
            TArray<TSharedPtr<FJsonValue>> Issues, GraphsJson, Warnings, Errors;
            TArray<FString> Revisions; int32 NodeCount = 0;
            for (const FGraphRef& G : Graphs)
            {
                const FString GraphName = G.Graph->GetName();
                const FString Rev = GraphRevision(G.Graph); Revisions.Add(Rev);
                auto GJ = MakeShared<FJsonObject>(); GJ->SetStringField(TEXT("name"), GraphName); GJ->SetStringField(TEXT("kind"), G.Kind);
                GJ->SetNumberField(TEXT("node_count"), G.Graph->Nodes.Num()); GJ->SetStringField(TEXT("revision"), Rev);
                GraphsJson.Add(MakeShared<FJsonValueObject>(GJ));
                for (UEdGraphNode* Node : G.Graph->Nodes)
                {
                    if (!Node) continue;
                    ++NodeCount;
                    if (Node->IsA<UEdGraphNode_Comment>() || Node->IsA<UK2Node_Knot>()) continue;
                    const FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
                    bool bAnyLink = false, bHasExecIn = false, bExecInLinked = false;
                    const bool bEnabled = Node->IsNodeEnabled();
                    for (UEdGraphPin* Pin : Node->Pins)
                    {
                        if (!Pin || Pin->bHidden) continue;
                        if (Pin->LinkedTo.Num() > 0) bAnyLink = true;
                        if (!bEnabled) continue;
                        const bool bExec = Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec;
                        if (bExec && Pin->Direction == EGPD_Input) { bHasExecIn = true; if (Pin->LinkedTo.Num() > 0) bExecInLinked = true; }
                        if (Pin->bOrphanedPin) AddIssue(Issues, TEXT("orphaned_pin"), TEXT("warning"), GraphName, Node, Pin->PinName.ToString(), FString::Printf(TEXT("Pin '%s' on '%s' no longer exists on the signature"), *Pin->PinName.ToString(), *Title));
                        if (bExec && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() == 0)
                            AddIssue(Issues, TEXT("exec_out_unconnected"), TEXT("info"), GraphName, Node, Pin->PinName.ToString(), FString::Printf(TEXT("Execution output '%s' on '%s' leads nowhere"), *Pin->PinName.ToString(), *Title));
                        if (!bExec && Pin->Direction == EGPD_Input && Pin->LinkedTo.Num() == 0 && IsReferencePin(Pin) && !Pin->bDefaultValueIsIgnored
                            && !Pin->DefaultObject && Pin->DefaultValue.IsEmpty() && Pin->PinName != UEdGraphSchema_K2::PN_Self && !Pin->bNotConnectable)
                            AddIssue(Issues, TEXT("missing_required_input"), TEXT("warning"), GraphName, Node, Pin->PinName.ToString(), FString::Printf(TEXT("Reference input '%s' on '%s' has neither a link nor a default"), *Pin->PinName.ToString(), *Title));
                        if (Pin->Direction == EGPD_Output)
                            for (UEdGraphPin* Linked : Pin->LinkedTo)
                                if (Linked && Linked->GetOwningNodeUnchecked() && K2 && !K2->ArePinsCompatible(Pin, Linked, BP->GeneratedClass))
                                    AddIssue(Issues, TEXT("incompatible_link"), TEXT("error"), GraphName, Node, Pin->PinName.ToString(), FString::Printf(TEXT("Link from '%s.%s' to '%s.%s' is no longer type-compatible"), *Title, *Pin->PinName.ToString(), *Linked->GetOwningNode()->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Linked->PinName.ToString()));
                    }
                    // Disabled nodes (the editor's greyed template events) and entry nodes are never orphans:
                    // an unused event is normal scaffolding and is already visible as exec_out_unconnected.
                    if (!Node->IsNodeEnabled()) AddIssue(Issues, TEXT("disabled_node"), TEXT("info"), GraphName, Node, FString(), FString::Printf(TEXT("'%s' is disabled"), *Title));
                    else if (!bAnyLink && Node->Pins.Num() > 0 && !IsEntryNode(Node)) AddIssue(Issues, TEXT("orphan_node"), TEXT("warning"), GraphName, Node, FString(), FString::Printf(TEXT("'%s' has no connections"), *Title));
                    else if (bHasExecIn && !bExecInLinked && !IsEntryNode(Node)) AddIssue(Issues, TEXT("exec_in_unconnected"), TEXT("warning"), GraphName, Node, FString(), FString::Printf(TEXT("'%s' can never execute: its execution input is unconnected"), *Title));
                    if (Node->bHasCompilerMessage && !Node->ErrorMsg.IsEmpty())
                        AddIssue(Issues, TEXT("stale_compile_message"), TEXT("info"), GraphName, Node, FString(), FString::Printf(TEXT("Last compile reported on '%s': %s"), *Title, *Node->ErrorMsg));
                }
            }
            Revisions.Sort();
            auto Counts = MakeShared<FJsonObject>();
            for (const TCHAR* Rule : { TEXT("orphan_node"), TEXT("exec_in_unconnected"), TEXT("exec_out_unconnected"), TEXT("missing_required_input"), TEXT("incompatible_link"), TEXT("orphaned_pin"), TEXT("disabled_node"), TEXT("stale_compile_message") })
            {
                int32 N = 0; for (const auto& V : Issues) if (V->AsObject()->GetStringField(TEXT("rule")) == Rule) ++N;
                Counts->SetNumberField(Rule, N);
            }
            const bool bTruncated = Issues.Num() > Limit;
            if (bTruncated) Issues.SetNum(Limit);
            for (const auto& V : Issues)
            {
                const auto I = V->AsObject(); const FString Sev = I->GetStringField(TEXT("severity"));
                if (Sev == TEXT("error")) Errors.Add(MakeShared<FJsonValueString>(I->GetStringField(TEXT("message"))));
                else if (Sev == TEXT("warning")) Warnings.Add(MakeShared<FJsonValueString>(I->GetStringField(TEXT("message"))));
            }
            auto Out = MakeShared<FJsonObject>();
            Out->SetStringField(TEXT("asset_path"), BP->GetPathName());
            Out->SetStringField(TEXT("revision"), Sha1(FString::Join(Revisions, TEXT("\n"))));
            Out->SetArrayField(TEXT("graphs"), GraphsJson); Out->SetArrayField(TEXT("issues"), Issues); Out->SetObjectField(TEXT("counts"), Counts);
            Out->SetNumberField(TEXT("node_count"), NodeCount); Out->SetBoolField(TEXT("deterministic"), true); Out->SetBoolField(TEXT("truncated"), bTruncated);
            Out->SetArrayField(TEXT("warnings"), Warnings); Out->SetArrayField(TEXT("errors"), Errors);
            return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%s: %d graph(s), %d node(s), %d error(s), %d warning(s), %d info; revision %s. Not compiled."),
                *BP->GetName(), Graphs.Num(), NodeCount, Errors.Num(), Warnings.Num(), Issues.Num() - Errors.Num() - Warnings.Num(), *Out->GetStringField(TEXT("revision")).Left(12)), Out);
        });
}
}
