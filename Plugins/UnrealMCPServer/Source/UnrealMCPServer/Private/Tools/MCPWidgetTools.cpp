#include "Tools/MCPWidgetTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/UserWidget.h"
#include "UObject/SavePackage.h"

namespace MCPWidgetTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor) return GEditor->GetEditorWorldContext().World();
	return nullptr;
}

static AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label) return *It;
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// list_widget_blueprints - List all Widget Blueprint assets
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("path"), TEXT("Content path to search (e.g., '/Game/'). Default: '/Game/'"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("name_filter"), TEXT("Filter by asset name (substring match)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("limit"), TEXT("Maximum number of results (default: 100)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("list_widget_blueprints");
		Def.Description = TEXT("List all Widget Blueprint (UMG) assets in the project. Returns asset name, content path, and parent class for each widget.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
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

			// Use FTopLevelAssetPath to query the asset registry for WidgetBlueprint assets
			FTopLevelAssetPath WidgetBPClassPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint"));
			TArray<FAssetData> Assets;
			AssetRegistry.GetAssetsByClass(WidgetBPClassPath, Assets, true);

			TArray<FString> Results;
			int32 TotalMatching = 0;

			for (const FAssetData& Asset : Assets)
			{
				// Filter by path
				if (!Asset.PackageName.ToString().StartsWith(Path)) continue;

				// Filter by name
				if (!NameFilter.IsEmpty() && !Asset.AssetName.ToString().Contains(NameFilter)) continue;

				TotalMatching++;
				if (Results.Num() < Limit)
				{
					// Attempt to read the parent class from asset tags
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
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// spawn_widget_component - Add a WidgetComponent to an existing actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the target actor to add the WidgetComponent to"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_class_path"), TEXT("Optional content path to a Widget Blueprint asset (e.g., '/Game/UI/WBP_HUD.WBP_HUD')"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_x"), TEXT("Width of the widget in world units (default: 500)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_y"), TEXT("Height of the widget in world units (default: 500)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("relative_x"), TEXT("X offset from actor root (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("relative_y"), TEXT("Y offset from actor root (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("relative_z"), TEXT("Z offset from actor root (default: 0)"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("space"), TEXT("Render space for the widget. 'World' renders in 3D world space, 'Screen' renders as a screen-space overlay. Default: 'World'"),
			{ TEXT("World"), TEXT("Screen") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("spawn_widget_component");
		Def.Description = TEXT("Add a WidgetComponent to an existing actor for in-world UI display. The component can render a Widget Blueprint in 3D world space or screen space. Returns the component name on success.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			// Draw size
			float DrawSizeX = 500.0f;
			float DrawSizeY = 500.0f;
			if (Args->HasField(TEXT("draw_size_x"))) DrawSizeX = (float)Args->GetNumberField(TEXT("draw_size_x"));
			if (Args->HasField(TEXT("draw_size_y"))) DrawSizeY = (float)Args->GetNumberField(TEXT("draw_size_y"));

			// Relative offset
			FVector RelativeOffset(
				Args->HasField(TEXT("relative_x")) ? Args->GetNumberField(TEXT("relative_x")) : 0.0,
				Args->HasField(TEXT("relative_y")) ? Args->GetNumberField(TEXT("relative_y")) : 0.0,
				Args->HasField(TEXT("relative_z")) ? Args->GetNumberField(TEXT("relative_z")) : 0.0
			);

			// Space
			FString SpaceStr;
			Args->TryGetStringField(TEXT("space"), SpaceStr);
			EWidgetSpace WidgetSpace = EWidgetSpace::World;
			if (SpaceStr == TEXT("Screen")) WidgetSpace = EWidgetSpace::Screen;

			// Optionally load the widget class from a content path
			TSubclassOf<UUserWidget> WidgetClass = nullptr;
			FString WidgetClassPath;
			if (Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath) && !WidgetClassPath.IsEmpty())
			{
				// Load the Blueprint object and get its generated class
				UBlueprint* WidgetBP = LoadObject<UBlueprint>(nullptr, *WidgetClassPath);
				UClass* SpawnGenClass = WidgetBP ? WidgetBP->GeneratedClass.Get() : nullptr;
				if (SpawnGenClass && SpawnGenClass->IsChildOf(UUserWidget::StaticClass()))
				{
					WidgetClass = TSubclassOf<UUserWidget>(SpawnGenClass);
				}
				else
				{
					// Fallback: try loading the _C generated class directly
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

			// Create the WidgetComponent
			UWidgetComponent* WidgetComp = NewObject<UWidgetComponent>(Actor, UWidgetComponent::StaticClass(),
				MakeUniqueObjectName(Actor, UWidgetComponent::StaticClass(), TEXT("WidgetComponent")),
				RF_Transactional);

			if (!WidgetComp)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to create WidgetComponent"));
			}

			// Configure properties
			WidgetComp->SetWidgetSpace(WidgetSpace);
			WidgetComp->SetDrawSize(FVector2D(DrawSizeX, DrawSizeY));

			if (WidgetClass)
			{
				WidgetComp->SetWidgetClass(WidgetClass);
			}

			// Register and attach to actor root
			WidgetComp->RegisterComponent();
			WidgetComp->AttachToComponent(
				Actor->GetRootComponent(),
				FAttachmentTransformRules::KeepRelativeTransform);
			WidgetComp->SetRelativeLocation(RelativeOffset);

			// Notify editor of change
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
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_widget_component_property - Set properties on a WidgetComponent
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor that owns the WidgetComponent"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_x"), TEXT("New draw width in world units"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("draw_size_y"), TEXT("New draw height in world units"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("widget_class_path"), TEXT("Content path to a Widget Blueprint to assign as the widget class (e.g., '/Game/UI/WBP_HUD.WBP_HUD')"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("space"), TEXT("Render space: 'World' for 3D world space, 'Screen' for screen-space overlay"),
			{ TEXT("World"), TEXT("Screen") });
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_r"), TEXT("Tint color red channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_g"), TEXT("Tint color green channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_b"), TEXT("Tint color blue channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("tint_a"), TEXT("Tint color alpha channel (0.0 - 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("max_interaction_distance"), TEXT("Maximum distance at which the player can interact with this widget (in world units)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("is_two_sided"), TEXT("Whether the widget geometry is rendered two-sided"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_widget_component_property");
		Def.Description = TEXT("Set one or more properties on the first WidgetComponent found on an actor. Only provided fields are modified. Supports draw size, widget class, render space, tint color, interaction distance, and two-sided rendering.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor)
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UWidgetComponent* WidgetComp = Actor->FindComponentByClass<UWidgetComponent>();
			if (!WidgetComp)
				return FMCPToolResult::Error(FString::Printf(
					TEXT("No WidgetComponent found on actor '%s'. Use spawn_widget_component to add one first."), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Widget Component Property")));
			WidgetComp->Modify();

			TArray<FString> ChangedProps;

			// Draw size - update both axes together if either is provided
			bool bHasDrawX = Args->HasField(TEXT("draw_size_x"));
			bool bHasDrawY = Args->HasField(TEXT("draw_size_y"));
			if (bHasDrawX || bHasDrawY)
			{
				FVector2D CurrentSize = WidgetComp->GetDrawSize();
				float NewX = bHasDrawX ? (float)Args->GetNumberField(TEXT("draw_size_x")) : CurrentSize.X;
				float NewY = bHasDrawY ? (float)Args->GetNumberField(TEXT("draw_size_y")) : CurrentSize.Y;
				WidgetComp->SetDrawSize(FVector2D(NewX, NewY));
				ChangedProps.Add(FString::Printf(TEXT("DrawSize=(%.0f x %.0f)"), NewX, NewY));
			}

			// Widget class
			FString WidgetClassPath;
			if (Args->TryGetStringField(TEXT("widget_class_path"), WidgetClassPath) && !WidgetClassPath.IsEmpty())
			{
				UBlueprint* WidgetBP = LoadObject<UBlueprint>(nullptr, *WidgetClassPath);
				UClass* GenClass = WidgetBP ? WidgetBP->GeneratedClass.Get() : nullptr;
				if (GenClass && GenClass->IsChildOf(UUserWidget::StaticClass()))
				{
					WidgetComp->SetWidgetClass(TSubclassOf<UUserWidget>(GenClass));
					ChangedProps.Add(FString::Printf(TEXT("WidgetClass=%s"), *WidgetBP->GetName()));
				}
				else
				{
					// Fallback: try the _C generated class path
					FString GeneratedClassPath = WidgetClassPath + TEXT("_C");
					UClass* LoadedClass = LoadObject<UClass>(nullptr, *GeneratedClassPath);
					if (LoadedClass && LoadedClass->IsChildOf(UUserWidget::StaticClass()))
					{
						TSubclassOf<UUserWidget> WidgetClass = LoadedClass;
						WidgetComp->SetWidgetClass(WidgetClass);
						ChangedProps.Add(FString::Printf(TEXT("WidgetClass=%s"), *LoadedClass->GetName()));
					}
					else
					{
						GEditor->EndTransaction();
						return FMCPToolResult::Error(FString::Printf(
							TEXT("Could not load Widget Blueprint class from: %s"), *WidgetClassPath));
					}
				}
			}

			// Render space
			FString SpaceStr;
			if (Args->TryGetStringField(TEXT("space"), SpaceStr))
			{
				EWidgetSpace NewSpace = (SpaceStr == TEXT("Screen")) ? EWidgetSpace::Screen : EWidgetSpace::World;
				WidgetComp->SetWidgetSpace(NewSpace);
				ChangedProps.Add(FString::Printf(TEXT("Space=%s"), *SpaceStr));
			}

			// Tint color - apply if any channel was provided
			bool bHasR = Args->HasField(TEXT("tint_r"));
			bool bHasG = Args->HasField(TEXT("tint_g"));
			bool bHasB = Args->HasField(TEXT("tint_b"));
			bool bHasA = Args->HasField(TEXT("tint_a"));
			if (bHasR || bHasG || bHasB || bHasA)
			{
				FLinearColor NewTint(
					bHasR ? (float)Args->GetNumberField(TEXT("tint_r")) : 1.0f,
					bHasG ? (float)Args->GetNumberField(TEXT("tint_g")) : 1.0f,
					bHasB ? (float)Args->GetNumberField(TEXT("tint_b")) : 1.0f,
					bHasA ? (float)Args->GetNumberField(TEXT("tint_a")) : 1.0f
				);
				WidgetComp->SetTintColorAndOpacity(NewTint);
				ChangedProps.Add(FString::Printf(TEXT("Tint=(R=%.2f G=%.2f B=%.2f A=%.2f)"),
					NewTint.R, NewTint.G, NewTint.B, NewTint.A));
			}

			// Two-sided
			bool bTwoSided = false;
			if (Args->TryGetBoolField(TEXT("is_two_sided"), bTwoSided))
			{
				WidgetComp->SetTwoSided(bTwoSided);
				ChangedProps.Add(FString::Printf(TEXT("TwoSided=%s"), bTwoSided ? TEXT("true") : TEXT("false")));
			}

			GEditor->EndTransaction();

			if (ChangedProps.Num() == 0)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("No properties changed on WidgetComponent of '%s'. Provide at least one optional property to modify."),
					*ActorName));
			}

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Updated WidgetComponent on '%s': %s"),
				*ActorName,
				*FString::Join(ChangedProps, TEXT(", "))));
		});
		Def.bIdempotentHint = true;
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPWidgetTools
