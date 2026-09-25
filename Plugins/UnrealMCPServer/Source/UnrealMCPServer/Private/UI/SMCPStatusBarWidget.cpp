// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SMCPStatusBarWidget.h"
#include "MCPHttpServer.h"
#include "MCPProtocol.h"
#include "MCPToolRegistry.h"
#include "MCPRequestContext.h"
#include "MCPSettings.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MCPStatusBar"

namespace
{
	/** Seconds the dot stays "hot" after a tool call before fading back. */
	constexpr double ActivityPulseSeconds = 1.2;

	FString FormatUptime(double Seconds)
	{
		const int32 Total = (int32)Seconds;
		if (Total >= 3600) { return FString::Printf(TEXT("%dh %02dm"), Total / 3600, (Total % 3600) / 60); }
		if (Total >= 60)   { return FString::Printf(TEXT("%dm %02ds"), Total / 60, Total % 60); }
		return FString::Printf(TEXT("%ds"), Total);
	}
}

void SMCPStatusBarWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SComboButton)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("SimpleComboButton"))
		.ToolTipText_Raw(this, &SMCPStatusBarWidget::GetStatusTooltip)
		.OnGetMenuContent_Raw(this, &SMCPStatusBarWidget::MakeMenuContent)
		.HasDownArrow(false)
		.ContentPadding(FMargin(6.0f, 0.0f))
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(9.0f, 9.0f))
				.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
				.ColorAndOpacity_Raw(this, &SMCPStatusBarWidget::GetStatusDotColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
			[
				SNew(STextBlock)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.Text_Raw(this, &SMCPStatusBarWidget::GetStatusText)
			]
		]
	];
}

// ============================================================================
// Chip
// ============================================================================

FSlateColor SMCPStatusBarWidget::GetStatusDotColor() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (!Server.IsRunning())
	{
		return FStyleColors::Error; // red
	}

	// Pulse toward white-hot cyan while tool calls are landing, fading back to
	// steady green over ActivityPulseSeconds. Gives "the agent is working right
	// now" feedback without a single extra pixel of layout.
	const FMCPHttpServer::FServerStats Stats = Server.GetStats();
	const FLinearColor Idle = FStyleColors::Success.GetSpecifiedColor();
	if (Stats.SecondsSinceLastToolCall >= 0.0 && Stats.SecondsSinceLastToolCall < ActivityPulseSeconds)
	{
		const float Alpha = 1.0f - (float)(Stats.SecondsSinceLastToolCall / ActivityPulseSeconds);
		const FLinearColor Hot(0.3f, 1.0f, 0.9f);
		return FLinearColor::LerpUsingHSV(Idle, Hot, Alpha);
	}
	return Idle;
}

FText SMCPStatusBarWidget::GetStatusText() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (Server.IsRunning())
	{
		return FText::Format(
			LOCTEXT("MCPRunning", "MCP :{0} · {1} tools"),
			FText::AsNumber(Server.GetPort(), &FNumberFormattingOptions::DefaultNoGrouping()),
			FText::AsNumber(FMCPToolRegistry::Get().GetToolCount()));
	}
	return LOCTEXT("MCPStopped", "MCP · off");
}

FText SMCPStatusBarWidget::GetStatusTooltip() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	const UMCPSettings* Settings = UMCPSettings::Get();

	if (Server.IsRunning())
	{
		const FMCPHttpServer::FServerStats Stats = Server.GetStats();
		const TCHAR* Exposure = Settings->ToolExposureMode == EMCPToolExposureMode::Catalog
			? TEXT("Catalog (progressive disclosure)") : TEXT("Full");

		FString LastCall = TEXT("—");
		if (!Stats.LastToolName.IsEmpty())
		{
			LastCall = FString::Printf(TEXT("%s (%ds ago)"),
				*Stats.LastToolName, (int32)Stats.SecondsSinceLastToolCall);
		}

		return FText::Format(
			LOCTEXT("MCPRunningTooltip",
				"Unreal MCP Server v{0} — Running\n"
				"{1}\n"
				"\n"
				"Uptime: {2}    Sessions: {3}\n"
				"Requests: {4}    Tool calls: {5}\n"
				"Last call: {6}\n"
				"Exposure: {7}\n"
				"\n"
				"Click for actions"),
			FText::FromString(MCPProtocol::ServerVersion),
			FText::FromString(GetMcpUrl()),
			FText::FromString(FormatUptime(Stats.UptimeSeconds)),
			FText::AsNumber(Stats.ActiveSessions),
			FText::AsNumber((int64)Stats.TotalRequests),
			FText::AsNumber((int64)Stats.TotalToolCalls),
			FText::FromString(LastCall),
			FText::FromString(Exposure));
	}

	return FText::Format(
		LOCTEXT("MCPStoppedTooltip",
			"Unreal MCP Server v{0} — Stopped\n"
			"Configured port: {1}\n"
			"\n"
			"Click for actions"),
		FText::FromString(MCPProtocol::ServerVersion),
		FText::AsNumber(Settings->ServerPort, &FNumberFormattingOptions::DefaultNoGrouping()));
}

// ============================================================================
// Menu
// ============================================================================

TSharedRef<SWidget> SMCPStatusBarWidget::MakeMenuContent()
{
	FMenuBuilder Menu(/*bShouldCloseAfterSelection*/ true, /*CommandList*/ nullptr);
	const bool bRunning = FMCPHttpServer::Get().IsRunning();

	Menu.BeginSection("MCPStatus", LOCTEXT("StatusSection", "Server"));
	{
		// Live status + stats lines (read-only).
		Menu.AddWidget(
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(14.0f, 2.0f, 14.0f, 0.0f)
			[
				SNew(STextBlock)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Text_Raw(this, &SMCPStatusBarWidget::GetMenuStatusLine)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(14.0f, 2.0f, 14.0f, 4.0f)
			[
				SNew(STextBlock)
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("SmallText"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text_Raw(this, &SMCPStatusBarWidget::GetMenuStatsLine)
			],
			FText::GetEmpty(), /*bNoIndent*/ true);

		Menu.AddMenuEntry(
			bRunning ? LOCTEXT("StopServer", "Stop Server") : LOCTEXT("StartServer", "Start Server"),
			bRunning ? LOCTEXT("StopServerTip", "Stop the MCP HTTP listener. Connected agents will lose access.")
			         : LOCTEXT("StartServerTip", "Start the MCP HTTP listener on the configured port."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), bRunning ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play"),
			FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::ToggleServer)));

		if (bRunning)
		{
			Menu.AddMenuEntry(
				LOCTEXT("RestartServer", "Restart Server"),
				LOCTEXT("RestartServerTip", "Stop and immediately restart the listener (applies port/setting changes)."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Refresh"),
				FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::RestartServer)));
		}
	}
	Menu.EndSection();

	Menu.BeginSection("MCPConnect", LOCTEXT("ConnectSection", "Connect an Agent"));
	{
		Menu.AddMenuEntry(
			LOCTEXT("CopyClaude", "Copy Claude Code Config"),
			LOCTEXT("CopyClaudeTip", "Copy a ready-to-paste .mcp.json entry for Claude Code."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::CopyClaudeCodeConfig)));

		Menu.AddMenuEntry(
			LOCTEXT("CopyCursor", "Copy Cursor Config"),
			LOCTEXT("CopyCursorTip", "Copy a ready-to-paste .cursor/mcp.json entry for Cursor."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::CopyCursorConfig)));

		Menu.AddMenuEntry(
			LOCTEXT("CopyUrl", "Copy Server URL"),
			LOCTEXT("CopyUrlTip", "Copy the raw MCP endpoint URL to the clipboard."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
			FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::CopyMcpUrl)));
	}
	Menu.EndSection();

	Menu.BeginSection("MCPTools", LOCTEXT("ToolsSection", "Tools"));
	{
		Menu.AddMenuEntry(
			LOCTEXT("ExportDocs", "Export Tool Reference"),
			LOCTEXT("ExportDocsTip", "Generate the complete tool-reference markdown from the live registry into Saved/MCPDocs/."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Documentation"),
			FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::ExportToolDocs)));

		Menu.AddMenuEntry(
			LOCTEXT("OpenSettings", "Plugin Settings..."),
			LOCTEXT("OpenSettingsTip", "Open Project Settings > Plugins > Unreal MCP Server."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Settings"),
			FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::OpenPluginSettings)));

		Menu.AddMenuEntry(
			LOCTEXT("OpenDocs", "Online Documentation"),
			LOCTEXT("OpenDocsTip", "Open the Unreal MCP Server documentation in your browser."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Help"),
			FUIAction(FExecuteAction::CreateSP(this, &SMCPStatusBarWidget::OpenOnlineDocs)));
	}
	Menu.EndSection();

	return Menu.MakeWidget();
}

FText SMCPStatusBarWidget::GetMenuStatusLine() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (Server.IsRunning())
	{
		return FText::Format(LOCTEXT("MenuStatusRunning", "v{0} — Running on {1}"),
			FText::FromString(MCPProtocol::ServerVersion),
			FText::FromString(GetMcpUrl()));
	}
	return FText::Format(LOCTEXT("MenuStatusStopped", "v{0} — Stopped"),
		FText::FromString(MCPProtocol::ServerVersion));
}

FText SMCPStatusBarWidget::GetMenuStatsLine() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (!Server.IsRunning())
	{
		return LOCTEXT("MenuStatsStopped", "Start the server to accept agent connections.");
	}
	const FMCPHttpServer::FServerStats Stats = Server.GetStats();
	return FText::Format(
		LOCTEXT("MenuStats", "{0} tools · {1} sessions · {2} calls · up {3}"),
		FText::AsNumber(FMCPToolRegistry::Get().GetToolCount()),
		FText::AsNumber(Stats.ActiveSessions),
		FText::AsNumber((int64)Stats.TotalToolCalls),
		FText::FromString(FormatUptime(Stats.UptimeSeconds)));
}

// ============================================================================
// Actions
// ============================================================================

void SMCPStatusBarWidget::ToggleServer()
{
	FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (Server.IsRunning())
	{
		Server.Stop();
		NotifySuccess(LOCTEXT("ServerStopped", "MCP server stopped"));
	}
	else
	{
		const UMCPSettings* Settings = UMCPSettings::Get();
		if (Server.Start(Settings->ServerPort))
		{
			NotifySuccess(FText::Format(LOCTEXT("ServerStarted", "MCP server running on port {0}"),
				FText::AsNumber(Settings->ServerPort, &FNumberFormattingOptions::DefaultNoGrouping())));
		}
	}
}

void SMCPStatusBarWidget::RestartServer()
{
	FMCPHttpServer& Server = FMCPHttpServer::Get();
	Server.Stop();
	const UMCPSettings* Settings = UMCPSettings::Get();
	if (Server.Start(Settings->ServerPort))
	{
		NotifySuccess(LOCTEXT("ServerRestarted", "MCP server restarted"));
	}
}

FString SMCPStatusBarWidget::GetMcpUrl() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	const int32 Port = Server.IsRunning() ? Server.GetPort() : UMCPSettings::Get()->ServerPort;
	return FString::Printf(TEXT("http://localhost:%d/mcp"), Port);
}

void SMCPStatusBarWidget::CopyMcpUrl()
{
	FPlatformApplicationMisc::ClipboardCopy(*GetMcpUrl());
	NotifySuccess(LOCTEXT("UrlCopied", "MCP URL copied to clipboard"));
}

void SMCPStatusBarWidget::CopyClaudeCodeConfig()
{
	const FString Config = FString::Printf(TEXT(
		"{\n"
		"  \"mcpServers\": {\n"
		"    \"unreal\": {\n"
		"      \"type\": \"http\",\n"
		"      \"url\": \"%s\"\n"
		"    }\n"
		"  }\n"
		"}"), *GetMcpUrl());
	FPlatformApplicationMisc::ClipboardCopy(*Config);
	NotifySuccess(LOCTEXT("ClaudeCopied", "Claude Code config copied — paste into .mcp.json"));
}

void SMCPStatusBarWidget::CopyCursorConfig()
{
	const FString Config = FString::Printf(TEXT(
		"{\n"
		"  \"mcpServers\": {\n"
		"    \"unreal\": {\n"
		"      \"url\": \"%s\"\n"
		"    }\n"
		"  }\n"
		"}"), *GetMcpUrl());
	FPlatformApplicationMisc::ClipboardCopy(*Config);
	NotifySuccess(LOCTEXT("CursorCopied", "Cursor config copied — paste into .cursor/mcp.json"));
}

void SMCPStatusBarWidget::ExportToolDocs()
{
	FMCPRequestContext Context;
	Context.Scope = EMCPScope::Scene;
	const FMCPToolResult Result = FMCPToolRegistry::Get().ExecuteTool(
		TEXT("export_tool_docs"), MakeShared<FJsonObject>(), Context);

	if (!Result.bIsError && Result.Content.Num() > 0)
	{
		NotifySuccess(FText::FromString(Result.Content[0].Text));
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("ExportFailed", "Tool reference export failed — see Output Log"));
		Info.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::CS_Fail);
	}
}

void SMCPStatusBarWidget::OpenPluginSettings()
{
	if (ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		Settings->ShowViewer("Project", "Plugins", "Unreal MCP Server");
	}
}

void SMCPStatusBarWidget::OpenOnlineDocs()
{
	FPlatformProcess::LaunchURL(TEXT("https://www.strayspark.studio/docs/unreal-mcp-server"), nullptr, nullptr);
}

void SMCPStatusBarWidget::NotifySuccess(const FText& Message) const
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 3.5f;
	Info.bFireAndForget = true;
	if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(SNotificationItem::CS_Success);
	}
}

#undef LOCTEXT_NAMESPACE
