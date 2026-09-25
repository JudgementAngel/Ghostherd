// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Core/MCPChatCommands.h"
#include "MCPChatController.h"
#include "MCPChatModelCatalog.h"
#include "MCPChatStore.h"
#include "MCPChatToolBridge.h"
#include "UnrealMCPChatModule.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "MCPChatCommands"

const TArray<FChatSlashCommand>& FMCPChatSlashCommands::GetAll()
{
	static const TArray<FChatSlashCommand> Commands =
	{
		{ TEXT("new"),     TEXT(""),          LOCTEXT("CmdNew",     "Start a new conversation, keeping the current model") },
		{ TEXT("clear"),   TEXT(""),          LOCTEXT("CmdClear",   "Empty this conversation but keep it") },
		{ TEXT("model"),   TEXT("<name>"),    LOCTEXT("CmdModel",   "Switch model; with no argument, opens the picker") },
		{ TEXT("agent"),   TEXT("<name>"),    LOCTEXT("CmdAgent",   "Switch to a local agent backend"), { TEXT("backend") } },
		{ TEXT("compact"), TEXT(""),          LOCTEXT("CmdCompact", "Replace older messages with a local summary (no model call)") },
		{ TEXT("export"),  TEXT("md|json"),   LOCTEXT("CmdExport",  "Write this conversation to Saved/UnrealMCPChat/exports") },
		{ TEXT("title"),   TEXT("<text>"),    LOCTEXT("CmdTitle",   "Rename this conversation") },
		{ TEXT("pin"),     TEXT(""),          LOCTEXT("CmdPin",     "Pin this conversation to the top of the history rail") },
		{ TEXT("unpin"),   TEXT(""),          LOCTEXT("CmdUnpin",   "Unpin this conversation") },
		{ TEXT("tools"),   TEXT(""),          LOCTEXT("CmdTools",   "Browse the editor tools available to the model") },
		{ TEXT("scope"),   TEXT("read|write|destructive"),
		                                      LOCTEXT("CmdScope",   "Tighten what this conversation is allowed to change") },
		{ TEXT("tokens"),  TEXT(""),          LOCTEXT("CmdTokens",  "Show the token and cost breakdown for this conversation") },
		{ TEXT("retry"),   TEXT(""),          LOCTEXT("CmdRetry",   "Re-run the last reply as an alternative, keeping the first") },
		{ TEXT("settings"),TEXT(""),          LOCTEXT("CmdSettings","Open the chat settings") },
		{ TEXT("help"),    TEXT(""),          LOCTEXT("CmdHelp",    "List these commands") },
	};
	return Commands;
}

const FChatSlashCommand* FMCPChatSlashCommands::Find(const FString& NameOrAlias)
{
	const FString Needle = NameOrAlias.ToLower();
	for (const FChatSlashCommand& C : GetAll())
	{
		if (C.Name == Needle) { return &C; }
		for (const FString& Alias : C.Aliases)
		{
			if (Alias == Needle) { return &C; }
		}
	}
	return nullptr;
}

TArray<FChatSlashCommand> FMCPChatSlashCommands::Filter(const FString& Query, int32 Limit)
{
	const FString Q = Query.ToLower();
	TArray<FChatSlashCommand> Out;
	for (const FChatSlashCommand& C : GetAll())
	{
		if (Q.IsEmpty() || C.Name.StartsWith(Q))
		{
			Out.Add(C);
			if (Out.Num() >= Limit) { break; }
		}
	}
	return Out;
}

bool FMCPChatSlashCommands::LooksLikeCommand(const FString& Line)
{
	if (!Line.StartsWith(TEXT("/"))) { return false; }

	FString Word = Line.Mid(1);
	int32 Space = INDEX_NONE;
	if (Word.FindChar(TEXT(' '), Space)) { Word.LeftInline(Space); }

	// Requiring a KNOWN command is what keeps "/Game/Maps/Arena is broken" a message
	// rather than an error about an unknown command.
	return Find(Word) != nullptr;
}

FChatCommandResult FMCPChatSlashCommands::Execute(const FString& Line, const TSharedPtr<FMCPChatController>& Controller)
{
	FChatCommandResult R;
	if (!LooksLikeCommand(Line) || !Controller.IsValid()) { return R; }

	FString Rest = Line.Mid(1).TrimStartAndEnd();
	FString Name = Rest;
	FString Args;
	int32 Space = INDEX_NONE;
	if (Rest.FindChar(TEXT(' '), Space))
	{
		Name = Rest.Left(Space);
		Args = Rest.Mid(Space + 1).TrimStartAndEnd();
	}
	Name.ToLowerInline();

	R.bHandled = true;
	const FChatSessionPtr Session = Controller->GetActiveSession();

	// ---- new / clear ----
	if (Name == TEXT("new"))
	{
		Controller->NewSession();
		R.Message = LOCTEXT("NewDone", "Started a new conversation.");
		return R;
	}

	if (Name == TEXT("clear"))
	{
		Controller->ClearActiveTranscript();
		R.Message = LOCTEXT("ClearDone", "Cleared this conversation.");
		return R;
	}

	// ---- model / agent ----
	if (Name == TEXT("model") || Name == TEXT("agent") || Name == TEXT("backend"))
	{
		if (Args.IsEmpty())
		{
			R.UiAction = FChatCommandResult::EUiAction::OpenModelPicker;
			return R;
		}

		// Match on id first, then on display name, then loosely. People type "opus",
		// not "claude-opus-5".
		const TArray<FChatModelInfo>& Models = FMCPChatModelCatalog::Get().GetModels();
		const FChatModelInfo* Best = nullptr;
		for (const FChatModelInfo& M : Models)
		{
			if (M.ModelId.Equals(Args, ESearchCase::IgnoreCase)) { Best = &M; break; }
		}
		if (!Best)
		{
			for (const FChatModelInfo& M : Models)
			{
				if (M.DisplayName.Equals(Args, ESearchCase::IgnoreCase)) { Best = &M; break; }
			}
		}
		if (!Best)
		{
			for (const FChatModelInfo& M : Models)
			{
				if (M.ModelId.Contains(Args) || M.DisplayName.Contains(Args)) { Best = &M; break; }
			}
		}

		if (!Best)
		{
			// Maybe it names a backend rather than a model — that is what /agent is for.
			for (const FChatBackendPtr& B : Controller->GetBackends())
			{
				if (!B.IsValid()) { continue; }
				if (B->GetId().Equals(Args, ESearchCase::IgnoreCase)
					|| B->GetDisplayName().ToString().Contains(Args))
				{
					const TArray<FChatModelInfo> BackendModels = B->GetModels();
					Controller->SwitchModel(B->GetId(),
						BackendModels.Num() > 0 ? BackendModels[0].ModelId : FString());
					R.Message = FText::Format(LOCTEXT("SwitchedBackend", "Switched to {0}."), B->GetDisplayName());
					return R;
				}
			}

			R.bIsError = true;
			R.Message = FText::Format(
				LOCTEXT("NoSuchModel", "No model or agent matching '{0}'. Try /model with no argument."),
				FText::FromString(Args));
			return R;
		}

		Controller->SwitchModel(Best->ProviderId, Best->ModelId);
		R.Message = FText::Format(LOCTEXT("SwitchedModel", "Switched to {0}."), FText::FromString(Best->DisplayName));
		return R;
	}

	// ---- compact ----
	if (Name == TEXT("compact"))
	{
		R.Message = Controller->CompactActiveSession();
		return R;
	}

	// ---- export ----
	if (Name == TEXT("export"))
	{
		if (!Session.IsValid())
		{
			R.bIsError = true;
			R.Message = LOCTEXT("ExportNoSession", "Nothing to export.");
			return R;
		}

		const FString Format = Args.IsEmpty() ? TEXT("md") : Args.ToLower();
		if (Format != TEXT("md") && Format != TEXT("json"))
		{
			R.bIsError = true;
			R.Message = LOCTEXT("ExportBadFormat", "Use /export md or /export json.");
			return R;
		}

		FString Written;
		if (Format == TEXT("md"))
		{
			Written = FMCPChatStore::Get().ExportSessionToMarkdown(Session->Id);
		}
		else
		{
			// JSON needs no separate exporter — the session file already IS the JSON,
			// deliberately (docs/02 §7). Point at it rather than writing a second copy
			// that can drift from the first.
			FMCPChatStore::Get().SaveNow(Session->Id);
			Written = FMCPChatStore::GetSessionFilePath(Session->Id);
		}

		if (Written.IsEmpty())
		{
			R.bIsError = true;
			R.Message = LOCTEXT("ExportFailed", "Export failed. Check the Output Log.");
			return R;
		}

		R.UiAction = FChatCommandResult::EUiAction::RevealExport;
		R.Payload  = Written;
		R.Message  = FText::Format(LOCTEXT("ExportDone", "Exported to {0}"),
			FText::FromString(FPaths::GetCleanFilename(Written)));
		return R;
	}

	// ---- title / pin ----
	if (Name == TEXT("title"))
	{
		if (Args.IsEmpty())
		{
			R.bIsError = true;
			R.Message = LOCTEXT("TitleNeedsText", "Usage: /title A better name");
			return R;
		}
		Controller->SetSessionTitle(Args);
		R.Message = FText::Format(LOCTEXT("TitleDone", "Renamed to \"{0}\"."), FText::FromString(Args));
		return R;
	}

	if (Name == TEXT("pin") || Name == TEXT("unpin"))
	{
		const bool bPin = (Name == TEXT("pin"));
		Controller->SetSessionPinned(bPin);
		R.Message = bPin
			? LOCTEXT("PinDone", "Pinned to the top of the history rail.")
			: LOCTEXT("UnpinDone", "Unpinned.");
		return R;
	}

	// ---- scope ----
	if (Name == TEXT("scope"))
	{
		if (!Session.IsValid())
		{
			R.bIsError = true;
			R.Message = LOCTEXT("ScopeNoSession", "No conversation to scope.");
			return R;
		}

		const FString S = Args.ToLower();
		EMCPChatApprovalMode Ceiling;
		if (S == TEXT("read") || S == TEXT("readonly"))        { Ceiling = EMCPChatApprovalMode::ReadOnly; }
		else if (S == TEXT("write") || S == TEXT("scene"))     { Ceiling = EMCPChatApprovalMode::AskWrites; }
		else if (S == TEXT("destructive"))                     { Ceiling = EMCPChatApprovalMode::AskDestructive; }
		else if (S == TEXT("off") || S == TEXT("clear"))
		{
			FMCPChatToolBridge::Get().ClearSessionCeiling(Session->Id);
			R.Message = LOCTEXT("ScopeCleared", "Removed this conversation's scope limit; the global setting applies.");
			return R;
		}
		else
		{
			R.bIsError = true;
			R.Message = LOCTEXT("ScopeUsage", "Usage: /scope read | write | destructive | off");
			return R;
		}

		FMCPChatToolBridge& Bridge = FMCPChatToolBridge::Get();
		Bridge.SetSessionCeiling(Session->Id, Ceiling);

		// Say what actually applies, not what was asked for. A ceiling can only
		// tighten, so `/scope destructive` inside a read-only project changes nothing
		// and the user needs to hear that rather than assume it worked.
		const EMCPChatApprovalMode Effective = Bridge.GetEffectiveMode(Session->Id);
		if (Effective != Ceiling)
		{
			R.Message = LOCTEXT("ScopeClampedByGlobal",
				"Your global permission setting is already stricter, so nothing changed. "
				"A /scope can tighten a conversation, never loosen one.");
		}
		else
		{
			R.Message = FText::Format(LOCTEXT("ScopeSet", "This conversation is now limited to: {0}."),
				FText::FromString(S));
		}
		return R;
	}

	// ---- retry ----
	if (Name == TEXT("retry"))
	{
		Controller->RetryLastTurn();
		R.Message = LOCTEXT("RetryDone", "Re-running the last reply as an alternative.");
		return R;
	}

	// ---- UI-only ----
	if (Name == TEXT("tools"))
	{
		R.UiAction = FChatCommandResult::EUiAction::OpenToolBrowser;
		return R;
	}
	if (Name == TEXT("settings"))
	{
		R.UiAction = FChatCommandResult::EUiAction::OpenSettings;
		return R;
	}
	if (Name == TEXT("tokens"))
	{
		R.UiAction = FChatCommandResult::EUiAction::ShowTokenBreakdown;
		return R;
	}

	// ---- help ----
	if (Name == TEXT("help"))
	{
		TStringBuilder<2048> SB;
		SB.Append(TEXT("Commands (these run locally and are never sent to a model):\n"));
		for (const FChatSlashCommand& C : GetAll())
		{
			SB.Appendf(TEXT("  /%s %s — %s\n"), *C.Name, *C.ArgHint, *C.Description.ToString());
		}
		SB.Append(TEXT("\nType @ for editor context, # for tools and workflows."));
		R.Message = FText::FromString(SB.ToString());
		return R;
	}

	R.bHandled = false;
	return R;
}

#undef LOCTEXT_NAMESPACE
