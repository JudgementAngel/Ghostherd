#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FUnrealMCPServerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FUnrealMCPServerModule& Get()
	{
		return FModuleManager::GetModuleChecked<FUnrealMCPServerModule>(TEXT("UnrealMCPServer"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("UnrealMCPServer"));
	}

private:
	void RegisterAllTools();
	void RegisterAllResources();
	void RegisterAllPrompts();
	void RegisterStatusBarWidget();
};
