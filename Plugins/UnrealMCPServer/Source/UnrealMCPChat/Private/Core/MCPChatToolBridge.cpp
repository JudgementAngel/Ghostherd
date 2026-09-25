// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatToolBridge.h"
#include "MCPChatSettings.h"
#include "UnrealMCPChatModule.h"

#include "MCPToolRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "MCPChatToolBridge"

FMCPChatToolBridge& FMCPChatToolBridge::Get()
{
	static FMCPChatToolBridge Instance;
	return Instance;
}

EMCPScope FMCPChatToolBridge::ScopeForMode(EMCPChatApprovalMode Mode)
{
	switch (Mode)
	{
	case EMCPChatApprovalMode::ReadOnly:  return EMCPScope::Read;
	case EMCPChatApprovalMode::AllowAll:  return EMCPScope::Destructive;
	// Ask modes still need Destructive scope available, because the user CAN say
	// yes. The gate decides whether we get that far; the scope decides whether the
	// registry will run it once approved.
	default:                              return EMCPScope::Destructive;
	}
}

// ============================================================================
// Schema conversion
// ============================================================================

TArray<TSharedPtr<FJsonValue>> FMCPChatToolBridge::BuildSchemas(bool bCatalogMode, bool bOpenAIShape) const
{
	const TArray<FMCPToolDefinition> Tools =
		FMCPToolRegistry::Get().GetToolDefinitionsForExposure(bCatalogMode);

	TArray<TSharedPtr<FJsonValue>> Out;
	Out.Reserve(Tools.Num());

	for (const FMCPToolDefinition& Tool : Tools)
	{
		// A tool with no schema would make the model guess at arguments; skip it
		// rather than send something the provider will reject.
		if (!Tool.InputSchema.IsValid())
		{
			continue;
		}

		if (bOpenAIShape)
		{
			TSharedPtr<FJsonObject> Function = MakeShared<FJsonObject>();
			Function->SetStringField(TEXT("name"), Tool.Name);
			Function->SetStringField(TEXT("description"), Tool.Description);
			Function->SetObjectField(TEXT("parameters"), Tool.InputSchema);

			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), TEXT("function"));
			Entry->SetObjectField(TEXT("function"), Function);
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
		else
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Tool.Name);
			Entry->SetStringField(TEXT("description"), Tool.Description);
			Entry->SetObjectField(TEXT("input_schema"), Tool.InputSchema);
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	return Out;
}

TArray<TSharedPtr<FJsonValue>> FMCPChatToolBridge::BuildAnthropicToolSchemas(bool bCatalogMode) const
{
	return BuildSchemas(bCatalogMode, /*bOpenAIShape*/ false);
}

TArray<TSharedPtr<FJsonValue>> FMCPChatToolBridge::BuildOpenAIToolSchemas(bool bCatalogMode) const
{
	return BuildSchemas(bCatalogMode, /*bOpenAIShape*/ true);
}

int32 FMCPChatToolBridge::EstimateSchemaTokens(bool bCatalogMode) const
{
	const TArray<TSharedPtr<FJsonValue>> Schemas = BuildAnthropicToolSchemas(bCatalogMode);

	FString Serialized;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Schemas, Writer);

	// ~3.7 chars/token for English prose, but JSON schemas are punctuation-dense
	// and tokenize worse; 3.0 is a closer fit and errs high, which is the safe
	// direction for a number shown next to a cost.
	return FMath::CeilToInt(Serialized.Len() / 3.0f);
}

// ============================================================================
// Introspection
// ============================================================================

bool FMCPChatToolBridge::IsToolReadOnly(const FString& ToolName) const
{
	const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(ToolName);
	return Tool && Tool->bReadOnlyHint;
}

bool FMCPChatToolBridge::IsToolDestructive(const FString& ToolName) const
{
	const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(ToolName);
	return Tool && Tool->bDestructiveHint;
}

FText FMCPChatToolBridge::GetToolDescription(const FString& ToolName) const
{
	const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(ToolName);
	return Tool ? FText::FromString(Tool->Description) : FText::GetEmpty();
}

// ============================================================================
// Approval gate
// ============================================================================

EMCPChatGateResult FMCPChatToolBridge::EvaluateGate(const FString& ToolName, const FGuid& SessionId,
	FText& OutReason) const
{
	const FMCPToolDefinition* Tool = FMCPToolRegistry::Get().FindTool(ToolName);
	if (!Tool)
	{
		OutReason = FText::Format(LOCTEXT("UnknownTool", "Unknown tool '{0}'."), FText::FromString(ToolName));
		return EMCPChatGateResult::Blocked;
	}

	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const EMCPChatApprovalMode Mode = GetEffectiveMode(SessionId);

	// Deny list wins over everything, including AllowAll — it is the user's explicit
	// "never this one", and a blanket setting must not silently override it.
	if (Settings && Settings->BlockedTools.Contains(ToolName))
	{
		OutReason = FText::Format(
			LOCTEXT("ToolBlocked", "'{0}' is on the blocked list in Settings."), FText::FromString(ToolName));
		return EMCPChatGateResult::Blocked;
	}

	// Read-only mode refuses every mutation outright rather than asking — the whole
	// point of the mode is that there is nothing to say yes to.
	if (Mode == EMCPChatApprovalMode::ReadOnly && !Tool->bReadOnlyHint)
	{
		OutReason = FText::Format(
			LOCTEXT("ReadOnlyMode", "'{0}' changes the project, and this session is read-only."),
			FText::FromString(ToolName));
		return EMCPChatGateResult::Blocked;
	}

	// PIE gate: checked here as well as in the registry so the user gets a useful
	// card instead of a raw tool error.
	if (Tool->bRequiresPieOff && GEditor && GEditor->IsPlaySessionInProgress())
	{
		OutReason = FText::Format(
			LOCTEXT("PieActive", "'{0}' cannot run while Play-In-Editor is active."),
			FText::FromString(ToolName));
		return EMCPChatGateResult::Blocked;
	}

	if (Mode == EMCPChatApprovalMode::AllowAll)
	{
		return EMCPChatGateResult::Allow;
	}

	if (Tool->bReadOnlyHint)
	{
		return EMCPChatGateResult::Allow;   // reads never ask, in any ask-mode
	}

	// Standing grants.
	if (Settings && Settings->AlwaysAllowedTools.Contains(ToolName))
	{
		return EMCPChatGateResult::Allow;
	}
	if (const TSet<FGuid>* Sessions = SessionGrants.Find(ToolName))
	{
		if (Sessions->Contains(SessionId))
		{
			return EMCPChatGateResult::Allow;
		}
	}

	if (Mode == EMCPChatApprovalMode::AskWrites)
	{
		OutReason = FText::Format(LOCTEXT("AsksWrite", "'{0}' modifies the project."), FText::FromString(ToolName));
		return EMCPChatGateResult::NeedsApproval;
	}

	// AskDestructive
	if (Tool->bDestructiveHint)
	{
		OutReason = FText::Format(
			LOCTEXT("AsksDestructive", "'{0}' is destructive and cannot be undone by Ctrl+Z alone."),
			FText::FromString(ToolName));
		return EMCPChatGateResult::NeedsApproval;
	}

	return EMCPChatGateResult::Allow;
}

void FMCPChatToolBridge::GrantForSession(const FString& ToolName, const FGuid& SessionId)
{
	SessionGrants.FindOrAdd(ToolName).Add(SessionId);
}

void FMCPChatToolBridge::GrantAlways(const FString& ToolName)
{
	UMCPChatSettings* Settings = GetMutableDefault<UMCPChatSettings>();
	if (!Settings) { return; }

	Settings->AlwaysAllowedTools.AddUnique(ToolName);
	Settings->SaveConfig();

	// Say it out loud: a standing grant is exactly the setting a user forgets they
	// made, so it is logged and it is visible/removable in Settings.
	UE_LOG(LogUnrealMCPChat, Log,
		TEXT("'%s' is now always allowed without asking. Remove it in Settings ▸ Permissions."),
		*ToolName);
}

void FMCPChatToolBridge::RevokeAlways(const FString& ToolName)
{
	UMCPChatSettings* Settings = GetMutableDefault<UMCPChatSettings>();
	if (!Settings) { return; }

	Settings->AlwaysAllowedTools.Remove(ToolName);
	Settings->SaveConfig();
}

void FMCPChatToolBridge::ClearSessionGrants(const FGuid& SessionId)
{
	for (auto It = SessionGrants.CreateIterator(); It; ++It)
	{
		It.Value().Remove(SessionId);
		if (It.Value().Num() == 0) { It.RemoveCurrent(); }
	}
}

// ============================================================================
// Session ceiling (Phase 6, `/scope`)
// ============================================================================

int32 FMCPChatToolBridge::StrictnessRank(EMCPChatApprovalMode Mode)
{
	switch (Mode)
	{
	case EMCPChatApprovalMode::ReadOnly:       return 0;
	case EMCPChatApprovalMode::AskWrites:      return 1;
	case EMCPChatApprovalMode::AskDestructive: return 2;
	case EMCPChatApprovalMode::AllowAll:       return 3;
	default:                                   return 2;
	}
}

void FMCPChatToolBridge::SetSessionCeiling(const FGuid& SessionId, EMCPChatApprovalMode Ceiling)
{
	SessionCeilings.Add(SessionId, Ceiling);
}

void FMCPChatToolBridge::ClearSessionCeiling(const FGuid& SessionId)
{
	SessionCeilings.Remove(SessionId);
}

bool FMCPChatToolBridge::GetSessionCeiling(const FGuid& SessionId, EMCPChatApprovalMode& OutCeiling) const
{
	if (const EMCPChatApprovalMode* Found = SessionCeilings.Find(SessionId))
	{
		OutCeiling = *Found;
		return true;
	}
	return false;
}

EMCPChatApprovalMode FMCPChatToolBridge::GetEffectiveMode(const FGuid& SessionId) const
{
	const UMCPChatSettings* Settings = UMCPChatSettings::Get();
	const EMCPChatApprovalMode Global = Settings ? Settings->ApprovalMode : EMCPChatApprovalMode::AskDestructive;

	const EMCPChatApprovalMode* Ceiling = SessionCeilings.Find(SessionId);
	if (!Ceiling) { return Global; }

	// Stricter always wins. `/scope` can tighten a session; it can never loosen one
	// past what the user chose globally.
	return (StrictnessRank(*Ceiling) < StrictnessRank(Global)) ? *Ceiling : Global;
}

// ============================================================================
// Execution
// ============================================================================

FMCPChatToolBridge::FExecutionResult FMCPChatToolBridge::Execute(const FString& ToolName,
	const TSharedPtr<FJsonObject>& Args, const FGuid& SessionId, EMCPChatApprovalMode Mode,
	const TSharedPtr<TAtomic<bool>>& CancelFlag,
	TFunction<void(float, const FString&)> ProgressSink) const
{
	// The registry asserts this too, but failing here gives a far clearer stack.
	check(IsInGameThread());

	FExecutionResult Result;
	const double Start = FPlatformTime::Seconds();

	FMCPRequestContext Context;
	Context.Scope     = ScopeForMode(Mode);
	Context.SessionId = SessionId.ToString(EGuidFormats::DigitsWithHyphens);
	Context.CancelFlag = CancelFlag;
	Context.ProgressSink = MoveTemp(ProgressSink);
	// Long-running tools must NOT be converted to background tasks here: we are
	// already on the game thread, and the chat's own turn loop is what waits.
	Context.bAllowAsyncTask = false;

	const FMCPToolResult ToolResult = FMCPToolRegistry::Get().ExecuteTool(
		ToolName, Args.IsValid() ? Args : MakeShared<FJsonObject>(), Context);

	Result.DurationSeconds = FPlatformTime::Seconds() - Start;
	Result.bIsError        = ToolResult.bIsError;
	Result.Structured      = ToolResult.StructuredContent;

	// Flatten content blocks to the text the model sees. Images returned by a tool
	// (screenshots) are noted but not inlined here — Phase 6 attaches them properly.
	TArray<FString> Parts;
	for (const FMCPContentBlock& Block : ToolResult.Content)
	{
		if (Block.Type == TEXT("text"))
		{
			Parts.Add(Block.Text);
		}
		else if (Block.Type == TEXT("image"))
		{
			Parts.Add(TEXT("[image returned by tool]"));
		}
	}
	Result.Text = FString::Join(Parts, TEXT("\n"));

	if (Result.Text.IsEmpty())
	{
		// A tool that returns nothing still needs a tool_result, or the provider
		// rejects the follow-up for having an unanswered tool_use.
		Result.Text = Result.bIsError ? TEXT("The tool failed and returned no detail.")
		                              : TEXT("Done.");
	}

	// Surface a task handle so the card can poll instead of looking stuck.
	if (Result.Structured.IsValid())
	{
		Result.Structured->TryGetStringField(TEXT("task_id"), Result.TaskId);
	}

	UE_LOG(LogUnrealMCPChat, Verbose, TEXT("Tool '%s' ran in %.3fs (%s)"),
		*ToolName, Result.DurationSeconds, Result.bIsError ? TEXT("error") : TEXT("ok"));

	return Result;
}

#undef LOCTEXT_NAMESPACE
