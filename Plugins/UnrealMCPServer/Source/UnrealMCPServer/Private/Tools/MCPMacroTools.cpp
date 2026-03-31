#include "Tools/MCPMacroTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Engine/World.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TriggerBox.h"
#include "Components/BoxComponent.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Materials/MaterialInterface.h"

namespace MCPMacroTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// create_basic_level - Creates a complete basic level with essentials
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("floor_size"), TEXT("Size of the floor plane in cm (default: 5000). The floor is scaled uniformly from the 100x100 engine Plane mesh."));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("add_player_start"), TEXT("Add a PlayerStart actor (default: true)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("add_nav_mesh"), TEXT("Add a NavMeshBoundsVolume covering the floor (default: true)"));
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("lighting_preset"), TEXT("Lighting preset"),
			{ TEXT("Day"), TEXT("Night"), TEXT("Sunset"), TEXT("Indoor") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_basic_level");
		Def.Description = TEXT("Creates a complete basic level with all essentials: floor plane, DirectionalLight, SkyLight, SkyAtmosphere, PostProcessVolume (infinite extent), ExponentialHeightFog, PlayerStart, and NavMeshBoundsVolume. Configures lighting based on preset (Day/Night/Sunset/Indoor).");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = false;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			double FloorSize = Args->HasField(TEXT("floor_size")) ? Args->GetNumberField(TEXT("floor_size")) : 5000.0;
			bool bAddPlayerStart = true;
			Args->TryGetBoolField(TEXT("add_player_start"), bAddPlayerStart);
			bool bAddNavMesh = true;
			Args->TryGetBoolField(TEXT("add_nav_mesh"), bAddNavMesh);

			FString LightingPreset = TEXT("Day");
			Args->TryGetStringField(TEXT("lighting_preset"), LightingPreset);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Basic Level")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			TArray<FString> CreatedActors;

			// --- Floor Plane ---
			{
				UStaticMesh* FloorMesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")));
				if (FloorMesh)
				{
					FVector FloorLoc(0, 0, 0);
					FRotator FloorRot(0, 0, 0);
					AStaticMeshActor* FloorActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FloorLoc, FloorRot, SpawnParams);
					if (FloorActor)
					{
						// Engine Plane is 100x100 units, scale to desired size
						double ScaleFactor = FloorSize / 100.0;
						FloorActor->SetActorScale3D(FVector(ScaleFactor, ScaleFactor, 1.0));
						FloorActor->GetStaticMeshComponent()->SetStaticMesh(FloorMesh);
						FloorActor->SetActorLabel(TEXT("Floor"));
						FloorActor->SetFolderPath(FName(TEXT("Level")));
						CreatedActors.Add(TEXT("Floor"));
					}
				}
			}

			// --- Directional Light (Sun/Moon) ---
			{
				FRotator LightRot(-50.0, -30.0, 0.0); // Default sun angle
				float LightIntensity = 10.0f;
				FLinearColor LightColor = FLinearColor(1.0f, 0.95f, 0.85f); // warm sun

				if (LightingPreset == TEXT("Night"))
				{
					LightRot = FRotator(-30.0, 120.0, 0.0);
					LightIntensity = 0.5f;
					LightColor = FLinearColor(0.4f, 0.5f, 0.8f); // blue moonlight
				}
				else if (LightingPreset == TEXT("Sunset"))
				{
					LightRot = FRotator(-10.0, -60.0, 0.0);
					LightIntensity = 8.0f;
					LightColor = FLinearColor(1.0f, 0.6f, 0.3f); // warm orange
				}
				else if (LightingPreset == TEXT("Indoor"))
				{
					LightRot = FRotator(-70.0, 0.0, 0.0);
					LightIntensity = 3.0f;
					LightColor = FLinearColor(1.0f, 0.98f, 0.95f); // neutral white
				}

				FVector LightLoc(0, 0, 500);
				ADirectionalLight* DirLight = World->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), LightLoc, LightRot, SpawnParams);
				if (DirLight)
				{
					UDirectionalLightComponent* LightComp = Cast<UDirectionalLightComponent>(DirLight->GetLightComponent());
					if (LightComp)
					{
						LightComp->SetIntensity(LightIntensity);
						LightComp->SetLightColor(LightColor);
						LightComp->SetCastShadows(true);
					}
					DirLight->SetActorLabel(TEXT("DirectionalLight"));
					DirLight->SetFolderPath(FName(TEXT("Level/Lighting")));
					CreatedActors.Add(TEXT("DirectionalLight"));
				}
			}

			// --- Sky Light ---
			{
				FVector SkyLightLoc(0, 0, 300);
				FRotator SkyLightRot(0, 0, 0);
				ASkyLight* SkyLightActor = World->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), SkyLightLoc, SkyLightRot, SpawnParams);
				if (SkyLightActor)
				{
					USkyLightComponent* SkyComp = SkyLightActor->GetLightComponent();
					if (SkyComp)
					{
						if (LightingPreset == TEXT("Night"))
						{
							SkyComp->Intensity = 0.5f;
						}
						else if (LightingPreset == TEXT("Indoor"))
						{
							SkyComp->Intensity = 2.0f;
						}
						else
						{
							SkyComp->Intensity = 1.0f;
						}
					}
					SkyLightActor->SetActorLabel(TEXT("SkyLight"));
					SkyLightActor->SetFolderPath(FName(TEXT("Level/Lighting")));
					CreatedActors.Add(TEXT("SkyLight"));
				}
			}

			// --- Sky Atmosphere ---
			{
				UClass* SkyAtmoClass = FindFirstObject<UClass>(TEXT("SkyAtmosphere"), EFindFirstObjectOptions::ExactClass);
				if (!SkyAtmoClass)
				{
					SkyAtmoClass = FindFirstObject<UClass>(TEXT("ASkyAtmosphere"), EFindFirstObjectOptions::ExactClass);
				}
				if (SkyAtmoClass)
				{
					FVector AtmoLoc(0, 0, 0);
					FRotator AtmoRot(0, 0, 0);
					AActor* SkyAtmo = World->SpawnActor(SkyAtmoClass, &AtmoLoc, &AtmoRot, SpawnParams);
					if (SkyAtmo)
					{
						SkyAtmo->SetActorLabel(TEXT("SkyAtmosphere"));
						SkyAtmo->SetFolderPath(FName(TEXT("Level/Lighting")));
						CreatedActors.Add(TEXT("SkyAtmosphere"));
					}
				}
			}

			// --- Post Process Volume (infinite extent) ---
			{
				FVector PPVLoc(0, 0, 0);
				FRotator PPVRot(0, 0, 0);
				APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), PPVLoc, PPVRot, SpawnParams);
				if (PPV)
				{
					PPV->bUnbound = true;
					PPV->Settings.bOverride_AutoExposureBias = true;
					PPV->Settings.AutoExposureBias = 1.0f;

					if (LightingPreset == TEXT("Night"))
					{
						PPV->Settings.AutoExposureBias = 0.5f;
						PPV->Settings.bOverride_BloomIntensity = true;
						PPV->Settings.BloomIntensity = 1.0f;
					}
					else if (LightingPreset == TEXT("Sunset"))
					{
						PPV->Settings.bOverride_BloomIntensity = true;
						PPV->Settings.BloomIntensity = 0.8f;
						PPV->Settings.bOverride_VignetteIntensity = true;
						PPV->Settings.VignetteIntensity = 0.3f;
					}

					PPV->SetActorLabel(TEXT("PostProcessVolume"));
					PPV->SetFolderPath(FName(TEXT("Level/Lighting")));
					CreatedActors.Add(TEXT("PostProcessVolume"));
				}
			}

			// --- Exponential Height Fog ---
			{
				FVector FogLoc(0, 0, 200);
				FRotator FogRot(0, 0, 0);
				AExponentialHeightFog* FogActor = World->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FogLoc, FogRot, SpawnParams);
				if (FogActor)
				{
					UExponentialHeightFogComponent* FogComp = FogActor->GetComponent();
					if (FogComp)
					{
						FogComp->FogDensity = 0.02f;

						if (LightingPreset == TEXT("Night"))
						{
							FogComp->FogInscatteringLuminance = FLinearColor(0.05f, 0.08f, 0.15f);
							FogComp->FogDensity = 0.05f;
						}
						else if (LightingPreset == TEXT("Sunset"))
						{
							FogComp->FogInscatteringLuminance = FLinearColor(0.8f, 0.4f, 0.15f);
						}
						else if (LightingPreset == TEXT("Indoor"))
						{
							FogComp->FogDensity = 0.005f;
						}
					}
					FogActor->SetActorLabel(TEXT("ExponentialHeightFog"));
					FogActor->SetFolderPath(FName(TEXT("Level/Lighting")));
					CreatedActors.Add(TEXT("ExponentialHeightFog"));
				}
			}

			// --- Player Start ---
			if (bAddPlayerStart)
			{
				FVector PlayerStartLoc(0, 0, 100);
				FRotator PlayerStartRot(0, 0, 0);
				APlayerStart* PS = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), PlayerStartLoc, PlayerStartRot, SpawnParams);
				if (PS)
				{
					PS->SetActorLabel(TEXT("PlayerStart"));
					PS->SetFolderPath(FName(TEXT("Level")));
					CreatedActors.Add(TEXT("PlayerStart"));
				}
			}

			// --- NavMesh Bounds Volume ---
			if (bAddNavMesh)
			{
				FVector NavLoc(0, 0, FloorSize * 0.25);
				FRotator NavRot(0, 0, 0);
				ANavMeshBoundsVolume* NavVol = World->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(), NavLoc, NavRot, SpawnParams);
				if (NavVol)
				{
					// Scale the brush to cover the floor area with vertical extent
					NavVol->SetActorScale3D(FVector(FloorSize / 200.0, FloorSize / 200.0, FloorSize * 0.5 / 200.0));
					NavVol->SetActorLabel(TEXT("NavMeshBoundsVolume"));
					NavVol->SetFolderPath(FName(TEXT("Level")));
					CreatedActors.Add(TEXT("NavMeshBoundsVolume"));
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created basic level (%s preset, floor %.0f cm): %s"),
				*LightingPreset, FloorSize, *FString::Join(CreatedActors, TEXT(", "))));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_trigger_volume - Creates a trigger box actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("label"), TEXT("Label for the trigger box actor"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("X position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Y position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Z position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("extent_x"), TEXT("Box half-extent along X in cm (default: 100)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("extent_y"), TEXT("Box half-extent along Y in cm (default: 100)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("extent_z"), TEXT("Box half-extent along Z in cm (default: 100)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("tag"), TEXT("Optional tag to apply to the actor"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_trigger_volume");
		Def.Description = TEXT("Creates a TriggerBox actor with configurable position, extents, and optional tag. Useful for creating gameplay trigger zones.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = false;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString Label;
			if (!Args->TryGetStringField(TEXT("label"), Label))
				return FMCPToolResult::Error(TEXT("label is required"));

			FVector Location(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);

			double ExtentX = Args->HasField(TEXT("extent_x")) ? Args->GetNumberField(TEXT("extent_x")) : 100.0;
			double ExtentY = Args->HasField(TEXT("extent_y")) ? Args->GetNumberField(TEXT("extent_y")) : 100.0;
			double ExtentZ = Args->HasField(TEXT("extent_z")) ? Args->GetNumberField(TEXT("extent_z")) : 100.0;

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Trigger Volume")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			FRotator Rot(0, 0, 0);
			ATriggerBox* TriggerBox = World->SpawnActor<ATriggerBox>(ATriggerBox::StaticClass(), Location, Rot, SpawnParams);
			if (!TriggerBox)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn TriggerBox"));
			}

			TriggerBox->SetActorLabel(Label);

			// Configure extents via the box component
			UBoxComponent* BoxComp = Cast<UBoxComponent>(TriggerBox->GetCollisionComponent());
			if (BoxComp)
			{
				BoxComp->SetBoxExtent(FVector(ExtentX, ExtentY, ExtentZ));
			}

			// Optional tag
			FString Tag;
			if (Args->TryGetStringField(TEXT("tag"), Tag) && !Tag.IsEmpty())
			{
				TriggerBox->Tags.Add(FName(*Tag));
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created TriggerBox '%s' at (%.1f, %.1f, %.1f) with extents (%.1f, %.1f, %.1f)"),
				*Label, Location.X, Location.Y, Location.Z, ExtentX, ExtentY, ExtentZ));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_light_rig - Creates a 3-point lighting setup
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_x"), TEXT("Center X position of the rig (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_y"), TEXT("Center Y position of the rig (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_z"), TEXT("Center Z position of the rig (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("radius"), TEXT("Distance of lights from center (default: 500)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("key_intensity"), TEXT("Key light intensity in candelas (default: 10)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("fill_intensity"), TEXT("Fill light intensity in candelas (default: 3)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("rim_intensity"), TEXT("Rim/back light intensity in candelas (default: 5)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_light_rig");
		Def.Description = TEXT("Creates a 3-point lighting setup (key, fill, rim) around a center position. Key light is front-left, fill light is front-right (softer), rim/back light is behind the subject. All lights placed in 'Lighting/LightRig' folder.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = false;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FVector Center(
				Args->HasField(TEXT("center_x")) ? Args->GetNumberField(TEXT("center_x")) : 0.0,
				Args->HasField(TEXT("center_y")) ? Args->GetNumberField(TEXT("center_y")) : 0.0,
				Args->HasField(TEXT("center_z")) ? Args->GetNumberField(TEXT("center_z")) : 0.0
			);

			double Radius = Args->HasField(TEXT("radius")) ? Args->GetNumberField(TEXT("radius")) : 500.0;
			float KeyIntensity = (float)(Args->HasField(TEXT("key_intensity")) ? Args->GetNumberField(TEXT("key_intensity")) : 10.0);
			float FillIntensity = (float)(Args->HasField(TEXT("fill_intensity")) ? Args->GetNumberField(TEXT("fill_intensity")) : 3.0);
			float RimIntensity = (float)(Args->HasField(TEXT("rim_intensity")) ? Args->GetNumberField(TEXT("rim_intensity")) : 5.0);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Light Rig")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			auto SpawnPointLight = [&](const FString& LightLabel, const FVector& Offset, float Intensity, const FLinearColor& Color) -> APointLight*
			{
				FVector Loc = Center + Offset;
				FRotator Rot(0, 0, 0);
				APointLight* Light = World->SpawnActor<APointLight>(APointLight::StaticClass(), Loc, Rot, SpawnParams);
				if (Light)
				{
					UPointLightComponent* LightComp = Cast<UPointLightComponent>(Light->GetLightComponent());
					if (LightComp)
					{
						LightComp->SetIntensity(Intensity);
						LightComp->SetLightColor(Color);
						LightComp->SetAttenuationRadius((float)(Radius * 3.0));
						LightComp->SetCastShadows(true);
					}
					Light->SetActorLabel(LightLabel);
					Light->SetFolderPath(FName(TEXT("Lighting/LightRig")));
				}
				return Light;
			};

			// Key light: front-left, 45 degrees, elevated
			FVector KeyOffset(
				-Radius * 0.707, // front
				-Radius * 0.707, // left
				Radius * 0.5     // elevated
			);
			APointLight* KeyLight = SpawnPointLight(TEXT("KeyLight"), KeyOffset, KeyIntensity, FLinearColor(1.0f, 0.95f, 0.9f));

			// Fill light: front-right, 45 degrees, slightly lower
			FVector FillOffset(
				-Radius * 0.707, // front
				Radius * 0.707,  // right
				Radius * 0.3     // slightly elevated
			);
			APointLight* FillLight = SpawnPointLight(TEXT("FillLight"), FillOffset, FillIntensity, FLinearColor(0.85f, 0.9f, 1.0f));

			// Rim/back light: behind and above
			FVector RimOffset(
				Radius * 0.8,    // behind
				0,               // centered
				Radius * 0.7     // high
			);
			APointLight* RimLight = SpawnPointLight(TEXT("RimLight"), RimOffset, RimIntensity, FLinearColor(1.0f, 1.0f, 1.0f));

			GEditor->EndTransaction();

			int32 Created = 0;
			if (KeyLight) Created++;
			if (FillLight) Created++;
			if (RimLight) Created++;

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created 3-point light rig (%d lights) centered at (%.1f, %.1f, %.1f) with radius %.1f"),
				Created, Center.X, Center.Y, Center.Z, Radius));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_grid_layout - Creates a grid of mesh actors
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"), TEXT("Content path of the static mesh asset to use for each grid cell"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("rows"), TEXT("Number of rows in the grid"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("columns"), TEXT("Number of columns in the grid"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("spacing_x"), TEXT("Spacing between columns along X axis in cm (default: 200)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("spacing_y"), TEXT("Spacing between rows along Y axis in cm (default: 200)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_x"), TEXT("Grid origin X position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_y"), TEXT("Grid origin Y position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_z"), TEXT("Grid origin Z position (default: 0)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"), TEXT("Optional material to assign to all mesh actors"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_grid_layout");
		Def.Description = TEXT("Creates a grid of StaticMeshActors arranged in rows and columns. All actors are placed in a 'Layout/Grid' folder.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = false;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
				return FMCPToolResult::Error(TEXT("mesh_path is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

			int32 Rows = Args->HasField(TEXT("rows")) ? (int32)Args->GetNumberField(TEXT("rows")) : 1;
			int32 Columns = Args->HasField(TEXT("columns")) ? (int32)Args->GetNumberField(TEXT("columns")) : 1;

			if (Rows < 1 || Rows > 100) return FMCPToolResult::Error(TEXT("rows must be between 1 and 100"));
			if (Columns < 1 || Columns > 100) return FMCPToolResult::Error(TEXT("columns must be between 1 and 100"));
			if (Rows * Columns > 2500) return FMCPToolResult::Error(TEXT("Total grid cells (rows * columns) cannot exceed 2500"));

			double SpacingX = Args->HasField(TEXT("spacing_x")) ? Args->GetNumberField(TEXT("spacing_x")) : 200.0;
			double SpacingY = Args->HasField(TEXT("spacing_y")) ? Args->GetNumberField(TEXT("spacing_y")) : 200.0;
			double StartX = Args->HasField(TEXT("start_x")) ? Args->GetNumberField(TEXT("start_x")) : 0.0;
			double StartY = Args->HasField(TEXT("start_y")) ? Args->GetNumberField(TEXT("start_y")) : 0.0;
			double StartZ = Args->HasField(TEXT("start_z")) ? Args->GetNumberField(TEXT("start_z")) : 0.0;

			// Optional material
			UMaterialInterface* Material = nullptr;
			FString MaterialPath;
			if (Args->TryGetStringField(TEXT("material_path"), MaterialPath) && !MaterialPath.IsEmpty())
			{
				Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Grid Layout")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			int32 Created = 0;
			for (int32 Row = 0; Row < Rows; Row++)
			{
				for (int32 Col = 0; Col < Columns; Col++)
				{
					FVector Loc(
						StartX + Col * SpacingX,
						StartY + Row * SpacingY,
						StartZ
					);
					FRotator Rot(0, 0, 0);

					AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, Rot, SpawnParams);
					if (Actor)
					{
						UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
						if (MeshComp)
						{
							MeshComp->SetStaticMesh(Mesh);
							if (Material)
							{
								for (int32 i = 0; i < MeshComp->GetNumMaterials(); i++)
								{
									MeshComp->SetMaterial(i, Material);
								}
							}
						}

						FString ActorLabel = FString::Printf(TEXT("Grid_%d_%d"), Row, Col);
						Actor->SetActorLabel(ActorLabel);
						Actor->SetFolderPath(FName(TEXT("Layout/Grid")));
						Created++;
					}
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created %d actors in a %dx%d grid (spacing: %.1fx%.1f) starting at (%.1f, %.1f, %.1f)"),
				Created, Rows, Columns, SpacingX, SpacingY, StartX, StartY, StartZ));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_ring_layout - Creates a circular arrangement of actors
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"), TEXT("Content path of the static mesh asset"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("count"), TEXT("Number of actors to distribute around the ring"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("radius"), TEXT("Radius of the ring in cm"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_x"), TEXT("Center X position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_y"), TEXT("Center Y position (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("center_z"), TEXT("Center Z position (default: 0)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("face_center"), TEXT("Rotate actors to face the center of the ring (default: true)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("material_path"), TEXT("Optional material to assign to all mesh actors"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_ring_layout");
		Def.Description = TEXT("Creates a circular arrangement of StaticMeshActors evenly distributed around a ring. Optionally rotates each actor to face the center. All actors placed in 'Layout/Ring' folder.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = false;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
				return FMCPToolResult::Error(TEXT("mesh_path is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

			int32 Count = Args->HasField(TEXT("count")) ? (int32)Args->GetNumberField(TEXT("count")) : 1;
			if (Count < 1 || Count > 360) return FMCPToolResult::Error(TEXT("count must be between 1 and 360"));

			double RingRadius = Args->HasField(TEXT("radius")) ? Args->GetNumberField(TEXT("radius")) : 500.0;
			if (RingRadius <= 0) return FMCPToolResult::Error(TEXT("radius must be positive"));

			FVector Center(
				Args->HasField(TEXT("center_x")) ? Args->GetNumberField(TEXT("center_x")) : 0.0,
				Args->HasField(TEXT("center_y")) ? Args->GetNumberField(TEXT("center_y")) : 0.0,
				Args->HasField(TEXT("center_z")) ? Args->GetNumberField(TEXT("center_z")) : 0.0
			);

			bool bFaceCenter = true;
			Args->TryGetBoolField(TEXT("face_center"), bFaceCenter);

			// Optional material
			UMaterialInterface* Material = nullptr;
			FString MaterialPath;
			if (Args->TryGetStringField(TEXT("material_path"), MaterialPath) && !MaterialPath.IsEmpty())
			{
				Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
			}

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Ring Layout")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			int32 Created = 0;
			double AngleStep = 360.0 / (double)Count;

			for (int32 i = 0; i < Count; i++)
			{
				double AngleDeg = i * AngleStep;
				double AngleRad = FMath::DegreesToRadians(AngleDeg);

				FVector Loc(
					Center.X + RingRadius * FMath::Cos(AngleRad),
					Center.Y + RingRadius * FMath::Sin(AngleRad),
					Center.Z
				);

				FRotator Rot(0, 0, 0);
				if (bFaceCenter)
				{
					// Face toward center: compute yaw from position to center
					FVector ToCenter = Center - Loc;
					Rot = ToCenter.Rotation();
					Rot.Pitch = 0; // Keep level
					Rot.Roll = 0;
				}

				AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Loc, Rot, SpawnParams);
				if (Actor)
				{
					UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
					if (MeshComp)
					{
						MeshComp->SetStaticMesh(Mesh);
						if (Material)
						{
							for (int32 j = 0; j < MeshComp->GetNumMaterials(); j++)
							{
								MeshComp->SetMaterial(j, Material);
							}
						}
					}

					FString ActorLabel = FString::Printf(TEXT("Ring_%d"), i);
					Actor->SetActorLabel(ActorLabel);
					Actor->SetFolderPath(FName(TEXT("Layout/Ring")));
					Created++;
				}
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created %d actors in a ring (radius: %.1f) centered at (%.1f, %.1f, %.1f)%s"),
				Created, RingRadius, Center.X, Center.Y, Center.Z,
				bFaceCenter ? TEXT(", facing center") : TEXT("")));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// create_staircase - Creates a staircase from mesh geometry
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("mesh_path"), TEXT("Content path of the static mesh asset to use for each step"), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("step_count"), TEXT("Number of steps in the staircase"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("step_height"), TEXT("Vertical height offset per step in cm (default: 20)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("step_depth"), TEXT("Horizontal depth offset per step in cm (default: 30)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("step_width"), TEXT("Width of each step in cm; used only for labeling, mesh defines actual width (default: 100)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_x"), TEXT("X position of the first step (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_y"), TEXT("Y position of the first step (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_z"), TEXT("Z position of the first step (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("yaw"), TEXT("Yaw rotation of the staircase in degrees (default: 0). Steps progress forward along this direction."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("create_staircase");
		Def.Description = TEXT("Creates a staircase from N copies of a mesh arranged in ascending steps. Each step is offset by step_height vertically and step_depth horizontally along the yaw direction. All actors placed in 'Layout/Staircase' folder.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = false;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString MeshPath;
			if (!Args->TryGetStringField(TEXT("mesh_path"), MeshPath))
				return FMCPToolResult::Error(TEXT("mesh_path is required"));

			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
			if (!Mesh) return FMCPToolResult::Error(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath));

			int32 StepCount = Args->HasField(TEXT("step_count")) ? (int32)Args->GetNumberField(TEXT("step_count")) : 1;
			if (StepCount < 1 || StepCount > 200) return FMCPToolResult::Error(TEXT("step_count must be between 1 and 200"));

			double StepHeight = Args->HasField(TEXT("step_height")) ? Args->GetNumberField(TEXT("step_height")) : 20.0;
			double StepDepth = Args->HasField(TEXT("step_depth")) ? Args->GetNumberField(TEXT("step_depth")) : 30.0;
			double Yaw = Args->HasField(TEXT("yaw")) ? Args->GetNumberField(TEXT("yaw")) : 0.0;

			FVector Start(
				Args->HasField(TEXT("start_x")) ? Args->GetNumberField(TEXT("start_x")) : 0.0,
				Args->HasField(TEXT("start_y")) ? Args->GetNumberField(TEXT("start_y")) : 0.0,
				Args->HasField(TEXT("start_z")) ? Args->GetNumberField(TEXT("start_z")) : 0.0
			);

			// Compute forward direction from yaw
			double YawRad = FMath::DegreesToRadians(Yaw);
			FVector Forward(FMath::Cos(YawRad), FMath::Sin(YawRad), 0.0);
			FVector Up(0.0, 0.0, 1.0);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Create Staircase")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			int32 Created = 0;
			for (int32 i = 0; i < StepCount; i++)
			{
				FVector StepLoc = Start + Forward * (StepDepth * i) + Up * (StepHeight * i);
				FRotator StepRot(0, Yaw, 0);

				AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), StepLoc, StepRot, SpawnParams);
				if (Actor)
				{
					UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
					if (MeshComp)
					{
						MeshComp->SetStaticMesh(Mesh);
					}

					FString StepLabel = FString::Printf(TEXT("Step_%d"), i);
					Actor->SetActorLabel(StepLabel);
					Actor->SetFolderPath(FName(TEXT("Layout/Staircase")));
					Created++;
				}
			}

			GEditor->EndTransaction();

			double TotalHeight = StepHeight * (StepCount - 1);
			double TotalDepth = StepDepth * (StepCount - 1);

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Created staircase with %d steps (height: %.1f, depth: %.1f per step, yaw: %.1f). Total rise: %.1f cm, total run: %.1f cm"),
				Created, StepHeight, StepDepth, Yaw, TotalHeight, TotalDepth));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPMacroTools
