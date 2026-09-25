// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// Phase D.4 — Runtime debug & introspection.
//
// Wraps FKismetDebugUtilities for breakpoints + pin watches, plus a small
// runtime-error capture buffer that hooks GLog. Breakpoints and watches are
// editor-session state — they are persisted to the user's local settings by
// UE the same way the Blueprint editor's manual breakpoints are.
//
// Limitations called out in the roadmap:
//  - Call-stack queries (`get_call_stack`) only return data while paused on
//    a hit; otherwise we report empty.
//  - `get_last_runtime_error` returns the most recent BP runtime error
//    captured by FBlueprintCoreDelegates::OnScriptException.

#include "Tools/MCPDebugTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/Breakpoint.h"
#include "Kismet2/WatchedPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Script.h"
#include "Blueprint/BlueprintExceptionInfo.h"
#include "Misc/CoreDelegates.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"

namespace MCPDebugTools
{

// ---------------------------------------------------------------------------
// Runtime-error capture
// ---------------------------------------------------------------------------

struct FRuntimeErrorRecord
{
	bool bHave = false;
	FString Message;
	FString ObjectPath;
	FDateTime When;
};

static FRuntimeErrorRecord& GetLastError()
{
	static FRuntimeErrorRecord R;
	return R;
}

static FDelegateHandle GScriptExceptionHandle;

static void OnScriptException(const UObject* Object, const FFrame& /*Stack*/, const FBlueprintExceptionInfo& Info)
{
	if (Info.GetType() == EBlueprintExceptionType::Breakpoint
	 || Info.GetType() == EBlueprintExceptionType::Tracepoint)
	{
		// These are not errors — skip.
		return;
	}
	FRuntimeErrorRecord& R = GetLastError();
	R.bHave = true;
	R.Message = Info.GetDescription().ToString();
	R.ObjectPath = Object ? Object->GetPathName() : FString();
	R.When = FDateTime::UtcNow();
}

static void EnsureScriptExceptionHook()
{
	if (GScriptExceptionHandle.IsValid()) return;
	GScriptExceptionHandle = FBlueprintCoreDelegates::OnScriptException.AddStatic(&OnScriptException);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static UBlueprint* LoadBlueprint(const FString& Path)
{
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path);
	if (BP) return BP;
	// Try with .Object suffix variants
	FString Stripped = Path;
	int32 Dot;
	if (Stripped.FindLastChar(TEXT('.'), Dot)) Stripped = Stripped.Left(Dot);
	return LoadObject<UBlueprint>(nullptr, *Stripped);
}

static UEdGraphNode* FindNodeByGuid(UBlueprint* BP, const FString& GuidStr)
{
	if (!BP) return nullptr;
	FGuid Wanted;
	const bool bGuid = FGuid::Parse(GuidStr, Wanted);

	TArray<UEdGraph*> Graphs;
	BP->GetAllGraphs(Graphs);
	for (UEdGraph* G : Graphs)
	{
		if (!G) continue;
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N) continue;
			if (bGuid && N->NodeGuid == Wanted) return N;
			if (!bGuid && N->GetName() == GuidStr) return N;
		}
	}
	return nullptr;
}

static UEdGraphPin* FindPinById(UBlueprint* BP, const FString& PinIdStr, UEdGraphNode** OutNode = nullptr)
{
	if (!BP) return nullptr;
	FGuid Wanted;
	const bool bGuid = FGuid::Parse(PinIdStr, Wanted);

	TArray<UEdGraph*> Graphs;
	BP->GetAllGraphs(Graphs);
	for (UEdGraph* G : Graphs)
	{
		for (UEdGraphNode* N : G->Nodes)
		{
			if (!N) continue;
			for (UEdGraphPin* P : N->Pins)
			{
				if (!P) continue;
				if ((bGuid && P->PinId == Wanted) || (!bGuid && P->PinName.ToString() == PinIdStr))
				{
					if (OutNode) *OutNode = N;
					return P;
				}
			}
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------

void RegisterAll(FMCPToolRegistry& Registry)
{
	EnsureScriptExceptionHook();

	// ================================================================
	// set_blueprint_breakpoint
	// ================================================================
	MCP_TOOL(Registry, "set_blueprint_breakpoint")
		.Description(TEXT(
			"Place a breakpoint on a Blueprint node. node_id may be the node's NodeGuid (preferred) or its "
			"object name. Idempotent: if a breakpoint already exists on the node, it is enabled."))
		.StringArg(TEXT("blueprint"), TEXT("Blueprint asset path (e.g., '/Game/BP_Foo')."), true)
		.StringArg(TEXT("node_id"), TEXT("Node GUID or object name."), true)
		.BoolArg(TEXT("enabled"), TEXT("Whether the breakpoint should be enabled (default true)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString BPPath, NodeId;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("blueprint"), BPPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_id"), NodeId));

			UBlueprint* BP = LoadBlueprint(BPPath);
			if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

			UEdGraphNode* Node = FindNodeByGuid(BP, NodeId);
			if (!Node) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Node '%s' not found in '%s'"), *NodeId, *BPPath));

			bool bEnabled = true;
			Args->TryGetBoolField(TEXT("enabled"), bEnabled);

			FBlueprintBreakpoint* Existing = FKismetDebugUtilities::FindBreakpointForNode(Node, BP);
			if (Existing)
			{
				FKismetDebugUtilities::SetBreakpointEnabled(*Existing, bEnabled);
			}
			else
			{
				FKismetDebugUtilities::CreateBreakpoint(BP, Node, bEnabled);
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("blueprint"), BPPath);
			R->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
			R->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
			R->SetBoolField(TEXT("enabled"), bEnabled);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Breakpoint set on '%s'"), *Node->NodeGuid.ToString()), R);
		});

	// ================================================================
	// clear_blueprint_breakpoint
	// ================================================================
	MCP_TOOL(Registry, "clear_blueprint_breakpoint")
		.Description(TEXT("Remove the breakpoint from a Blueprint node, if any."))
		.StringArg(TEXT("blueprint"), TEXT("Blueprint asset path."), true)
		.StringArg(TEXT("node_id"), TEXT("Node GUID or name."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString BPPath, NodeId;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("blueprint"), BPPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_id"), NodeId));

			UBlueprint* BP = LoadBlueprint(BPPath);
			if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
			UEdGraphNode* Node = FindNodeByGuid(BP, NodeId);
			if (!Node) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Node '%s' not found"), *NodeId));

			FKismetDebugUtilities::RemoveBreakpointFromNode(Node, BP);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("ok"), true);
			R->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
			return FMCPToolResult::SuccessStructured(TEXT("Breakpoint cleared"), R);
		});

	// ================================================================
	// list_breakpoints
	// ================================================================
	MCP_TOOL(Registry, "list_breakpoints")
		.Description(TEXT(
			"List all breakpoints on a single blueprint, or — if blueprint is omitted — across every loaded "
			"Blueprint. Returns {blueprint, node_guid, node_title, enabled} per breakpoint."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("blueprint"), TEXT("Optional Blueprint asset path. If omitted, scans all loaded Blueprints."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			TArray<UBlueprint*> Targets;
			FString BPPath;
			if (Args->TryGetStringField(TEXT("blueprint"), BPPath) && !BPPath.IsEmpty())
			{
				UBlueprint* BP = LoadBlueprint(BPPath);
				if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
				Targets.Add(BP);
			}
			else
			{
				for (TObjectIterator<UBlueprint> It; It; ++It)
				{
					if (FKismetDebugUtilities::BlueprintHasBreakpoints(*It))
					{
						Targets.Add(*It);
					}
				}
			}

			TArray<TSharedPtr<FJsonValue>> Items;
			for (UBlueprint* BP : Targets)
			{
				FKismetDebugUtilities::ForeachBreakpoint(BP, [&](FBlueprintBreakpoint& B)
				{
					UEdGraphNode* N = B.GetLocation();
					TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
					E->SetStringField(TEXT("blueprint"), BP->GetPathName());
					E->SetStringField(TEXT("node_guid"), N ? N->NodeGuid.ToString() : TEXT(""));
					E->SetStringField(TEXT("node_title"), N ? N->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT(""));
					E->SetBoolField(TEXT("enabled"), B.IsEnabled());
					E->SetBoolField(TEXT("enabled_by_user"), B.IsEnabledByUser());
					E->SetStringField(TEXT("location"), B.GetLocationDescription().ToString());
					Items.Add(MakeShared<FJsonValueObject>(E));
				});
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetNumberField(TEXT("total"), Items.Num());
			R->SetArrayField(TEXT("breakpoints"), Items);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d breakpoint(s)"), Items.Num()), R);
		});

	// ================================================================
	// add_watch
	// ================================================================
	MCP_TOOL(Registry, "add_watch")
		.Description(TEXT(
			"Add a pin watch on a Blueprint pin. pin_id is the FGuid of the pin (preferred) or its name. "
			"Watches are surfaced in the Blueprint debugger when running PIE."))
		.StringArg(TEXT("blueprint"), TEXT("Blueprint asset path."), true)
		.StringArg(TEXT("pin_id"), TEXT("Pin GUID or pin name."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			FString BPPath, PinId;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("blueprint"), BPPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("pin_id"), PinId));

			UBlueprint* BP = LoadBlueprint(BPPath);
			if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));

			UEdGraphPin* Pin = FindPinById(BP, PinId);
			if (!Pin) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Pin '%s' not found"), *PinId));

			if (!FKismetDebugUtilities::CanWatchPin(BP, Pin))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					TEXT("Pin cannot be watched"),
					TEXT("Some pin types (exec, delegate) do not support watches."));
			}

			if (!FKismetDebugUtilities::IsPinBeingWatched(BP, Pin))
			{
				FKismetDebugUtilities::AddPinWatch(BP, FBlueprintWatchedPin(Pin));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("blueprint"), BPPath);
			R->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());
			R->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Watch added on pin '%s'"), *Pin->PinName.ToString()), R);
		});

	// ================================================================
	// get_watches
	// ================================================================
	MCP_TOOL(Registry, "get_watches")
		.Description(TEXT(
			"List currently watched pins for one Blueprint, or — when blueprint is omitted — across all "
			"loaded Blueprints. Returns {blueprint, pin_id, pin_name, node_title}."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("blueprint"), TEXT("Optional Blueprint asset path."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			TArray<UBlueprint*> Targets;
			FString BPPath;
			if (Args->TryGetStringField(TEXT("blueprint"), BPPath) && !BPPath.IsEmpty())
			{
				UBlueprint* BP = LoadBlueprint(BPPath);
				if (!BP) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Blueprint not found: %s"), *BPPath));
				Targets.Add(BP);
			}
			else
			{
				for (TObjectIterator<UBlueprint> It; It; ++It) { Targets.Add(*It); }
			}

			TArray<TSharedPtr<FJsonValue>> Items;
			for (UBlueprint* BP : Targets)
			{
				FKismetDebugUtilities::ForeachPinPropertyWatch(BP, [&](FBlueprintWatchedPin& W)
				{
					UEdGraphPin* P = W.Get();
					if (!P) return;
					UEdGraphNode* N = P->GetOwningNode();
					TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
					E->SetStringField(TEXT("blueprint"), BP->GetPathName());
					E->SetStringField(TEXT("pin_id"), P->PinId.ToString());
					E->SetStringField(TEXT("pin_name"), P->PinName.ToString());
					E->SetStringField(TEXT("node_title"), N ? N->GetNodeTitle(ENodeTitleType::ListView).ToString() : TEXT(""));
					Items.Add(MakeShared<FJsonValueObject>(E));
				});
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetNumberField(TEXT("total"), Items.Num());
			R->SetArrayField(TEXT("watches"), Items);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d watch(es)"), Items.Num()), R);
		});

	// ================================================================
	// get_last_runtime_error
	// ================================================================
	MCP_TOOL(Registry, "get_last_runtime_error")
		.Description(TEXT(
			"Return the most recent Blueprint runtime error captured since the editor session started. "
			"Hooks FBlueprintCoreDelegates::OnScriptException; returns have=false if none."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			const FRuntimeErrorRecord& R = GetLastError();
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("have"), R.bHave);
			if (R.bHave)
			{
				Obj->SetStringField(TEXT("message"), R.Message);
				Obj->SetStringField(TEXT("object_path"), R.ObjectPath);
				Obj->SetStringField(TEXT("when_utc"), R.When.ToIso8601());
			}
			return FMCPToolResult::SuccessStructured(
				R.bHave ? FString::Printf(TEXT("Last error: %s"), *R.Message) : TEXT("No runtime errors captured"),
				Obj);
		});

	// ================================================================
	// get_call_stack
	// ================================================================
	MCP_TOOL(Registry, "get_call_stack")
		.Description(TEXT(
			"Return the Blueprint call stack at the most recent breakpoint hit (only available while paused). "
			"When no breakpoint has been hit, returns paused=false."))
		.ReadOnly()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			UEdGraphNode* MostRecent = FKismetDebugUtilities::GetMostRecentBreakpointHit();
			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("paused"), MostRecent != nullptr);
			if (!MostRecent)
			{
				return FMCPToolResult::SuccessStructured(TEXT("Not paused"), R);
			}

			R->SetStringField(TEXT("node_guid"), MostRecent->NodeGuid.ToString());
			R->SetStringField(TEXT("node_title"), MostRecent->GetNodeTitle(ENodeTitleType::ListView).ToString());

			UEdGraph* G = MostRecent->GetGraph();
			if (G) R->SetStringField(TEXT("graph"), G->GetName());

			// Full BP call-stack reconstruction would need access to FBlueprintExceptionInfo
			// captured live. Surface what we can without that: the most recent node only.
			TArray<TSharedPtr<FJsonValue>> Frames;
			TSharedPtr<FJsonObject> Frame = MakeShared<FJsonObject>();
			Frame->SetStringField(TEXT("node_guid"), MostRecent->NodeGuid.ToString());
			Frame->SetStringField(TEXT("node_title"), MostRecent->GetNodeTitle(ENodeTitleType::ListView).ToString());
			Frame->SetStringField(TEXT("graph"), G ? G->GetName() : TEXT(""));
			Frames.Add(MakeShared<FJsonValueObject>(Frame));
			R->SetArrayField(TEXT("frames"), Frames);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Paused on '%s'"), *MostRecent->GetNodeTitle(ENodeTitleType::ListView).ToString()), R);
		});
}

} // namespace MCPDebugTools
