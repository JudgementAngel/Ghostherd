// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatSettings.h"

UMCPChatSettings::UMCPChatSettings()
	: ThemeName(TEXT("Editor"))
	, bHotReloadThemes(true)
	, DefaultDock(EMCPChatDefaultDock::RightSidebar)
	, HistoryRailWidth(240)
	, WideBreakpoint(900)
	, NarrowBreakpoint(480)
	, TranscriptMaxContentWidth(820)
	, ApprovalMode(EMCPChatApprovalMode::AskDestructive)
	, bSingleUndoPerTurn(true)
	, MaxToolIterationsPerTurn(25)
	, bShowFirstRunNotification(true)
	, bShowToolbarButton(false)
	, bEnterInsertsNewline(false)
{
}

const UMCPChatSettings* UMCPChatSettings::Get()
{
	return GetDefault<UMCPChatSettings>();
}
