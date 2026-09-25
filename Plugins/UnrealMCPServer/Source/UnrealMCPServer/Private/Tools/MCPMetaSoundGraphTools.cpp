// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// Phase D.5 — MetaSound graph editing parity.
// v4.5 (UE 5.8) — graph mutators IMPLEMENTED.
//
// In 5.8 the MetaSound Frontend builder API (`FMetaSoundFrontendDocumentBuilder`)
// is the sanctioned path for programmatic graph edits, and `FClassInterface`
// left experimental status (FNodeClassMetadata::DefaultInterface now takes it
// directly; the old FVertexInterface helpers on IDataTypeRegistry were removed).
// We therefore take the `MetasoundFrontend`/`MetasoundEngine`/`MetasoundEditor`
// module deps (Build.cs, guarded by Version.MinorVersion >= 8) and edit the
// document in place.
//
// Scope:
//   * Read-only tools (`metasound_get_graph`, `metasound_list_node_classes`)
//     still enumerate via reflection — no document dependency, works even when
//     the MetaSound plugin is disabled at runtime (RequireMetaSound guards it).
//   * Compile (`metasound_compile`) marks the asset dirty + saves, which forces
//     the builder to rebuild the Frontend document on next access.
//   * Graph mutations (`metasound_add_node`, `metasound_remove_node`,
//     `metasound_connect_pins`, `metasound_disconnect_pin`,
//     `metasound_set_node_property`) now operate on the live document through
//     FMetaSoundFrontendDocumentBuilder. Each mutation marks the package dirty;
//     pair with metasound_compile (or save_all) to persist.
//
// Node identity: the Frontend addresses nodes by FGuid. Tools accept/return the
// guid as a string (node_id). Pins are addressed by vertex name (matching the
// MetaSound editor's pin labels); we resolve name -> vertex GUID internally.

#include "Tools/MCPMetaSoundGraphTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectIterator.h"

// v4.5 — MetaSound Frontend document editing (5.8).
#include "MetasoundDocumentInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilder.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendRegistryKey.h"

namespace MCPMetaSoundGraphTools
{

static bool IsMetaSoundLoaded()
{
	return FindFirstObject<UClass>(TEXT("MetaSoundSource"), EFindFirstObjectOptions::ExactClass) != nullptr;
}

static FMCPValidateResult RequireMetaSound()
{
	if (!IsMetaSoundLoaded())
	{
		return FMCPValidateResult::FailCoded(EMCPError::Unsupported,
			TEXT("MetaSound plugin is not loaded"),
			TEXT("Enable Edit > Plugins > MetaSound and restart the editor before invoking metasound_* tools."));
	}
	return FMCPValidateResult::Ok();
}

static UObject* LoadMetaSoundAsset(const FString& Path)
{
	return StaticLoadObject(UObject::StaticClass(), nullptr, *Path);
}

// ----------------------------------------------------------------------------
// v4.5 document-builder helpers (5.8 MetasoundFrontend)
// ----------------------------------------------------------------------------

/** Load an asset and return it as a MetaSound document interface, or nullptr.
 *  Both UMetaSoundSource and UMetaSoundPatch implement IMetaSoundDocumentInterface. */
static TScriptInterface<IMetaSoundDocumentInterface> LoadMetaSoundDocument(const FString& Path, UObject*& OutAsset)
{
	OutAsset = LoadMetaSoundAsset(Path);
	TScriptInterface<IMetaSoundDocumentInterface> DocIface(OutAsset);
	return DocIface;
}

/** Parse "Namespace.Name" or "Namespace.Name.Variant" into an FMetasoundFrontendClassName.
 *  A bare "Name" is treated as an empty namespace. */
static FMetasoundFrontendClassName ParseClassName(const FString& In)
{
	TArray<FString> Parts;
	In.ParseIntoArray(Parts, TEXT("."), /*CullEmpty*/ false);
	FName Namespace, Name, Variant;
	if (Parts.Num() >= 3)
	{
		Namespace = FName(*Parts[0]);
		Name = FName(*Parts[1]);
		Variant = FName(*Parts[2]);
	}
	else if (Parts.Num() == 2)
	{
		Namespace = FName(*Parts[0]);
		Name = FName(*Parts[1]);
	}
	else
	{
		Name = FName(*In);
	}
	return FMetasoundFrontendClassName(Namespace, Name, Variant);
}

/** Resolve a node by GUID string. Returns nullptr on parse failure or miss. */
static const FMetasoundFrontendNode* FindNode(FMetaSoundFrontendDocumentBuilder& Builder, const FString& NodeIdStr, FGuid& OutGuid)
{
	if (!FGuid::Parse(NodeIdStr, OutGuid))
	{
		return nullptr;
	}
	return Builder.FindNode(OutGuid);
}

/** Build a structured error for a missing node id. */
static FMCPToolResult NodeNotFound(const FString& NodeIdStr)
{
	return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
		FString::Printf(TEXT("Node id not found (or not a valid GUID): %s"), *NodeIdStr),
		TEXT("Use metasound_get_graph to list node ids, or the GUID returned by metasound_add_node."));
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// metasound_list_node_classes
	// ================================================================
	MCP_TOOL(Registry, "metasound_list_node_classes")
		.Description(TEXT(
			"Enumerate MetaSound node classes registered with the engine. Returns a list of "
			"{class_name, full_name, namespace} entries discovered via the UObject reflection system. "
			"Useful for discovering valid node_class_path values for metasound_add_node."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("name_filter"), TEXT("Optional substring filter."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString Filter;
			Args->TryGetStringField(TEXT("name_filter"), Filter);

			// Walk every UClass and pick out anything declared inside Metasound* modules.
			TArray<TSharedPtr<FJsonValue>> Items;
			for (TObjectIterator<UClass> It; It; ++It)
			{
				UClass* C = *It;
				if (!C) continue;
				const FString Pkg = C->GetOuterUPackage()->GetName();
				if (!Pkg.StartsWith(TEXT("/Script/Metasound"))) continue;
				if (!Filter.IsEmpty() && !C->GetName().Contains(Filter, ESearchCase::IgnoreCase)) continue;

				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("class_name"), C->GetName());
				E->SetStringField(TEXT("full_name"), C->GetPathName());
				E->SetStringField(TEXT("namespace"), Pkg);
				Items.Add(MakeShared<FJsonValueObject>(E));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetNumberField(TEXT("total"), Items.Num());
			R->SetArrayField(TEXT("classes"), Items);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%d MetaSound class(es)"), Items.Num()), R);
		});

	// ================================================================
	// metasound_get_graph
	// ================================================================
	MCP_TOOL(Registry, "metasound_get_graph")
		.Description(TEXT(
			"Return a structural summary of a MetaSound asset: class, package, exposed inputs/outputs, "
			"and any graph metadata reachable via UObject reflection. For full node-and-connection "
			"introspection the MetasoundFrontend module is required (see hint on mutation tools)."))
		.ReadOnly()
		.Idempotent()
		.StringArg(TEXT("metasound_path"), TEXT("Asset path to a MetaSound Source or Patch."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("metasound_path"), AssetPath));

			UObject* Asset = LoadMetaSoundAsset(AssetPath);
			if (!Asset)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("path"), Asset->GetPathName());
			R->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
			R->SetStringField(TEXT("package"), Asset->GetOutermost()->GetName());

			// Walk top-level UProperties to expose inputs / outputs
			TArray<TSharedPtr<FJsonValue>> Props;
			for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
			{
				FProperty* P = *PropIt;
				if (!P) continue;
				TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("name"), P->GetName());
				E->SetStringField(TEXT("type"), P->GetCPPType());
				Props.Add(MakeShared<FJsonValueObject>(E));
			}
			R->SetArrayField(TEXT("reflected_properties"), Props);
			R->SetStringField(TEXT("note"),
				TEXT("Full node/connection enumeration requires MetasoundFrontend module access; "
				     "use metasound_list_node_classes to discover registered classes instead."));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("%s (%d reflected properties)"), *Asset->GetPathName(), Props.Num()),
				R);
		});

	// ================================================================
	// metasound_compile
	// ================================================================
	MCP_TOOL(Registry, "metasound_compile")
		.Description(TEXT(
			"Mark the MetaSound asset dirty and save it to disk. The MetaSound builder rebuilds the "
			"Frontend document on next access, which is the engine's compile path. Per-node error "
			"surfacing requires the MetasoundFrontend module."))
		.StringArg(TEXT("metasound_path"), TEXT("Asset path to compile."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString AssetPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("metasound_path"), AssetPath));

			UObject* Asset = LoadMetaSoundAsset(AssetPath);
			if (!Asset)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			}

			Asset->MarkPackageDirty();
			UPackage* Pkg = Asset->GetOutermost();
			const FString Filename = FPackageName::LongPackageNameToFilename(
				Pkg->GetName(), FPackageName::GetAssetPackageExtension());

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			const bool bSaved = UPackage::SavePackage(Pkg, Asset, *Filename, SaveArgs);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("path"), AssetPath);
			R->SetBoolField(TEXT("saved"), bSaved);
			R->SetStringField(TEXT("filename"), Filename);
			return FMCPToolResult::SuccessStructured(
				bSaved ? TEXT("MetaSound saved (Frontend rebuild deferred to next access)")
				       : TEXT("Save failed"), R);
		});

	// ================================================================
	// metasound_add_node  — v4.5 implemented (FMetaSoundFrontendDocumentBuilder)
	// ================================================================
	MCP_TOOL(Registry, "metasound_add_node")
		.Description(TEXT(
			"Insert a node into a MetaSound graph. node_class_path is a MetaSound Frontend class name "
			"\"Namespace.Name\" or \"Namespace.Name.Variant\" (e.g. \"UE.Sine.Audio\"). Returns the new "
			"node's GUID (node_id) for use with connect_pins / set_node_property. Marks the asset dirty; "
			"call metasound_compile to persist."))
		.StringArg(TEXT("metasound_path"), TEXT("Asset path to the MetaSound."), true)
		.StringArg(TEXT("node_class_path"), TEXT("Frontend class name 'Namespace.Name[.Variant]'."), true)
		.IntArg(TEXT("major_version"), TEXT("Node class major version (default 1)."))
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString AssetPath, ClassPath;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("metasound_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_class_path"), ClassPath));
			int32 MajorVersion = 1;
			if (Args->HasField(TEXT("major_version")))
			{
				MajorVersion = FMath::Max(1, (int32)Args->GetNumberField(TEXT("major_version")));
			}

			UObject* Asset = nullptr;
			TScriptInterface<IMetaSoundDocumentInterface> Doc = LoadMetaSoundDocument(AssetPath, Asset);
			if (!Asset) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			if (!Doc) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("'%s' is not a MetaSound document"), *AssetPath),
				TEXT("Pass a MetaSound Source or Patch asset."));

			FMetaSoundFrontendDocumentBuilder Builder(Doc);
			const FMetasoundFrontendClassName ClassName = ParseClassName(ClassPath);
			const FMetasoundFrontendNode* NewNode = Builder.AddNodeByClassName(ClassName, MajorVersion);
			if (!NewNode)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("MetaSound node class not found / not registered: %s"), *ClassPath),
					TEXT("Use metasound_list_node_classes to discover classes; class names are 'Namespace.Name[.Variant]'."));
			}

			Asset->MarkPackageDirty();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("metasound_path"), AssetPath);
			R->SetStringField(TEXT("node_id"), NewNode->GetID().ToString());
			R->SetStringField(TEXT("class_name"), ClassPath);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Added node '%s' (id %s) to '%s'."),
					*ClassPath, *NewNode->GetID().ToString(), *AssetPath), R);
		});

	// ================================================================
	// metasound_remove_node  — v4.5 implemented
	// ================================================================
	MCP_TOOL(Registry, "metasound_remove_node")
		.Description(TEXT(
			"Remove a node (by GUID) from a MetaSound graph. Also removes its edges. Marks the asset dirty."))
		.Destructive()
		.StringArg(TEXT("metasound_path"), TEXT("Asset path."), true)
		.StringArg(TEXT("node_id"), TEXT("Node GUID to remove (from metasound_add_node / metasound_get_graph)."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString AssetPath, NodeIdStr;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("metasound_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_id"), NodeIdStr));

			UObject* Asset = nullptr;
			TScriptInterface<IMetaSoundDocumentInterface> Doc = LoadMetaSoundDocument(AssetPath, Asset);
			if (!Asset) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			if (!Doc) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("'%s' is not a MetaSound document"), *AssetPath));

			FMetaSoundFrontendDocumentBuilder Builder(Doc);
			FGuid NodeGuid;
			if (!FindNode(Builder, NodeIdStr, NodeGuid)) return NodeNotFound(NodeIdStr);

			if (!Builder.RemoveNode(NodeGuid))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Failed to remove node %s"), *NodeIdStr));
			}
			Asset->MarkPackageDirty();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("metasound_path"), AssetPath);
			R->SetStringField(TEXT("removed_node_id"), NodeGuid.ToString());
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Removed node %s from '%s'."), *NodeGuid.ToString(), *AssetPath), R);
		});

	// ================================================================
	// metasound_connect_pins  — v4.5 implemented
	// ================================================================
	MCP_TOOL(Registry, "metasound_connect_pins")
		.Description(TEXT(
			"Wire an output pin to an input pin inside a MetaSound graph. Pins are addressed by vertex name "
			"(the label shown in the MetaSound editor). Types must be compatible. Marks the asset dirty."))
		.StringArg(TEXT("metasound_path"), TEXT("Asset path."), true)
		.StringArg(TEXT("from_node"), TEXT("Source node GUID."), true)
		.StringArg(TEXT("from_pin"), TEXT("Source output vertex name."), true)
		.StringArg(TEXT("to_node"), TEXT("Destination node GUID."), true)
		.StringArg(TEXT("to_pin"), TEXT("Destination input vertex name."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString AssetPath, FromNode, FromPin, ToNode, ToPin;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("metasound_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("from_node"), FromNode));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("from_pin"), FromPin));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_node"), ToNode));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_pin"), ToPin));

			UObject* Asset = nullptr;
			TScriptInterface<IMetaSoundDocumentInterface> Doc = LoadMetaSoundDocument(AssetPath, Asset);
			if (!Asset) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			if (!Doc) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("'%s' is not a MetaSound document"), *AssetPath));

			FMetaSoundFrontendDocumentBuilder Builder(Doc);
			FGuid FromGuid, ToGuid;
			if (!FindNode(Builder, FromNode, FromGuid)) return NodeNotFound(FromNode);
			if (!FindNode(Builder, ToNode, ToGuid)) return NodeNotFound(ToNode);

			const FMetasoundFrontendVertex* OutV = Builder.FindNodeOutput(FromGuid, FName(*FromPin));
			const FMetasoundFrontendVertex* InV  = Builder.FindNodeInput(ToGuid, FName(*ToPin));
			if (!OutV) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Output pin '%s' not found on node %s"), *FromPin, *FromNode));
			if (!InV) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Input pin '%s' not found on node %s"), *ToPin, *ToNode));

			FMetasoundFrontendEdge Edge;
			Edge.FromNodeID = FromGuid;
			Edge.FromVertexID = OutV->VertexID;
			Edge.ToNodeID = ToGuid;
			Edge.ToVertexID = InV->VertexID;

			// AddEdge() returns void; CanAddEdge() validates type compatibility and
			// that the destination input isn't already driven.
			if (!Builder.CanAddEdge(Edge))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Could not connect %s.%s -> %s.%s (type mismatch or input already connected)."),
						*FromNode, *FromPin, *ToNode, *ToPin),
					TEXT("MetaSound inputs accept a single connection; disconnect first, and ensure data types match."));
			}
			Builder.AddEdge(MoveTemp(Edge));
			Asset->MarkPackageDirty();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("metasound_path"), AssetPath);
			R->SetStringField(TEXT("from"), FString::Printf(TEXT("%s.%s"), *FromNode, *FromPin));
			R->SetStringField(TEXT("to"), FString::Printf(TEXT("%s.%s"), *ToNode, *ToPin));
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Connected %s.%s -> %s.%s."), *FromNode, *FromPin, *ToNode, *ToPin), R);
		});

	// ================================================================
	// metasound_disconnect_pin  — v4.5 implemented
	// ================================================================
	MCP_TOOL(Registry, "metasound_disconnect_pin")
		.Description(TEXT(
			"Remove the connection feeding a destination input pin inside a MetaSound graph. Marks dirty."))
		.StringArg(TEXT("metasound_path"), TEXT("Asset path."), true)
		.StringArg(TEXT("to_node"), TEXT("Destination node GUID."), true)
		.StringArg(TEXT("to_pin"), TEXT("Destination input vertex name to clear."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString AssetPath, ToNode, ToPin;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("metasound_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_node"), ToNode));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("to_pin"), ToPin));

			UObject* Asset = nullptr;
			TScriptInterface<IMetaSoundDocumentInterface> Doc = LoadMetaSoundDocument(AssetPath, Asset);
			if (!Asset) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			if (!Doc) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("'%s' is not a MetaSound document"), *AssetPath));

			FMetaSoundFrontendDocumentBuilder Builder(Doc);
			FGuid ToGuid;
			if (!FindNode(Builder, ToNode, ToGuid)) return NodeNotFound(ToNode);

			const FMetasoundFrontendVertex* InV = Builder.FindNodeInput(ToGuid, FName(*ToPin));
			if (!InV) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Input pin '%s' not found on node %s"), *ToPin, *ToNode));

			const bool bRemoved = Builder.RemoveEdgeToNodeInput(ToGuid, InV->VertexID);
			Asset->MarkPackageDirty();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("metasound_path"), AssetPath);
			R->SetStringField(TEXT("to"), FString::Printf(TEXT("%s.%s"), *ToNode, *ToPin));
			R->SetBoolField(TEXT("removed"), bRemoved);
			return FMCPToolResult::SuccessStructured(
				bRemoved
					? FString::Printf(TEXT("Disconnected input %s.%s."), *ToNode, *ToPin)
					: FString::Printf(TEXT("Input %s.%s had no connection."), *ToNode, *ToPin), R);
		});

	// ================================================================
	// metasound_set_node_property  — v4.5 implemented (input literal default)
	// ================================================================
	MCP_TOOL(Registry, "metasound_set_node_property")
		.Description(TEXT(
			"Set the literal default value on a node input pin. The value string is coerced to the input's "
			"data type (float / int / bool / string heuristic). Marks the asset dirty."))
		.StringArg(TEXT("metasound_path"), TEXT("Asset path."), true)
		.StringArg(TEXT("node_id"), TEXT("Node GUID."), true)
		.StringArg(TEXT("property_name"), TEXT("Input vertex name to set the default on."), true)
		.StringArg(TEXT("value"), TEXT("Value as a string; coerced by the MetaSound type system."), true)
		.SupportsDryRun()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireMetaSound());

			FString AssetPath, NodeIdStr, PropName, Value;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("metasound_path"), AssetPath));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("node_id"), NodeIdStr));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("property_name"), PropName));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("value"), Value));

			UObject* Asset = nullptr;
			TScriptInterface<IMetaSoundDocumentInterface> Doc = LoadMetaSoundDocument(AssetPath, Asset);
			if (!Asset) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
			if (!Doc) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
				FString::Printf(TEXT("'%s' is not a MetaSound document"), *AssetPath));

			FMetaSoundFrontendDocumentBuilder Builder(Doc);
			FGuid NodeGuid;
			if (!FindNode(Builder, NodeIdStr, NodeGuid)) return NodeNotFound(NodeIdStr);

			const FMetasoundFrontendVertex* InV = Builder.FindNodeInput(NodeGuid, FName(*PropName));
			if (!InV) return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
				FString::Printf(TEXT("Input pin '%s' not found on node %s"), *PropName, *NodeIdStr));

			// Build a literal from the string. Heuristic coercion: bool -> int -> float -> string.
			FMetasoundFrontendLiteral Literal;
			if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
			{
				Literal.Set(Value.Equals(TEXT("true"), ESearchCase::IgnoreCase));
			}
			else if (Value.IsNumeric() && !Value.Contains(TEXT(".")))
			{
				Literal.Set((int32)FCString::Atoi(*Value));
			}
			else if (Value.IsNumeric())
			{
				Literal.Set((float)FCString::Atod(*Value));
			}
			else
			{
				Literal.Set(Value);
			}

			if (!Builder.SetNodeInputDefault(NodeGuid, InV->VertexID, Literal))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Could not set default on %s.%s (type mismatch?)."), *NodeIdStr, *PropName),
					TEXT("The value's coerced literal type must match the input's data type."));
			}
			Asset->MarkPackageDirty();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("metasound_path"), AssetPath);
			R->SetStringField(TEXT("node_id"), NodeGuid.ToString());
			R->SetStringField(TEXT("property"), PropName);
			R->SetStringField(TEXT("value"), Value);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Set %s.%s = %s."), *NodeIdStr, *PropName, *Value), R);
		});
}

} // namespace MCPMetaSoundGraphTools
