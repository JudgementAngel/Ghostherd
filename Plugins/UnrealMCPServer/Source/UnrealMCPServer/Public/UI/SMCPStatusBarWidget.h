// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * v4 status-bar widget for the MCP Server.
 *
 * - Live status chip: colored dot (green running / red stopped) that PULSES
 *   when tool calls arrive, plus port + tool count.
 * - Clicking opens a menu (instead of v3's instant server toggle — a misclick
 *   used to kill the server): Start/Stop/Restart, copy-paste client configs
 *   (Claude Code / Cursor / raw URL), live stats, Export Tool Reference,
 *   Project Settings, online docs.
 * - Rich tooltip with uptime / request counters for at-a-glance health.
 */
class SMCPStatusBarWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SMCPStatusBarWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Chip
	FSlateColor GetStatusDotColor() const;
	FText GetStatusText() const;
	FText GetStatusTooltip() const;

	// Menu
	TSharedRef<SWidget> MakeMenuContent();
	FText GetMenuStatusLine() const;
	FText GetMenuStatsLine() const;

	// Actions
	void ToggleServer();
	void RestartServer();
	void CopyMcpUrl();
	void CopyClaudeCodeConfig();
	void CopyCursorConfig();
	void ExportToolDocs();
	void OpenPluginSettings();
	void OpenOnlineDocs();

	FString GetMcpUrl() const;
	void NotifySuccess(const FText& Message) const;
};
