// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IMCPChatBackend.h"

/**
 * Phase 4 — provider and model metadata, loaded from Config/DefaultChatModels.json.
 *
 * WHY THIS IS DATA: provider APIs drift faster than plugin releases. A hardcoded
 * C++ model list is wrong within a month, and the failure mode is a 400 the user
 * cannot fix without a new build. Here, a wrong capability flag is a one-line JSON
 * edit — and the settings UI renders itself from these flags, so a model that
 * rejects `temperature` simply has no slider.
 */

struct UNREALMCPCHAT_API FMCPChatProviderInfo
{
	FString ProviderId;
	FString DisplayName;
	FString BaseUrl;
	/** "anthropic" or "openai" — selects the request/response shape. */
	FString WireFormat = TEXT("openai");
	FString AuthHeader = TEXT("Authorization");
	FString AuthPrefix = TEXT("Bearer ");
	bool    bRequiresKey = true;
	TMap<FString, FString> ExtraHeaders;
};

class UNREALMCPCHAT_API FMCPChatModelCatalog
{
public:
	static FMCPChatModelCatalog& Get();

	/** Load (or reload) from disk. User overrides in Saved/ win over the shipped file
	 *  so a plugin update never clobbers a hand-added model. */
	void Load();

	const TArray<FChatModelInfo>&        GetModels() const    { return Models; }
	const TArray<FMCPChatProviderInfo>&  GetProviders() const { return Providers; }

	const FChatModelInfo*       FindModel(const FString& ModelId) const;
	const FMCPChatProviderInfo* FindProvider(const FString& ProviderId) const;

	TArray<FChatModelInfo> GetModelsForProvider(const FString& ProviderId) const;

	/** Cost estimate in USD for a completed turn. */
	double EstimateCost(const FString& ModelId, const FChatUsage& Usage) const;

	/** Where a user's own additions live. */
	static FString GetUserCatalogPath();
	static FString GetShippedCatalogPath();

private:
	FMCPChatModelCatalog() = default;
	void MergeFromFile(const FString& Path);

	TArray<FChatModelInfo>       Models;
	TArray<FMCPChatProviderInfo> Providers;
	bool bLoaded = false;
};
