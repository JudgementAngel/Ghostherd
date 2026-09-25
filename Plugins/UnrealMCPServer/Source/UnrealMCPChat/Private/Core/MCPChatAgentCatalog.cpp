// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatAgentCatalog.h"
#include "MCPChatStore.h"
#include "UnrealMCPChatModule.h"

#include "MCPHttpServer.h"
#include "MCPSettings.h"

#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	EMCPAgentMcpInjection InjectionFromString(const FString& In)
	{
		if (In.Equals(TEXT("env"), ESearchCase::IgnoreCase))    { return EMCPAgentMcpInjection::Environment; }
		if (In.Equals(TEXT("manual"), ESearchCase::IgnoreCase)) { return EMCPAgentMcpInjection::Manual; }
		if (In.Equals(TEXT("none"), ESearchCase::IgnoreCase))   { return EMCPAgentMcpInjection::None; }
		return EMCPAgentMcpInjection::Flag;
	}

	/** Quote an argument only when it needs it. Quoting everything breaks agents
	 *  that compare argv entries literally; quoting nothing breaks every path with
	 *  a space in it, which on Windows is most of them. */
	FString QuoteIfNeeded(const FString& Arg)
	{
		if (Arg.IsEmpty()) { return TEXT("\"\""); }
		if (!Arg.Contains(TEXT(" ")) && !Arg.Contains(TEXT("\t"))) { return Arg; }
		if (Arg.StartsWith(TEXT("\""))) { return Arg; }
		return FString::Printf(TEXT("\"%s\""), *Arg);
	}
}

FMCPChatAgentCatalog& FMCPChatAgentCatalog::Get()
{
	static FMCPChatAgentCatalog Instance;
	return Instance;
}

FString FMCPChatAgentCatalog::GetShippedCatalogPath()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealMCPServer"));
	const FString Base = Plugin.IsValid()
		? Plugin->GetBaseDir()
		: FPaths::ProjectPluginsDir() / TEXT("UnrealMCPServer");
	return Base / TEXT("Config") / TEXT("DefaultChatAgents.json");
}

FString FMCPChatAgentCatalog::GetUserCatalogPath()
{
	return FMCPChatStore::GetRootDirectory() / TEXT("agents.json");
}

FString FMCPChatAgentCatalog::GetAgentDirectory(const FString& AgentId)
{
	return FMCPChatStore::GetRootDirectory() / TEXT("agents") / AgentId;
}

void FMCPChatAgentCatalog::Load()
{
	Agents.Reset();
	MergeFromFile(GetShippedCatalogPath());
	// User file second, so a hand-edited entry overrides the shipped one of the same
	// id rather than being overwritten by a plugin update.
	MergeFromFile(GetUserCatalogPath());
	bLoaded = true;

	UE_LOG(LogUnrealMCPChat, Log, TEXT("Agent catalogue: %d agent(s)."), Agents.Num());
}

void FMCPChatAgentCatalog::MergeFromFile(const FString& Path)
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path)) { return; }

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Could not parse agent catalogue '%s'."), *Path);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* AgentArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("agents"), AgentArray) || !AgentArray) { return; }

	for (const TSharedPtr<FJsonValue>& Value : *AgentArray)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Obj) || !Obj) { continue; }

		FMCPChatAgentInfo A;
		if (!(*Obj)->TryGetStringField(TEXT("id"), A.AgentId) || A.AgentId.IsEmpty()) { continue; }

		(*Obj)->TryGetStringField(TEXT("display"), A.DisplayName);
		(*Obj)->TryGetStringField(TEXT("command"), A.Command);
		(*Obj)->TryGetStringField(TEXT("installHint"), A.InstallHint);
		(*Obj)->TryGetStringField(TEXT("notes"), A.Notes);
		(*Obj)->TryGetBoolField(TEXT("enabled"), A.bEnabled);

		const TArray<TSharedPtr<FJsonValue>>* Args = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("args"), Args) && Args)
		{
			for (const TSharedPtr<FJsonValue>& Arg : *Args)
			{
				FString S;
				if (Arg.IsValid() && Arg->TryGetString(S)) { A.Arguments.Add(S); }
			}
		}

		const TSharedPtr<FJsonObject>* Env = nullptr;
		if ((*Obj)->TryGetObjectField(TEXT("env"), Env) && Env)
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Env)->Values)
			{
				if (Pair.Value.IsValid()) { A.Environment.Add(Pair.Key, Pair.Value->AsString()); }
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ModelArray = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("models"), ModelArray) && ModelArray)
		{
			for (const TSharedPtr<FJsonValue>& ModelValue : *ModelArray)
			{
				const TSharedPtr<FJsonObject>* ModelObj = nullptr;
				if (!ModelValue.IsValid() || !ModelValue->TryGetObject(ModelObj) || !ModelObj) { continue; }

				FString ModelId, ModelDisplay;
				if (!(*ModelObj)->TryGetStringField(TEXT("id"), ModelId) || ModelId.IsEmpty()) { continue; }
				(*ModelObj)->TryGetStringField(TEXT("display"), ModelDisplay);
				A.Models.Emplace(ModelId, ModelDisplay.IsEmpty() ? ModelId : ModelDisplay);
			}
		}
		(*Obj)->TryGetStringField(TEXT("modelEnvVar"), A.ModelEnvVar);
		(*Obj)->TryGetStringField(TEXT("modelArgFlag"), A.ModelArgFlag);

		FString Injection;
		if ((*Obj)->TryGetStringField(TEXT("mcpInjection"), Injection))
		{
			A.McpInjection = InjectionFromString(Injection);
		}
		(*Obj)->TryGetStringField(TEXT("mcpConfigFlag"), A.McpConfigFlag);

		if (A.DisplayName.IsEmpty()) { A.DisplayName = A.AgentId; }

		Agents.RemoveAll([&A](const FMCPChatAgentInfo& Existing) { return Existing.AgentId == A.AgentId; });
		Agents.Add(MoveTemp(A));
	}
}

const FMCPChatAgentInfo* FMCPChatAgentCatalog::FindAgent(const FString& AgentId) const
{
	return Agents.FindByPredicate(
		[&AgentId](const FMCPChatAgentInfo& A) { return A.AgentId == AgentId; });
}

FString FMCPChatAgentCatalog::BuildArgumentString(const FMCPChatAgentInfo& Agent,
                                                  const FString& McpConfigPath, const FString& ModelId)
{
	TArray<FString> Parts;
	for (const FString& Arg : Agent.Arguments)
	{
		Parts.Add(QuoteIfNeeded(Arg));
	}

	// The model flag goes before the MCP config flag only because it reads better in
	// the log line; neither CLI cares about order.
	if (!ModelId.IsEmpty() && !Agent.ModelArgFlag.IsEmpty())
	{
		Parts.Add(Agent.ModelArgFlag);
		Parts.Add(QuoteIfNeeded(ModelId));
	}

	if (Agent.McpInjection == EMCPAgentMcpInjection::Flag
		&& !Agent.McpConfigFlag.IsEmpty() && !McpConfigPath.IsEmpty())
	{
		Parts.Add(Agent.McpConfigFlag);
		Parts.Add(QuoteIfNeeded(McpConfigPath));
	}

	return FString::Join(Parts, TEXT(" "));
}

FString FMCPChatAgentCatalog::WriteMcpConfig(const FString& AgentId)
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (!Server.IsRunning())
	{
		// Not an error: the agent still works, it just has no editor tools. The
		// backend reports that as a notice rather than refusing to start, because a
		// user debugging the server needs the agent to talk to them about it.
		UE_LOG(LogUnrealMCPChat, Warning,
			TEXT("MCP server is not running; agent '%s' will start without editor tools."), *AgentId);
		return FString();
	}

	TSharedPtr<FJsonObject> Unreal = MakeShared<FJsonObject>();
	Unreal->SetStringField(TEXT("type"), TEXT("http"));
	Unreal->SetStringField(TEXT("url"),
		FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), Server.GetPort()));

	// Auth: when the server demands a token, the agent must carry one or every call
	// it makes 401s. It inherits the FIRST configured token, and therefore that
	// token's scope — the agent cannot be given more access than that token has.
	const UMCPSettings* ServerSettings = GetDefault<UMCPSettings>();
	if (ServerSettings && ServerSettings->bRequireAuthToken && ServerSettings->AuthTokens.Num() > 0)
	{
		TSharedPtr<FJsonObject> Headers = MakeShared<FJsonObject>();
		Headers->SetStringField(TEXT("Authorization"),
			FString::Printf(TEXT("Bearer %s"), *ServerSettings->AuthTokens[0]));
		Unreal->SetObjectField(TEXT("headers"), Headers);
	}

	TSharedPtr<FJsonObject> Servers = MakeShared<FJsonObject>();
	Servers->SetObjectField(TEXT("unreal"), Unreal);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetObjectField(TEXT("mcpServers"), Servers);

	FString Out;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	const FString Directory = GetAgentDirectory(AgentId);
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.DirectoryExists(*Directory) && !PF.CreateDirectoryTree(*Directory))
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Could not create '%s' for the agent's MCP config."), *Directory);
		return FString();
	}

	const FString Path = Directory / TEXT("mcp.json");
	if (!FFileHelper::SaveStringToFile(Out, *Path))
	{
		UE_LOG(LogUnrealMCPChat, Warning, TEXT("Could not write '%s'."), *Path);
		return FString();
	}

	// NOT logged with its contents: when auth is on, that string is a bearer token,
	// and a token in the Output Log is a token in every crash report and screenshot.
	UE_LOG(LogUnrealMCPChat, Verbose, TEXT("Wrote MCP config for '%s'."), *AgentId);

	return Path;
}
