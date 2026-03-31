#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Status bar widget for the MCP Server.
 * Displays server status (running/stopped), port, and tool count
 * in the editor's bottom status bar. Click to toggle server on/off.
 */
class SMCPStatusBarWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SMCPStatusBarWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	const FSlateBrush* GetStatusIcon() const;
	FText GetStatusText() const;
	FText GetStatusTooltip() const;
	FReply OnStatusBarButtonClicked();
};
