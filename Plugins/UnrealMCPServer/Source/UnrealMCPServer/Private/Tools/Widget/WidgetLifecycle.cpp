// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "UObject/SavePackage.h"

// UMG Widget Blueprint editing
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/WidgetTree.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetToolsModule.h"
#include "Misc/PackageName.h"
#include "Engine/Texture2D.h"

// Blueprint event graph (for bind_widget_event)
#include "K2Node_ComponentBoundEvent.h"
#include "EdGraphSchema_K2.h"

// UMG Widget types
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/GridPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/WrapBox.h"
#include "Components/UniformGridPanel.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/EditableTextBox.h"
#include "Components/Slider.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/Spacer.h"
#include "Components/RichTextBlock.h"
#include "Components/ScrollBox.h"
#include "Components/PanelWidget.h"

#include "Tools/Widget/WidgetCommon.h"
#include "Common/MCPAssetCreate.h"
#include "WidgetBlueprintOperationUtils.h"
#include "Engine/BlueprintGeneratedClass.h"

namespace MCPWidgetTools::Lifecycle
{

using namespace MCPWidgetTools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_widget_blueprints - List all Widget Blueprint assets
	// ================================================================
	MCP_TOOL(Registry, "list_widget_blueprints")
		.ReadOnly()
		.Description(TEXT("List all Widget Blueprint (UMG) assets in the project. Returns asset name, content path, and parent class for each widget."))
		.StringArg(TEXT("path"), TEXT("Content path to search (e.g., '/Game/'). Default: '/Game/'"))
		.StringArg(TEXT("name_filter"), TEXT("Filter by asset name (substring match)"))
		.IntArg(TEXT("limit"), TEXT("Maximum number of results (default: 100)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

			FString Path = TEXT("/Game/");
			Args->TryGetStringField(TEXT("path"), Path);

			FString NameFilter;
			Args->TryGetStringField(TEXT("name_filter"), NameFilter);

			int32 Limit = 100;
			if (Args->HasField(TEXT("limit")))
			{
				Limit = FMath::Clamp((int32)Args->GetNumberField(TEXT("limit")), 1, 5000);
			}

			FTopLevelAssetPath WidgetBPClassPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint"));
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(WidgetBPClassPath, Assets, true);

			TArray<FString> Results;
			int32 TotalMatching = 0;

			for (const FAssetData& Asset : Assets)
			{
				if (!Asset.PackageName.ToString().StartsWith(Path)) continue;
				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter)) continue;

				TotalMatching++;
				if (Results.Num() < Limit)
				{
					FString ParentClass = TEXT("UserWidget");
					FAssetTagValueRef ParentClassTag = Asset.TagsAndValues.FindTag(TEXT("ParentClass"));
					if (ParentClassTag.IsSet())
					{
						ParentClass = ParentClassTag.AsString();
					}

					TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
					Obj->SetStringField(TEXT("name"), Asset.AssetName.ToString());
					Obj->SetStringField(TEXT("path"), Asset.GetObjectPathString());
					Obj->SetStringField(TEXT("package"), Asset.PackageName.ToString());
					Obj->SetStringField(TEXT("parent_class"), ParentClass);

					Results.Add(JsonToString(Obj));
				}
			}

			if (Results.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No Widget Blueprints found in '%s'%s"),
					*Path,
					NameFilter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" matching '%s'"), *NameFilter)));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Found %d Widget Blueprint(s) (showing %d):\n[%s]"),
				TotalMatching, Results.Num(), *FString::Join(Results, TEXT(",\n"))));
		});
	// ================================================================
	// spawn_widget_component - Add a WidgetComponent to an existing actor
	// ================================================================
	MCP_TOOL(Registry, "spawn_widget_component")
		.Description(TEXT("Add a WidgetComponent to an existing actor for in-world UI display. The component can render a Widget Blueprint in 3D world space or screen space. Returns the component name on success."))
		.StringArg(TEXT("actor_name"), TEXT("Label of the target actor to add the WidgetComponent to"), true)
		.StringArg(TEXT("widget_class_path"), TEXT("Optional content path to a Widget Blueprint asset (e.g., '/Game/UI/WBP_HUD.WBP_HUD')"))
		.NumberArg(TEXT("draw_size_x"), TEXT("Width of the widget in world units (default: 500)"))
		.NumberArg(TEXT("draw_size_y"), TEXT("Height of the widget in world units (default: 500)"))
		.NumberArg(TEXT("relative_x"), TEXT("X offset from actor root (default: 0)"))
		.NumberArg(TEXT("relative_y"), TEXT("Y offset from actor root (default: 0)"))
		.NumberArg(TEXT("relative_z"), TEXT("Z offset from actor root (default: 0)"))
		.EnumArg(TEXT("space"), TEXT("Render space for the widget. 'World' renders in 3D world space, 'Screen' renders as a screen-space overlay. Default: 'World'"),
			{ TEXT("World"), TEXT("Screen") })
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			float DrawSizeX = 500.0f;
			float DrawSizeY = 500.0f;
			if (Args->HasField(TEXT("draw_size_x"))) DrawSizeX = (float)Args->GetNumberField(TEXT("draw_size_x"));
			if (Args->HasField(TEXT("draw_size_y"))) DrawSizeY = (float)Args->GetNumberField(TEXT("draw_size_y"));

			FVector RelativeOffset(
				Args->HasField(TEXT("relative_x")) ? Args->GetNumberField(TEXT("relative_x")) : 0.0,
				Args->HasField(TEXT("relative_y")) ? Args->GetNumberField(TEXT("relative_y")) : 0.0,
				Args->HasField(TEXT("relative_z")) ? Args->GetNumberField(TEXT("relative_z")) : 0.0
			);

			FString SpaceStr;
			Args->TryGetStringField(TEXT("space"), SpaceStr);
			EWidgetSpace WidgetSpace = EWidgetSpace::World;
			if (SpaceStr == TEXT("Screen")) WidgetSpace = EWidgetSpace::Screen;

			TSubclassOf<UUserWidget> WidgetClass = nullptr;
			FString WidgetClassPath;
			if (Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath) && !WidgetClassPath.IsEmpty())
			{
				UBlueprint* WidgetBP = LoadObject<UBlueprint>(nullptr, *WidgetClassPath);
				UClass* SpawnGenClass = WidgetBP ? WidgetBP->GeneratedClass.Get() : nullptr;
				if (SpawnGenClass && SpawnGenClass->IsChildOf(UUserWidget::StaticClass()))
				{
					WidgetClass = TSubclassOf<UUserWidget>(SpawnGenClass);
				}
				else
				{
					FString GeneratedClassPath = WidgetClassPath + TEXT("_C");
					UClass* LoadedClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
					if (LoadedClass && LoadedClass->IsChildOf(UUserWidget::StaticClass()))
					{
						WidgetClass = LoadedClass;
					}
					else
					{
						return FMCPToolResult::Error(FString::Printf(
							TEXT("Could not load Widget Blueprint class from: %s"), *WidgetClassPath));
					}
				}
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Spawn Widget Component")));
			Actor->Modify();

			UWidgetComponent* WidgetComp = NewObject<UWidgetComponent>(Actor, UWidgetComponent::StaticClass(),
				MakeUniqueObjectName(Actor, UWidgetComponent::StaticClass(), TEXT("WidgetComponent")),
				RF_Transactional);

			if (!WidgetComp)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create WidgetComponent"));
			}

			WidgetComp->SetWidgetSpace(WidgetSpace);
			WidgetComp->SetDrawSize(FVector2D(DrawSizeX, DrawSizeY));

			if (WidgetClass)
			{
				WidgetComp->SetWidgetClass(WidgetClass);
			}

			WidgetComp->RegisterComponent();
			WidgetComp->AttachToComponent(
				Actor->GetRootComponent(),
				FAttachmentTransformRules::KeepRelativeTransform);
			WidgetComp->SetRelativeLocation(RelativeOffset);

			Actor->AddInstanceComponent(WidgetComp);
			Actor->RerunConstructionScripts();

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Added WidgetComponent '%s' to actor '%s'. DrawSize=(%.0f x %.0f), Space=%s%s"),
				*WidgetComp->GetName(),
				*ActorName,
				DrawSizeX,
				DrawSizeY,
				WidgetSpace == EWidgetSpace::World ? TEXT("World") : TEXT("Screen"),
				WidgetClass ? *FString::Printf(TEXT(", WidgetClass=%s"), *WidgetClass->GetName()) : TEXT("")));
		});
	// ================================================================
	// create_widget_blueprint - Create a new Widget Blueprint
	// ================================================================
	MCP_TOOL(Registry, "create_widget_blueprint")
		.Description(TEXT("Create a new Widget Blueprint (UMG) asset with a specified root panel type. The Widget Blueprint is saved and ready for editing with add_widget, set_widget_properties, etc."))
		.StringArg(TEXT("asset_path"), TEXT("Content path for the new Widget Blueprint (e.g., '/Game/UI/WBP_MainMenu')"), true)
		.EnumArg(TEXT("root_widget_type"), TEXT("Root panel widget type (default: CanvasPanel)"),
			{ TEXT("CanvasPanel"), TEXT("VerticalBox"), TEXT("HorizontalBox"), TEXT("Overlay"), TEXT("GridPanel") })
		.StringArg(TEXT("parent_class"), TEXT("Parent UserWidget class: '/Script/Game.MyHUDBase', a short native class name ('CommonActivatableWidget'), or another Widget Blueprint asset path. Default: UserWidget."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath))
				return FMCPToolResult::Error(TEXT("asset_path is required"));

			FString RootType = TEXT("CanvasPanel");
			Args->TryGetStringField(TEXT("root_widget_type"), RootType);

			UClass* ParentClass = UUserWidget::StaticClass();
			FString ParentClassStr;
			if (Args->TryGetStringField(TEXT("parent_class"), ParentClassStr) && !ParentClassStr.IsEmpty())
			{
				FString Err;
				ParentClass = ResolveUserWidgetClass(ParentClassStr, Err);
				if (!ParentClass) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, Err);
			}

			UClass* RootClass = WidgetClassFromTypeName(RootType);
			if (!RootClass)
				return FMCPToolResult::Error(FString::Printf(TEXT("Unknown root widget type: %s"), *RootType));

			FString PackagePath, AssetName;
			FMCPToolResult PackageError;
			UPackage* Package = MCPCommon::CreateAssetPackage(AssetPath, PackagePath, AssetName, PackageError);
			if (!Package)
				return PackageError;

			UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
			Factory->ParentClass = ParentClass;

			UWidgetBlueprint* NewWBP = Cast<UWidgetBlueprint>(Factory->FactoryCreateNew(
				UWidgetBlueprint::StaticClass(), Package, FName(*AssetName),
				RF_Public | RF_Standalone, nullptr, GWarn));

			if (!NewWBP)
				return FMCPToolResult::Error(TEXT("Failed to create Widget Blueprint"));

			// Set the root widget
			if (NewWBP->WidgetTree)
			{
				UWidget* RootWidget = NewWBP->WidgetTree->ConstructWidget<UWidget>(RootClass, FName(TEXT("RootPanel")));
				if (RootWidget)
				{
					NewWBP->WidgetTree->RootWidget = RootWidget;
				}
			}

			SaveWidgetBlueprint(NewWBP);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created Widget Blueprint '%s' at %s with root widget type '%s' (parent class: %s)"),
				*AssetName, *AssetPath, *RootType, *ParentClass->GetName()));
		});

	// ================================================================
	// reparent_widget_blueprint - Change the parent class
	// ================================================================
	MCP_TOOL(Registry, "reparent_widget_blueprint")
		.Description(TEXT("Change the parent class of a Widget Blueprint (File > Reparent Blueprint), e.g. to a C++ base with BindWidget members, or to CommonActivatableWidget. Nodes are refreshed and the Blueprint recompiled; compile messages are returned so a broken reparent is visible. Python has no reparent API."))
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.StringArg(TEXT("parent_class"), TEXT("New parent: '/Script/Game.MyHUDBase', a short native class name, or another Widget Blueprint asset path"), true)
		.BoolArg(TEXT("save"), TEXT("Compile and save the asset to disk after the change (default true)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath, ParentClassStr;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			if (!Args->TryGetStringField(TEXT("parent_class"), ParentClassStr)) return FMCPToolResult::Error(TEXT("parent_class is required"));

			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			FString Err;
			UClass* NewParent = ResolveUserWidgetClass(ParentClassStr, Err);
			if (!NewParent) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, Err);

			if (NewParent == WBP->GeneratedClass || (WBP->GeneratedClass && NewParent->IsChildOf(WBP->GeneratedClass)))
				return FMCPToolResult::Error(TEXT("Cannot reparent a Blueprint to itself or to one of its own subclasses"));
			if (WBP->ParentClass == NewParent)
				return FMCPToolResult::Success(FString::Printf(TEXT("%s already derives from %s"), *WBP->GetName(), *NewParent->GetName()));

			const FString OldParent = WBP->ParentClass ? WBP->ParentClass->GetName() : TEXT("(none)");

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Reparent Widget Blueprint")));
			WBP->Modify();
			WBP->ParentClass = NewParent;
			FBlueprintEditorUtils::RefreshAllNodes(WBP);
			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
			GEditor->EndTransaction();

			TSharedPtr<FJsonObject> Compile = CompileWidgetBlueprint(WBP, WantsSave(Args));
			FBlueprintEditorUtils::RefreshExternalBlueprintDependencyNodes(WBP);

			bool bCompiled = false;
			Compile->TryGetBoolField(TEXT("compiled"), bCompiled);
			Compile->SetStringField(TEXT("old_parent_class"), OldParent);
			Compile->SetStringField(TEXT("new_parent_class"), NewParent->GetName());

			return FMCPToolResult::SuccessStructured(FString::Printf(
				TEXT("Reparented %s: %s -> %s (%s)"), *WBP->GetName(), *OldParent, *NewParent->GetName(),
				bCompiled ? TEXT("compiles cleanly") : TEXT("COMPILE ERRORS - see compile.messages")), Compile);
		});

	// ================================================================
	// compile_widget_blueprint - Closed-loop verification
	// ================================================================
	MCP_TOOL(Registry, "compile_widget_blueprint")
		.Description(TEXT("Compile a Widget Blueprint and report errors/warnings plus the widget and animation member variables that exist on the generated class - use it to verify is_variable changes, bindings and reparenting. Optionally saves."))
		.Idempotent()
		.StringArg(TEXT("asset_path"), TEXT("Content path of the Widget Blueprint"), true)
		.BoolArg(TEXT("save"), TEXT("Save the asset to disk after a successful compile (default false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString AssetPath;
			if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath)) return FMCPToolResult::Error(TEXT("asset_path is required"));
			UWidgetBlueprint* WBP = FindWidgetBlueprint(AssetPath);
			if (!WBP) return FMCPToolResult::Error(FString::Printf(TEXT("Widget Blueprint not found: %s"), *AssetPath));

			bool bSave = false;
			Args->TryGetBoolField(TEXT("save"), bSave);

			TSharedPtr<FJsonObject> Result = CompileWidgetBlueprint(WBP, bSave);
			bool bCompiled = false;
			Result->TryGetBoolField(TEXT("compiled"), bCompiled);
			if (!bCompiled)
			{
				FString Text = FString::Printf(TEXT("%s has compile errors (%d error(s), %d warning(s)):"),
					*WBP->GetName(), (int32)Result->GetNumberField(TEXT("error_count")), (int32)Result->GetNumberField(TEXT("warning_count")));
				const TArray<TSharedPtr<FJsonValue>>* Msgs = nullptr;
				if (Result->TryGetArrayField(TEXT("messages"), Msgs))
				{
					for (const TSharedPtr<FJsonValue>& V : *Msgs)
					{
						TSharedPtr<FJsonObject> M = V->AsObject();
						if (M) Text += FString::Printf(TEXT("\n  [%s] %s"), *M->GetStringField(TEXT("severity")), *M->GetStringField(TEXT("text")));
					}
				}
				FMCPToolResult R = FMCPToolResult::ErrorStructured(EMCPError::Internal, Text);
				if (R.StructuredContent.IsValid()) R.StructuredContent->SetObjectField(TEXT("compile"), Result); else R.StructuredContent = Result;
				return R;
			}
			return FMCPToolResult::SuccessStructured(JsonToString(Result), Result);
		});
}

} // namespace MCPWidgetTools::Lifecycle
