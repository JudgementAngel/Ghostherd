// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatModelCatalog.h"
#include "UnrealMCPChatModule.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FMCPChatModelCatalog& FMCPChatModelCatalog::Get()
{
	static FMCPChatModelCatalog Instance;
	if (!Instance.bLoaded)
	{
		Instance.Load();
	}
	return Instance;
}

FString FMCPChatModelCatalog::GetShippedCatalogPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealMCPServer"));
	const FString Base = Plugin.IsValid() ? Plugin->GetBaseDir()
	                                      : FPaths::ProjectPluginsDir() / TEXT("UnrealMCPServer");
	return Base / TEXT("Config") / TEXT("DefaultChatModels.json");
}

FString FMCPChatModelCatalog::GetUserCatalogPath()
{
	// Under Saved/ so a plugin update cannot overwrite the user's additions.
	return FPaths::ProjectSavedDir() / TEXT("UnrealMCPChat") / TEXT("ChatModels.json");
}

void FMCPChatModelCatalog::Load()
{
	Models.Reset();
	Providers.Reset();

	MergeFromFile(GetShippedCatalogPath());
	MergeFromFile(GetUserCatalogPath());   // second, so user entries win

	bLoaded = true;
	UE_LOG(LogUnrealMCPChat, Log, TEXT("Model catalogue: %d provider(s), %d model(s)."),
		Providers.Num(), Models.Num());
}

void FMCPChatModelCatalog::MergeFromFile(const FString& Path)
{
	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *Path))
	{
		return;   // absent user file is the normal case
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogUnrealMCPChat, Error, TEXT("'%s' is not valid JSON; ignoring it."), *Path);
		return;
	}

	// ---- Providers ----
	const TArray<TSharedPtr<FJsonValue>>* ProviderArray = nullptr;
	if (Root->TryGetArrayField(TEXT("providers"), ProviderArray) && ProviderArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *ProviderArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }

			FMCPChatProviderInfo P;
			if (!(*Obj)->TryGetStringField(TEXT("id"), P.ProviderId) || P.ProviderId.IsEmpty())
			{
				continue;
			}
			(*Obj)->TryGetStringField(TEXT("displayName"), P.DisplayName);
			(*Obj)->TryGetStringField(TEXT("baseUrl"), P.BaseUrl);
			(*Obj)->TryGetStringField(TEXT("wireFormat"), P.WireFormat);
			(*Obj)->TryGetStringField(TEXT("authHeader"), P.AuthHeader);
			(*Obj)->TryGetStringField(TEXT("authPrefix"), P.AuthPrefix);
			(*Obj)->TryGetBoolField(TEXT("requiresKey"), P.bRequiresKey);

			const TSharedPtr<FJsonObject>* Headers = nullptr;
			if ((*Obj)->TryGetObjectField(TEXT("extraHeaders"), Headers) && Headers)
			{
				for (const auto& Pair : (*Headers)->Values)
				{
					if (Pair.Value.IsValid())
					{
						// UE 5.8 keys FJsonObject::Values by UE::FSharedString, which does
						// not convert to FString implicitly. Dereferencing gives the TCHAR*
						// both storage variants expose, so this stays version-agnostic.
						P.ExtraHeaders.Add(FString(*Pair.Key), Pair.Value->AsString());
					}
				}
			}

			Providers.RemoveAll([&P](const FMCPChatProviderInfo& Existing)
			{
				return Existing.ProviderId == P.ProviderId;
			});
			Providers.Add(MoveTemp(P));
		}
	}

	// ---- Models ----
	const TArray<TSharedPtr<FJsonValue>>* ModelArray = nullptr;
	if (Root->TryGetArrayField(TEXT("models"), ModelArray) && ModelArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *ModelArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) { continue; }

			FChatModelInfo M;
			if (!(*Obj)->TryGetStringField(TEXT("id"), M.ModelId) || M.ModelId.IsEmpty())
			{
				continue;
			}
			(*Obj)->TryGetStringField(TEXT("displayName"), M.DisplayName);
			(*Obj)->TryGetStringField(TEXT("provider"), M.ProviderId);

			double N = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("context"), N))   { M.ContextTokens = static_cast<int32>(N); }
			if ((*Obj)->TryGetNumberField(TEXT("maxOutput"), N)) { M.MaxOutputTokens = static_cast<int32>(N); }
			(*Obj)->TryGetNumberField(TEXT("priceIn"), M.PriceInPerMillion);
			(*Obj)->TryGetNumberField(TEXT("priceOut"), M.PriceOutPerMillion);

			(*Obj)->TryGetBoolField(TEXT("vision"), M.bVision);
			(*Obj)->TryGetBoolField(TEXT("tools"), M.bTools);

			// Defaults to FALSE when absent. A missing flag must not cause us to send
			// temperature to a model that rejects it — the safe default is "omit".
			M.bSampling = false;
			(*Obj)->TryGetBoolField(TEXT("sampling"), M.bSampling);

			if ((*Obj)->TryGetNumberField(TEXT("cacheMinTokens"), N))
			{
				M.CacheMinTokens = static_cast<int32>(N);
			}

			const TArray<TSharedPtr<FJsonValue>>* EffortArray = nullptr;
			if ((*Obj)->TryGetArrayField(TEXT("effort"), EffortArray) && EffortArray)
			{
				for (const TSharedPtr<FJsonValue>& E : *EffortArray)
				{
					if (E.IsValid()) { M.EffortLevels.Add(E->AsString()); }
				}
			}

			if (M.DisplayName.IsEmpty()) { M.DisplayName = M.ModelId; }

			Models.RemoveAll([&M](const FChatModelInfo& Existing)
			{
				return Existing.ModelId == M.ModelId;
			});
			Models.Add(MoveTemp(M));
		}
	}
}

const FChatModelInfo* FMCPChatModelCatalog::FindModel(const FString& ModelId) const
{
	return Models.FindByPredicate([&ModelId](const FChatModelInfo& M)
	{
		return M.ModelId == ModelId;
	});
}

const FMCPChatProviderInfo* FMCPChatModelCatalog::FindProvider(const FString& ProviderId) const
{
	return Providers.FindByPredicate([&ProviderId](const FMCPChatProviderInfo& P)
	{
		return P.ProviderId == ProviderId;
	});
}

TArray<FChatModelInfo> FMCPChatModelCatalog::GetModelsForProvider(const FString& ProviderId) const
{
	return Models.FilterByPredicate([&ProviderId](const FChatModelInfo& M)
	{
		return M.ProviderId == ProviderId;
	});
}

double FMCPChatModelCatalog::EstimateCost(const FString& ModelId, const FChatUsage& Usage) const
{
	const FChatModelInfo* Model = FindModel(ModelId);
	if (!Model) { return 0.0; }

	// Cache reads bill at roughly 0.1x and cache writes at roughly 1.25x of the
	// input rate. Labelled an estimate everywhere it is shown, for exactly this
	// reason — the real invoice is the provider's.
	const double InRate  = Model->PriceInPerMillion / 1'000'000.0;
	const double OutRate = Model->PriceOutPerMillion / 1'000'000.0;

	return Usage.InputTokens      * InRate
	     + Usage.OutputTokens     * OutRate
	     + Usage.CacheReadTokens  * InRate * 0.1
	     + Usage.CacheWriteTokens * InRate * 1.25;
}
