#include "Tools/MCPNiagaraTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "NiagaraActor.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraTypes.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace MCPNiagaraTools
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
	// spawn_niagara_system - Place a Niagara system in the level
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("system_path"), TEXT("Content path to the NiagaraSystem asset (e.g., '/Game/FX/NS_Fire')"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("X position in the world (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Y position in the world (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Z position in the world (default: 0)"));
		FMCPSchemaBuilder::AddString(Schema, TEXT("label"), TEXT("Optional actor label shown in the scene outliner"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("spawn_niagara_system");
		Def.Description = TEXT("Spawn a Niagara particle system actor in the current level at a given world position. The system_path must point to a valid UNiagaraSystem asset in the content browser.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString SystemPath;
			if (!Args->TryGetStringField(TEXT("system_path"), SystemPath))
				return FMCPToolResult::Error(TEXT("system_path is required"));

			UNiagaraSystem* NiagaraSystem = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
			if (!IsValid(NiagaraSystem))
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to load NiagaraSystem asset at path: %s"), *SystemPath));

			FVector Location(
				Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
				Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
				Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
			);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Spawn Niagara System")));

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			ANiagaraActor* NiagaraActor = World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
			if (!IsValid(NiagaraActor))
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Failed to spawn ANiagaraActor in the level"));
			}

			UNiagaraComponent* NiagaraComponent = NiagaraActor->GetNiagaraComponent();
			if (!IsValid(NiagaraComponent))
			{
				NiagaraActor->Destroy();
				GEditor->EndTransaction();
				return FMCPToolResult::Error(TEXT("Spawned actor has no NiagaraComponent"));
			}

			NiagaraComponent->SetAsset(NiagaraSystem);

			FString Label;
			if (Args->TryGetStringField(TEXT("label"), Label) && !Label.IsEmpty())
			{
				NiagaraActor->SetActorLabel(Label);
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Spawned Niagara system '%s' as actor '%s' at (%.1f, %.1f, %.1f)"),
				*NiagaraSystem->GetName(),
				*NiagaraActor->GetActorLabel(),
				Location.X, Location.Y, Location.Z));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_niagara_parameter - Set a user parameter on a Niagara component
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor containing the NiagaraComponent"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("parameter_name"), TEXT("Name of the user-exposed Niagara parameter (with or without 'User.' prefix)"), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("value"), TEXT("New value as a string. Float: '1.5', Int: '3', Bool: 'true'/'false', Vector: 'X=1 Y=2 Z=3', Color: '(R=1,G=0,B=0,A=1)'"), true);
		FMCPSchemaBuilder::AddEnum(Schema, TEXT("type"), TEXT("Parameter type to parse the value as (default: Float)"),
			{ TEXT("Float"), TEXT("Int"), TEXT("Bool"), TEXT("Vector"), TEXT("Color") });

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_niagara_parameter");
		Def.Description = TEXT("Set a user-exposed parameter on a NiagaraComponent attached to an actor. Use get_niagara_parameters first to discover available parameter names and types.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			UWorld* World = GetEditorWorld();
			if (!World) return FMCPToolResult::Error(TEXT("No editor world available"));

			FString ActorName, ParamName, ValueStr;
			if (!Args->TryGetStringField(TEXT("actor_name"), ActorName))
				return FMCPToolResult::Error(TEXT("actor_name is required"));
			if (!Args->TryGetStringField(TEXT("parameter_name"), ParamName))
				return FMCPToolResult::Error(TEXT("parameter_name is required"));
			if (!Args->TryGetStringField(TEXT("value"), ValueStr))
				return FMCPToolResult::Error(TEXT("value is required"));

			AActor* Actor = FindActorByLabel(World, ActorName);
			if (!IsValid(Actor))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));

			UNiagaraComponent* NiagaraComponent = Actor->FindComponentByClass<UNiagaraComponent>();
			if (!IsValid(NiagaraComponent))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no NiagaraComponent"), *ActorName));

			FString TypeStr = TEXT("Float");
			Args->TryGetStringField(TEXT("type"), TypeStr);

			FName ParamFName(*ParamName);

			GEditor->BeginTransaction(FText::FromString(TEXT("MCP: Set Niagara Parameter")));
			NiagaraComponent->Modify();

			bool bSuccess = false;
			FString ErrorMessage;

			if (TypeStr == TEXT("Float"))
			{
				float FloatVal = FCString::Atof(*ValueStr);
				NiagaraComponent->SetVariableFloat(ParamFName, FloatVal);
				bSuccess = true;
			}
			else if (TypeStr == TEXT("Int"))
			{
				int32 IntVal = FCString::Atoi(*ValueStr);
				NiagaraComponent->SetVariableInt(ParamFName, IntVal);
				bSuccess = true;
			}
			else if (TypeStr == TEXT("Bool"))
			{
				bool bBoolVal = ValueStr.ToBool();
				NiagaraComponent->SetVariableBool(ParamFName, bBoolVal);
				bSuccess = true;
			}
			else if (TypeStr == TEXT("Vector"))
			{
				FVector VecVal = FVector::ZeroVector;
				// Accept "X=1 Y=2 Z=3" or "1,2,3" formats
				if (!VecVal.InitFromString(ValueStr))
				{
					TArray<FString> Parts;
					ValueStr.ParseIntoArray(Parts, TEXT(","));
					if (Parts.Num() >= 3)
					{
						VecVal.X = FCString::Atof(*Parts[0].TrimStartAndEnd());
						VecVal.Y = FCString::Atof(*Parts[1].TrimStartAndEnd());
						VecVal.Z = FCString::Atof(*Parts[2].TrimStartAndEnd());
					}
				}
				NiagaraComponent->SetVariableVec3(ParamFName, VecVal);
				bSuccess = true;
			}
			else if (TypeStr == TEXT("Color"))
			{
				FLinearColor ColorVal = FLinearColor::Black;
				// Accept "(R=1,G=0,B=0,A=1)" or "1,0,0,1" formats
				if (!ColorVal.InitFromString(ValueStr))
				{
					TArray<FString> Parts;
					ValueStr.ParseIntoArray(Parts, TEXT(","));
					if (Parts.Num() >= 3)
					{
						ColorVal.R = FCString::Atof(*Parts[0].TrimStartAndEnd());
						ColorVal.G = FCString::Atof(*Parts[1].TrimStartAndEnd());
						ColorVal.B = FCString::Atof(*Parts[2].TrimStartAndEnd());
						if (Parts.Num() >= 4)
						{
							ColorVal.A = FCString::Atof(*Parts[3].TrimStartAndEnd());
						}
					}
				}
				NiagaraComponent->SetVariableLinearColor(ParamFName, ColorVal);
				bSuccess = true;
			}
			else
			{
				ErrorMessage = FString::Printf(TEXT("Unknown type '%s'. Valid types: Float, Int, Bool, Vector, Color"), *TypeStr);
			}

			if (!bSuccess)
			{
				GEditor->EndTransaction();
				return FMCPToolResult::Error(ErrorMessage);
			}

			GEditor->EndTransaction();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Set Niagara parameter '%s' (%s) = '%s' on actor '%s'"),
				*ParamName, *TypeStr, *ValueStr, *ActorName));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_niagara_parameters - List user-exposed parameters on a Niagara component
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Label of the actor containing the NiagaraComponent"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_niagara_parameters");
		Def.Description = TEXT("List all user-exposed parameters on a Niagara system, including their names, types, and any current override values set on the component.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
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

			UNiagaraComponent* NiagaraComponent = Actor->FindComponentByClass<UNiagaraComponent>();
			if (!IsValid(NiagaraComponent))
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor '%s' has no NiagaraComponent"), *ActorName));

			UNiagaraSystem* NiagaraSystem = NiagaraComponent->GetAsset();
			if (!IsValid(NiagaraSystem))
				return FMCPToolResult::Error(FString::Printf(TEXT("NiagaraComponent on '%s' has no system asset assigned"), *ActorName));

			// Gather user-exposed parameters from the system's exposed parameter store
			TArray<FNiagaraVariable> UserParams;
			NiagaraSystem->GetExposedParameters().GetUserParameters(UserParams);

			if (UserParams.Num() == 0)
			{
				TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetStringField(TEXT("actor"), ActorName);
				Result->SetStringField(TEXT("system"), NiagaraSystem->GetName());
				Result->SetArrayField(TEXT("parameters"), TArray<TSharedPtr<FJsonValue>>());
				Result->SetNumberField(TEXT("count"), 0);
				return FMCPToolResult::Success(JsonToString(Result));
			}

			// Also get the component's override parameters to read current values
			const FNiagaraParameterStore& OverrideStore = NiagaraComponent->GetOverrideParameters();

			TArray<TSharedPtr<FJsonValue>> ParamArray;

			for (const FNiagaraVariable& Var : UserParams)
			{
				const FNiagaraTypeDefinition& TypeDef = Var.GetType();

				// Determine friendly type name
				FString TypeName = TEXT("Unknown");
				if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
				{
					TypeName = TEXT("Float");
				}
				else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
				{
					TypeName = TEXT("Int");
				}
				else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
				{
					TypeName = TEXT("Bool");
				}
				else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
				{
					TypeName = TEXT("Color");
				}
				else if (TypeDef == FNiagaraTypeHelper::GetVectorDef())
				{
					TypeName = TEXT("Vector");
				}
				else if (TypeDef.GetStruct() != nullptr)
				{
					TypeName = TypeDef.GetStruct()->GetName();
				}
				else if (TypeDef.GetClass() != nullptr)
				{
					TypeName = TypeDef.GetClass()->GetName();
				}

				TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				ParamObj->SetStringField(TEXT("name"), Var.GetName().ToString());
				ParamObj->SetStringField(TEXT("type"), TypeName);

				// Attempt to read current override value from the component
				FString CurrentValue = TEXT("(no override)");
				const FNiagaraVariableWithOffset* FoundVar = OverrideStore.FindParameterVariable(Var);
				if (FoundVar != nullptr)
				{
					if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
					{
						float Val = OverrideStore.GetParameterValue<float>(Var);
						CurrentValue = FString::Printf(TEXT("%.4f"), Val);
					}
					else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
					{
						int32 Val = OverrideStore.GetParameterValue<int32>(Var);
						CurrentValue = FString::FromInt(Val);
					}
					else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
					{
						FNiagaraBool Val = OverrideStore.GetParameterValue<FNiagaraBool>(Var);
						CurrentValue = Val.GetValue() ? TEXT("true") : TEXT("false");
					}
					else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
					{
						FLinearColor Val = OverrideStore.GetParameterValue<FLinearColor>(Var);
						CurrentValue = FString::Printf(TEXT("(R=%.3f,G=%.3f,B=%.3f,A=%.3f)"), Val.R, Val.G, Val.B, Val.A);
					}
					else if (TypeDef == FNiagaraTypeHelper::GetVectorDef())
					{
						FVector Val = OverrideStore.GetParameterValue<FVector>(Var);
						CurrentValue = FString::Printf(TEXT("(X=%.3f,Y=%.3f,Z=%.3f)"), Val.X, Val.Y, Val.Z);
					}
					else
					{
						CurrentValue = TEXT("(complex type)");
					}
				}

				ParamObj->SetStringField(TEXT("current_value"), CurrentValue);
				ParamArray.Add(MakeShared<FJsonValueObject>(ParamObj));
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("actor"), ActorName);
			Result->SetStringField(TEXT("system"), NiagaraSystem->GetName());
			Result->SetArrayField(TEXT("parameters"), ParamArray);
			Result->SetNumberField(TEXT("count"), ParamArray.Num());

			return FMCPToolResult::Success(JsonToString(Result));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPNiagaraTools
