#include "UI/SMCPStatusBarWidget.h"
#include "MCPHttpServer.h"
#include "MCPToolRegistry.h"
#include "MCPSettings.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "MCPStatusBar"

void SMCPStatusBarWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SButton)
		.ContentPadding(FMargin(6.0f, 0.0f))
		.ToolTipText_Raw(this, &SMCPStatusBarWidget::GetStatusTooltip)
		.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
		.OnClicked_Raw(this, &SMCPStatusBarWidget::OnStatusBarButtonClicked)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.DesiredSizeOverride(FVector2D(10.0f, 10.0f))
				.Image_Raw(this, &SMCPStatusBarWidget::GetStatusIcon)
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

const FSlateBrush* SMCPStatusBarWidget::GetStatusIcon() const
{
	if (FMCPHttpServer::Get().IsRunning())
	{
		return FAppStyle::Get().GetBrush("Icons.SuccessWithColor");
	}
	return FAppStyle::Get().GetBrush("Icons.ErrorWithColor");
}

FText SMCPStatusBarWidget::GetStatusText() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (Server.IsRunning())
	{
		int32 ToolCount = FMCPToolRegistry::Get().GetToolCount();
		return FText::Format(
			LOCTEXT("MCPRunning", "MCP: Port {0} | {1} Tools"),
			FText::AsNumber(Server.GetPort()),
			FText::AsNumber(ToolCount));
	}
	return LOCTEXT("MCPStopped", "MCP: Stopped");
}

FText SMCPStatusBarWidget::GetStatusTooltip() const
{
	const FMCPHttpServer& Server = FMCPHttpServer::Get();
	const UMCPSettings* Settings = UMCPSettings::Get();

	if (Server.IsRunning())
	{
		FString PresetName;
		switch (Settings->ToolPreset)
		{
		case EMCPToolPreset::Full:           PresetName = TEXT("Full"); break;
		case EMCPToolPreset::SceneBuilding:  PresetName = TEXT("Scene Building"); break;
		case EMCPToolPreset::Gameplay:       PresetName = TEXT("Gameplay"); break;
		case EMCPToolPreset::Minimal:        PresetName = TEXT("Minimal"); break;
		case EMCPToolPreset::Custom:         PresetName = TEXT("Custom"); break;
		}

		return FText::Format(
			LOCTEXT("MCPRunningTooltip", "Unreal MCP Server v2.0.0\nStatus: Running\nPort: {0}\nURL: http://localhost:{0}/mcp\nPreset: {1}\nTools: {2}\n\nClick to stop server"),
			FText::AsNumber(Server.GetPort()),
			FText::FromString(PresetName),
			FText::AsNumber(FMCPToolRegistry::Get().GetToolCount()));
	}

	return FText::Format(
		LOCTEXT("MCPStoppedTooltip", "Unreal MCP Server v2.0.0\nStatus: Stopped\nConfigured Port: {0}\n\nClick to start server"),
		FText::AsNumber(Settings->ServerPort));
}

FReply SMCPStatusBarWidget::OnStatusBarButtonClicked()
{
	FMCPHttpServer& Server = FMCPHttpServer::Get();
	if (Server.IsRunning())
	{
		Server.Stop();
	}
	else
	{
		const UMCPSettings* Settings = UMCPSettings::Get();
		Server.Start(Settings->ServerPort);
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
