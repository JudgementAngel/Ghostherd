#include "Tools/MCPEnvironmentTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Atmosphere/AtmosphericFog.h"

namespace MCPEnvironmentTools
{

static UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
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
	// set_post_process_settings - Configure PostProcessVolume
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the PostProcessVolume actor"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("bloom_intensity"), TEXT("Bloom intensity (0-8, default: 0.675)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("bloom_threshold"), TEXT("Bloom threshold (-1 to 8)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("exposure_compensation"), TEXT("Exposure compensation EV (-15 to 15)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("exposure_min_brightness"), TEXT("Auto exposure min brightness"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("exposure_max_brightness"), TEXT("Auto exposure max brightness"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("vignette_intensity"), TEXT("Vignette intensity (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("grain_intensity"), TEXT("Film grain intensity (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("ao_intensity"), TEXT("Ambient occlusion intensity (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("ao_radius"), TEXT("Ambient occlusion radius in cm"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("infinite_extent"), TEXT("Make this an unbound (infinite extent) volume"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_post_process_settings");
		Def.Description = TEXT("Set bloom, exposure, color grading, AO, and other post-process settings on a PostProcessVolume. Only provided parameters are changed.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			APostProcessVolume* PPV = Cast<APostProcessVolume>(Actor);
			if (!PPV) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' is not a PostProcessVolume"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Post Process Settings")));
			PPV->Modify();

			FPostProcessSettings& S = PPV->Settings;
			int32 Changed = 0;

			if (Args->HasField(TEXT("bloom_intensity")))
			{
				S.bOverride_BloomIntensity = true;
				S.BloomIntensity = (float)Args->GetNumberField(TEXT("bloom_intensity"));
				Changed++;
			}
			if (Args->HasField(TEXT("bloom_threshold")))
			{
				S.bOverride_BloomThreshold = true;
				S.BloomThreshold = (float)Args->GetNumberField(TEXT("bloom_threshold"));
				Changed++;
			}
			if (Args->HasField(TEXT("exposure_compensation")))
			{
				S.bOverride_AutoExposureBias = true;
				S.AutoExposureBias = (float)Args->GetNumberField(TEXT("exposure_compensation"));
				Changed++;
			}
			if (Args->HasField(TEXT("exposure_min_brightness")))
			{
				S.bOverride_AutoExposureMinBrightness = true;
				S.AutoExposureMinBrightness = (float)Args->GetNumberField(TEXT("exposure_min_brightness"));
				Changed++;
			}
			if (Args->HasField(TEXT("exposure_max_brightness")))
			{
				S.bOverride_AutoExposureMaxBrightness = true;
				S.AutoExposureMaxBrightness = (float)Args->GetNumberField(TEXT("exposure_max_brightness"));
				Changed++;
			}
			if (Args->HasField(TEXT("vignette_intensity")))
			{
				S.bOverride_VignetteIntensity = true;
				S.VignetteIntensity = (float)Args->GetNumberField(TEXT("vignette_intensity"));
				Changed++;
			}
			if (Args->HasField(TEXT("grain_intensity")))
			{
				S.bOverride_FilmGrainIntensity = true;
				S.FilmGrainIntensity = (float)Args->GetNumberField(TEXT("grain_intensity"));
				Changed++;
			}
			if (Args->HasField(TEXT("ao_intensity")))
			{
				S.bOverride_AmbientOcclusionIntensity = true;
				S.AmbientOcclusionIntensity = (float)Args->GetNumberField(TEXT("ao_intensity"));
				Changed++;
			}
			if (Args->HasField(TEXT("ao_radius")))
			{
				S.bOverride_AmbientOcclusionRadius = true;
				S.AmbientOcclusionRadius = (float)Args->GetNumberField(TEXT("ao_radius"));
				Changed++;
			}

			bool bInfiniteExtent = false;
			if (Args->TryGetBoolField(TEXT("infinite_extent"), bInfiniteExtent))
			{
				PPV->bUnbound = bInfiniteExtent;
				Changed++;
			}

			PPV->PostEditChange();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Updated %d post-process settings on '%s'"), Changed, *ActorName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_fog_settings - Configure ExponentialHeightFog
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the ExponentialHeightFog actor"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("fog_density"), TEXT("Fog density (0-1, default: 0.02)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("fog_height_falloff"), TEXT("Height falloff (0-2, default: 0.2)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("fog_max_opacity"), TEXT("Maximum opacity (0-1, default: 1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("start_distance"), TEXT("Start distance in cm (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_r"), TEXT("Inscattering color red (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_g"), TEXT("Inscattering color green (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_b"), TEXT("Inscattering color blue (0-1)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("volumetric_fog"), TEXT("Enable volumetric fog"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_fog_settings");
		Def.Description = TEXT("Configure ExponentialHeightFog density, color, falloff, and volumetric fog settings.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UExponentialHeightFogComponent* FogComp = Actor->FindComponentByClass<UExponentialHeightFogComponent>();
			if (!FogComp) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no ExponentialHeightFogComponent"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Fog Settings")));
			FogComp->Modify();

			int32 Changed = 0;

			if (Args->HasField(TEXT("fog_density")))
			{
				FogComp->FogDensity = (float)Args->GetNumberField(TEXT("fog_density"));
				Changed++;
			}
			if (Args->HasField(TEXT("fog_height_falloff")))
			{
				FogComp->FogHeightFalloff = (float)Args->GetNumberField(TEXT("fog_height_falloff"));
				Changed++;
			}
			if (Args->HasField(TEXT("fog_max_opacity")))
			{
				FogComp->FogMaxOpacity = (float)Args->GetNumberField(TEXT("fog_max_opacity"));
				Changed++;
			}
			if (Args->HasField(TEXT("start_distance")))
			{
				FogComp->StartDistance = (float)Args->GetNumberField(TEXT("start_distance"));
				Changed++;
			}

			// Inscattering color
			if (Args->HasField(TEXT("color_r")) || Args->HasField(TEXT("color_g")) || Args->HasField(TEXT("color_b")))
			{
				FLinearColor Color = FogComp->FogInscatteringLuminance;
				if (Args->HasField(TEXT("color_r"))) Color.R = (float)Args->GetNumberField(TEXT("color_r"));
				if (Args->HasField(TEXT("color_g"))) Color.G = (float)Args->GetNumberField(TEXT("color_g"));
				if (Args->HasField(TEXT("color_b"))) Color.B = (float)Args->GetNumberField(TEXT("color_b"));
				FogComp->FogInscatteringLuminance = Color;
				Changed++;
			}

			bool bVolumetric = false;
			if (Args->TryGetBoolField(TEXT("volumetric_fog"), bVolumetric))
			{
				FogComp->bEnableVolumetricFog = bVolumetric;
				Changed++;
			}

			FogComp->MarkRenderStateDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Updated %d fog settings on '%s'"), Changed, *ActorName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_sky_atmosphere - Configure SkyAtmosphere component
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor with SkyAtmosphereComponent"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("rayleigh_scattering_r"), TEXT("Rayleigh scattering red (default: 0.0058)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("rayleigh_scattering_g"), TEXT("Rayleigh scattering green (default: 0.01355)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("rayleigh_scattering_b"), TEXT("Rayleigh scattering blue (default: 0.0331)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("rayleigh_exponential_distribution"), TEXT("Rayleigh exponential distribution (default: 8)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("mie_scattering_scale"), TEXT("Mie scattering scale (default: 0.003996)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("mie_absorption_scale"), TEXT("Mie absorption scale (default: 0.000444)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("mie_anisotropy"), TEXT("Mie anisotropy (0-0.999, default: 0.8)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("atmosphere_height"), TEXT("Atmosphere height in km (default: 60)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_sky_atmosphere");
		Def.Description = TEXT("Configure SkyAtmosphere scattering, absorption, and height parameters for realistic sky rendering.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			USkyAtmosphereComponent* AtmoComp = Actor->FindComponentByClass<USkyAtmosphereComponent>();
			if (!AtmoComp) return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no SkyAtmosphereComponent"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Sky Atmosphere")));
			AtmoComp->Modify();

			int32 Changed = 0;

			// Rayleigh scattering color
			if (Args->HasField(TEXT("rayleigh_scattering_r")) || Args->HasField(TEXT("rayleigh_scattering_g")) || Args->HasField(TEXT("rayleigh_scattering_b")))
			{
				FLinearColor Rayleigh = AtmoComp->RayleighScattering;
				if (Args->HasField(TEXT("rayleigh_scattering_r"))) Rayleigh.R = (float)Args->GetNumberField(TEXT("rayleigh_scattering_r"));
				if (Args->HasField(TEXT("rayleigh_scattering_g"))) Rayleigh.G = (float)Args->GetNumberField(TEXT("rayleigh_scattering_g"));
				if (Args->HasField(TEXT("rayleigh_scattering_b"))) Rayleigh.B = (float)Args->GetNumberField(TEXT("rayleigh_scattering_b"));
				AtmoComp->RayleighScattering = Rayleigh;
				Changed++;
			}

			if (Args->HasField(TEXT("rayleigh_exponential_distribution")))
			{
				AtmoComp->RayleighExponentialDistribution = (float)Args->GetNumberField(TEXT("rayleigh_exponential_distribution"));
				Changed++;
			}
			if (Args->HasField(TEXT("mie_scattering_scale")))
			{
				AtmoComp->MieScatteringScale = (float)Args->GetNumberField(TEXT("mie_scattering_scale"));
				Changed++;
			}
			if (Args->HasField(TEXT("mie_absorption_scale")))
			{
				AtmoComp->MieAbsorptionScale = (float)Args->GetNumberField(TEXT("mie_absorption_scale"));
				Changed++;
			}
			if (Args->HasField(TEXT("mie_anisotropy")))
			{
				AtmoComp->MieAnisotropy = (float)Args->GetNumberField(TEXT("mie_anisotropy"));
				Changed++;
			}
			if (Args->HasField(TEXT("atmosphere_height")))
			{
				AtmoComp->AtmosphereHeight = (float)Args->GetNumberField(TEXT("atmosphere_height"));
				Changed++;
			}

			AtmoComp->MarkRenderStateDirty();
			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Updated %d atmosphere settings on '%s'"), Changed, *ActorName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_light_properties - Unified light configuration
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the light actor"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("intensity"), TEXT("Light intensity (in candelas for point/spot, lux for directional)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_r"), TEXT("Light color red (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_g"), TEXT("Light color green (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("color_b"), TEXT("Light color blue (0-1)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("temperature"), TEXT("Color temperature in Kelvin (1000-15000)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("use_temperature"), TEXT("Use color temperature instead of color"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("attenuation_radius"), TEXT("Attenuation radius in cm (point/spot lights)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("source_radius"), TEXT("Source radius for soft shadows (cm)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("cast_shadows"), TEXT("Enable shadow casting"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("inner_cone_angle"), TEXT("Spot light inner cone angle (degrees)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("outer_cone_angle"), TEXT("Spot light outer cone angle (degrees)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("indirect_lighting_intensity"), TEXT("Indirect lighting intensity multiplier"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_light_properties");
		Def.Description = TEXT("Configure light properties: intensity, color, temperature, shadows, attenuation. Works on PointLight, SpotLight, DirectionalLight, and SkyLight actors.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName)) return FMCPToolResult::Error(TEXT("actor_name required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!Actor) return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			ULightComponent* LightComp = Actor->FindComponentByClass<ULightComponent>();
			USkyLightComponent* SkyComp = Actor->FindComponentByClass<USkyLightComponent>();

			if (!LightComp && !SkyComp)
				return FMCPToolResult::Error(FString::Printf(TEXT("'%s' has no light component"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Light Properties")));

			int32 Changed = 0;

			if (LightComp)
			{
				LightComp->Modify();

				if (Args->HasField(TEXT("intensity")))
				{
					LightComp->SetIntensity((float)Args->GetNumberField(TEXT("intensity")));
					Changed++;
				}

				if (Args->HasField(TEXT("color_r")) || Args->HasField(TEXT("color_g")) || Args->HasField(TEXT("color_b")))
				{
					FLinearColor Color = LightComp->GetLightColor();
					if (Args->HasField(TEXT("color_r"))) Color.R = (float)Args->GetNumberField(TEXT("color_r"));
					if (Args->HasField(TEXT("color_g"))) Color.G = (float)Args->GetNumberField(TEXT("color_g"));
					if (Args->HasField(TEXT("color_b"))) Color.B = (float)Args->GetNumberField(TEXT("color_b"));
					LightComp->SetLightColor(Color);
					Changed++;
				}

				if (Args->HasField(TEXT("temperature")))
				{
					LightComp->Temperature = (float)Args->GetNumberField(TEXT("temperature"));
					Changed++;
				}

				bool bUseTemp = false;
				if (Args->TryGetBoolField(TEXT("use_temperature"), bUseTemp))
				{
					LightComp->bUseTemperature = bUseTemp;
					Changed++;
				}

				if (Args->HasField(TEXT("source_radius")))
				{
					UPointLightComponent* PointLightForRadius = Cast<UPointLightComponent>(LightComp);
					if (PointLightForRadius)
					{
						PointLightForRadius->SourceRadius = (float)Args->GetNumberField(TEXT("source_radius"));
						Changed++;
					}
				}

				bool bCastShadows = false;
				if (Args->TryGetBoolField(TEXT("cast_shadows"), bCastShadows))
				{
					LightComp->SetCastShadows(bCastShadows);
					Changed++;
				}

				if (Args->HasField(TEXT("indirect_lighting_intensity")))
				{
					LightComp->IndirectLightingIntensity = (float)Args->GetNumberField(TEXT("indirect_lighting_intensity"));
					Changed++;
				}

				// Point/Spot specific: attenuation radius
				UPointLightComponent* PointComp = Cast<UPointLightComponent>(LightComp);
				if (PointComp && Args->HasField(TEXT("attenuation_radius")))
				{
					PointComp->SetAttenuationRadius((float)Args->GetNumberField(TEXT("attenuation_radius")));
					Changed++;
				}

				// Spot specific: cone angles
				USpotLightComponent* SpotComp = Cast<USpotLightComponent>(LightComp);
				if (SpotComp)
				{
					if (Args->HasField(TEXT("inner_cone_angle")))
					{
						SpotComp->SetInnerConeAngle((float)Args->GetNumberField(TEXT("inner_cone_angle")));
						Changed++;
					}
					if (Args->HasField(TEXT("outer_cone_angle")))
					{
						SpotComp->SetOuterConeAngle((float)Args->GetNumberField(TEXT("outer_cone_angle")));
						Changed++;
					}
				}

				LightComp->MarkRenderStateDirty();
			}
			else if (SkyComp)
			{
				SkyComp->Modify();

				if (Args->HasField(TEXT("intensity")))
				{
					SkyComp->Intensity = (float)Args->GetNumberField(TEXT("intensity"));
					Changed++;
				}

				if (Args->HasField(TEXT("color_r")) || Args->HasField(TEXT("color_g")) || Args->HasField(TEXT("color_b")))
				{
					FLinearColor Color = FLinearColor(SkyComp->LightColor);
					if (Args->HasField(TEXT("color_r"))) Color.R = (float)Args->GetNumberField(TEXT("color_r"));
					if (Args->HasField(TEXT("color_g"))) Color.G = (float)Args->GetNumberField(TEXT("color_g"));
					if (Args->HasField(TEXT("color_b"))) Color.B = (float)Args->GetNumberField(TEXT("color_b"));
					SkyComp->LightColor = Color.ToFColor(/*bSRGB=*/true);
					Changed++;
				}

				bool bCastShadows = false;
				if (Args->TryGetBoolField(TEXT("cast_shadows"), bCastShadows))
				{
					SkyComp->SetCastShadows(bCastShadows);
					Changed++;
				}

				SkyComp->MarkRenderStateDirty();
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Updated %d light properties on '%s'"), Changed, *ActorName));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPEnvironmentTools
