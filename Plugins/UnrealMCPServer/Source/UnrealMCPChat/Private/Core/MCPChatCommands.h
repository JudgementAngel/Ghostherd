// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FMCPChatController;

/**
 * Phase 6 — `/` commands.
 *
 * These NEVER reach a model. That is the whole point: renaming a conversation or
 * exporting it should not cost a round-trip, a token, or a wait — and should not
 * depend on a model correctly deciding to call a tool.
 *
 * A line is only treated as a command when it starts with `/` and the first word
 * matches a known command. `/Game/Foo is broken` is a message about an asset path,
 * not a malformed command, and must send as typed.
 */

struct FChatSlashCommand
{
	FString Name;
	/** Argument hint shown in the palette: "<name>", "md|json". Empty = no args. */
	FString ArgHint;
	FText   Description;
	/** Aliases that resolve to the same command. */
	TArray<FString> Aliases;
};

struct FChatCommandResult
{
	/** False when the line was not a command at all — the caller sends it as text. */
	bool bHandled = false;
	bool bIsError = false;

	/** Shown to the user as a local notice in the transcript. Never sent anywhere. */
	FText Message;

	/** Something only the panel can do. */
	enum class EUiAction : uint8
	{
		None,
		OpenToolBrowser,
		OpenSettings,
		ShowTokenBreakdown,
		OpenModelPicker,
		RevealExport,
	};
	EUiAction UiAction = EUiAction::None;

	/** RevealExport: the file to show. */
	FString Payload;
};

class FMCPChatSlashCommands
{
public:
	static const TArray<FChatSlashCommand>& GetAll();

	/** Prefix-filtered list for the `/` palette. */
	static TArray<FChatSlashCommand> Filter(const FString& Query, int32 Limit = 12);

	/** True when Line begins with a `/` followed by a known command word. */
	static bool LooksLikeCommand(const FString& Line);

	static FChatCommandResult Execute(const FString& Line, const TSharedPtr<FMCPChatController>& Controller);

private:
	static const FChatSlashCommand* Find(const FString& NameOrAlias);
};
