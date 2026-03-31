#include "Tools/MCPAudioTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Sound/AmbientSound.h"
#include "Components/AudioComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"

namespace MCPAudioTools
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
	// spawn_sound - Place an AmbientSound actor in the level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sound_path"), TEXT("Content path to the USoundBase asset (e.g., '/Game/Audio/SFX/SW_Ambience')"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("X position in the world (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Y position in the world (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Z position in the world (default: 0)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("label"), TEXT("Optional actor label shown in the scene outliner"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("volume_multiplier"), TEXT("Volume multiplier applied to the sound (default: 1.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("pitch_multiplier"), TEXT("Pitch multiplier applied to the sound (default: 1.0)"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("auto_activate"), TEXT("Whether the sound plays automatically when the actor is spawned (default: true)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("spawn_sound");
		Def.Description = TEXT("Spawn an AmbientSound actor in the current level at a given world position. The sound_path must point to a valid USoundBase asset (SoundWave or SoundCue) in the content browser.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString SoundPath;
			if (!Args->TryGetStringField(TEXT("sound_path"), SoundPath))
				return FMCPToolResult::Error(TEXT("sound_path is required"));

			USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundPath);
			if (!IsValid(Sound))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load SoundBase asset at path: %s"), *SoundPath));

			FVector Location(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);

			float VolumeMultiplier = Args->HasField(TEXT("volume_multiplier")) ? (float)Args->GetNumberField(TEXT("volume_multiplier")) : 1.0f;
			float PitchMultiplier  = Args->HasField(TEXT("pitch_multiplier"))  ? (float)Args->GetNumberField(TEXT("pitch_multiplier"))  : 1.0f;

			bool bAutoActivate = true;
			Args->TryGetBoolField(TEXT("auto_activate"), bAutoActivate);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Spawn Sound")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			AAmbientSound* SoundActor = World->SpawnActor<AAmbientSound>(AAmbientSound::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
			if (!IsValid(SoundActor))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn AAmbientSound actor in the level"));
			}

			UAudioComponent* AudioComp = SoundActor->GetAudioComponent();
			if (!IsValid(AudioComp))
			{
				SoundActor->Destroy();
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Spawned AmbientSound actor has no AudioComponent"));
			}

			AudioComp->SetSound(Sound);
			AudioComp->VolumeMultiplier = VolumeMultiplier;
			AudioComp->PitchMultiplier  = PitchMultiplier;
			AudioComp->bAutoActivate    = bAutoActivate;

			FString Label;
			if (Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
			{
				SoundActor->SetActorLabel(Label);
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Spawned AmbientSound with asset '%s' as actor '%s' at (%.1f, %.1f, %.1f). Volume: %.2f, Pitch: %.2f, AutoActivate: %s"),
				*Sound->GetName(),
				*SoundActor->GetActorLabel(),
				Location.X, Location.Y, Location.Z,
				VolumeMultiplier, PitchMultiplier,
				bAutoActivate ? TEXT("true") : TEXT("false")));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_audio_properties - Set AudioComponent properties on an actor
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor containing the AudioComponent"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("volume_multiplier"), TEXT("Volume multiplier to apply to the sound (0.0 - 2.0+)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("pitch_multiplier"), TEXT("Pitch multiplier to apply to the sound (0.1 - 4.0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("attenuation_distance"), TEXT("Override the maximum attenuation distance in cm"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("is_spatialized"), TEXT("Whether the sound is spatialized in 3D space"));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("auto_activate"), TEXT("Whether the sound plays automatically when the actor begins play"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("sound_path"), TEXT("Optional content path to change the sound asset (e.g., '/Game/Audio/SFX/SW_Wind')"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_audio_properties");
		Def.Description = TEXT("Set audio component properties on any actor that has a UAudioComponent. Only provided parameters are modified. Use sound_path to swap the sound asset.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!IsValid(Actor))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UAudioComponent* AudioComp = Actor->FindComponentByClass<UAudioComponent>();
			if (!IsValid(AudioComp))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no AudioComponent"), *ActorName));

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Audio Properties")));
			AudioComp->Modify();

			int32 Changed = 0;

			if (Args->HasField(TEXT("volume_multiplier")))
			{
				AudioComp->VolumeMultiplier = (float)Args->GetNumberField(TEXT("volume_multiplier"));
				Changed++;
			}

			if (Args->HasField(TEXT("pitch_multiplier")))
			{
				AudioComp->PitchMultiplier = (float)Args->GetNumberField(TEXT("pitch_multiplier"));
				Changed++;
			}

			if (Args->HasField(TEXT("attenuation_distance")))
			{
				// Override max distance via the component's attenuation settings override
				AudioComp->bOverrideAttenuation = true;
				AudioComp->AttenuationOverrides.bAttenuate = true;
				AudioComp->AttenuationOverrides.FalloffDistance = (float)Args->GetNumberField(TEXT("attenuation_distance"));
				Changed++;
			}

			bool bSpatialized = false;
			if (Args->TryGetBoolField(TEXT("is_spatialized"), bSpatialized))
			{
				AudioComp->bOverrideAttenuation = true;
				AudioComp->AttenuationOverrides.bSpatialize = bSpatialized;
				Changed++;
			}

			bool bAutoActivate = false;
			if (Args->TryGetBoolField(TEXT("auto_activate"), bAutoActivate))
			{
				AudioComp->bAutoActivate = bAutoActivate;
				Changed++;
			}

			FString SoundPath;
			if (Args->TryGetStringField(TEXT("sound_path"), SoundPath) && !SoundPath.IsEmpty())
			{
				USoundBase* NewSound = LoadObject<USoundBase>(nullptr, *SoundPath);
				if (!IsValid(NewSound))
				{
					GEditor->EndTransaction();
					return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load sound asset at path: %s"), *SoundPath));
				}
				AudioComp->SetSound(NewSound);
				Changed++;
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(TEXT("Updated %d audio properties on actor '%s'"), Changed, *ActorName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_sound_info - Get information about a sound asset
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("sound_path"), TEXT("Content path to the USoundBase asset (e.g., '/Game/Audio/SFX/SW_Gunshot')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_sound_info");
		Def.Description = TEXT("Get detailed information about a sound asset: type, duration, and for SoundWaves also channel count, sample rate, looping flag, and sound group.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SoundPath;
			if (!Args->TryGetStringField(TEXT("sound_path"), SoundPath))
				return FMCPToolResult::Error(TEXT("sound_path is required"));

			USoundBase* Sound = LoadObject<USoundBase>(nullptr, *SoundPath);
			if (!IsValid(Sound))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load SoundBase asset at path: %s"), *SoundPath));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("name"), Sound->GetName());
			Result->SetStringField(TEXT("path"), Sound->GetPathName());
			Result->SetNumberField(TEXT("duration"), Sound->GetDuration());

			// Determine the concrete type and populate type-specific fields
			USoundWave* SoundWave = Cast<USoundWave>(Sound);
			USoundCue*  SoundCue  = Cast<USoundCue>(Sound);

			if (SoundWave)
			{
				Result->SetStringField(TEXT("type"), TEXT("SoundWave"));
				Result->SetNumberField(TEXT("num_channels"), SoundWave->NumChannels);
				Result->SetNumberField(TEXT("sample_rate"), SoundWave->GetSampleRateForCurrentPlatform());
				Result->SetBoolField(TEXT("looping"), SoundWave->bLooping);

				// SoundGroup is an enum; expose the display name as a string
				UEnum* SoundGroupEnum = StaticEnum<ESoundGroup>();
				FString SoundGroupName = SoundGroupEnum
					? SoundGroupEnum->GetDisplayNameTextByValue((int64)SoundWave->SoundGroup).ToString()
					: FString::FromInt((int32)SoundWave->SoundGroup);
				Result->SetStringField(TEXT("sound_group"), SoundGroupName);
			}
			else if (SoundCue)
			{
				Result->SetStringField(TEXT("type"), TEXT("SoundCue"));
			}
			else
			{
				// Fallback for any other USoundBase subclass
				Result->SetStringField(TEXT("type"), Sound->GetClass()->GetName());
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPAudioTools
