// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// v4.6.2: runtime verification of UMG work - create a Widget Blueprint instance in the
// running PIE session and put it on screen, so pie_screenshot can show it without first
// authoring a Blueprint graph that adds it.

#include "Tools/Widget/WidgetCommon.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "WidgetBlueprint.h"
#include "UObject/UObjectIterator.h"

namespace MCPWidgetTools::PIE
{

using namespace MCPWidgetTools::Common;

namespace
{
	struct FLiveWidget
	{
		TWeakObjectPtr<UUserWidget> Widget;
		FString ClassPath;
	};
	static TMap<FString, FLiveWidget> GLiveWidgets;
	static int32 GLiveWidgetCounter = 0;

	UWorld* GetPIEWorld()
	{
		if (GEditor && GEditor->IsPlaySessionInProgress() && GEditor->PlayWorld) return GEditor->PlayWorld;
		return nullptr;
	}

	void Prune()
	{
		for (auto It = GLiveWidgets.CreateIterator(); It; ++It)
		{
			if (!It->Value.Widget.IsValid()) It.RemoveCurrent();
		}
	}
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// pie_add_widget_to_viewport
	// ================================================================
	MCP_TOOL(Registry, "pie_add_widget_to_viewport")
		.Description(TEXT("Create an instance of a Widget Blueprint in the running Play-In-Editor session and add it to the viewport (owned by the first player controller). Returns a handle for pie_remove_widget. Use pie_screenshot afterwards to see it. Requires an active PIE session (pie_start)."))
		.StringArg(TEXT("widget_class_path"), TEXT("Widget Blueprint asset path (e.g. '/Game/UI/WBP_HUD')"), true)
		.IntArg(TEXT("z_order"), TEXT("Viewport Z order (default 0)"))
		.BoolArg(TEXT("show_mouse_cursor"), TEXT("Show the mouse cursor and switch to Game+UI input mode (default false)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ClassPath;
			if (!Args->TryGetStringField(TEXT("widget_class_path"), ClassPath)) return FMCPToolResult::Error(TEXT("widget_class_path is required"));
			int32 ZOrder = 0; if (Args->HasField(TEXT("z_order"))) ZOrder = (int32)Args->GetNumberField(TEXT("z_order"));
			bool bShowCursor = false; Args->TryGetBoolField(TEXT("show_mouse_cursor"), bShowCursor);

			UWorld* World = GetPIEWorld();
			if (!World) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("No PIE session is running. Call pie_start first."));

			FString Err;
			UClass* WidgetClass = ResolveUserWidgetClass(ClassPath, Err);
			if (!WidgetClass) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, Err);

			APlayerController* PC = World->GetFirstPlayerController();
			UUserWidget* Widget = PC
				? CreateWidget<UUserWidget>(PC, WidgetClass)
				: CreateWidget<UUserWidget>(World, WidgetClass);
			if (!Widget) return FMCPToolResult::Error(FString::Printf(TEXT("CreateWidget failed for %s (abstract class? construction script error?)"), *WidgetClass->GetName()));

			Widget->AddToViewport(ZOrder);

			if (bShowCursor && PC)
			{
				UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(PC, Widget);
				PC->SetShowMouseCursor(true);
			}

			Prune();
			const FString Handle = FString::Printf(TEXT("%s_%d"), *WidgetClass->GetName(), ++GLiveWidgetCounter);
			GLiveWidgets.Add(Handle, { Widget, ClassPath });

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetStringField(TEXT("handle"), Handle);
			Info->SetStringField(TEXT("class"), WidgetClass->GetName());
			Info->SetBoolField(TEXT("in_viewport"), Widget->IsInViewport());
			Info->SetNumberField(TEXT("z_order"), ZOrder);
			return FMCPToolResult::SuccessStructured(FString::Printf(
				TEXT("Added %s to the PIE viewport (handle: %s). Take pie_screenshot to inspect it; pie_remove_widget handle=%s to remove."),
				*WidgetClass->GetName(), *Handle, *Handle), Info);
		});

	// ================================================================
	// pie_remove_widget
	// ================================================================
	MCP_TOOL(Registry, "pie_remove_widget")
		.Description(TEXT("Remove a widget added with pie_add_widget_to_viewport (by handle, or 'all')."))
		.StringArg(TEXT("handle"), TEXT("Handle returned by pie_add_widget_to_viewport, or 'all'"), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Handle;
			if (!Args->TryGetStringField(TEXT("handle"), Handle)) return FMCPToolResult::Error(TEXT("handle is required"));

			TArray<FString> Removed;
			if (Handle.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				for (auto& Pair : GLiveWidgets)
				{
					if (UUserWidget* W = Pair.Value.Widget.Get()) { W->RemoveFromParent(); Removed.Add(Pair.Key); }
				}
				GLiveWidgets.Empty();
			}
			else
			{
				FLiveWidget* Live = GLiveWidgets.Find(Handle);
				if (!Live) return FMCPToolResult::ErrorStructured(EMCPError::NotFound, FString::Printf(TEXT("Unknown widget handle: %s"), *Handle));
				if (UUserWidget* W = Live->Widget.Get()) W->RemoveFromParent();
				GLiveWidgets.Remove(Handle);
				Removed.Add(Handle);
			}
			Prune();
			return FMCPToolResult::Success(FString::Printf(TEXT("Removed %d widget(s): %s"), Removed.Num(), *FString::Join(Removed, TEXT(", "))));
		});

	// ================================================================
	// pie_list_widgets
	// ================================================================
	MCP_TOOL(Registry, "pie_list_widgets")
		.Description(TEXT("List widgets added through pie_add_widget_to_viewport that are still alive, and all UserWidgets currently in the PIE viewport."))
		.ReadOnly()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			Prune();
			TArray<TSharedPtr<FJsonValue>> Handles;
			for (const auto& Pair : GLiveWidgets)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetStringField(TEXT("handle"), Pair.Key);
				O->SetStringField(TEXT("class_path"), Pair.Value.ClassPath);
				O->SetBoolField(TEXT("in_viewport"), Pair.Value.Widget.IsValid() && Pair.Value.Widget->IsInViewport());
				Handles.Add(MakeShared<FJsonValueObject>(O));
			}

			TArray<TSharedPtr<FJsonValue>> Live;
			if (UWorld* World = GetPIEWorld())
			{
				for (TObjectIterator<UUserWidget> It; It; ++It)
				{
					UUserWidget* W = *It;
					if (!W || W->GetWorld() != World || !W->IsInViewport()) continue;
					TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
					O->SetStringField(TEXT("name"), W->GetName());
					O->SetStringField(TEXT("class"), W->GetClass()->GetName());
					O->SetStringField(TEXT("visibility"), UEnum::GetValueAsString(W->GetVisibility()));
					Live.Add(MakeShared<FJsonValueObject>(O));
				}
			}

			TSharedPtr<FJsonObject> Info = MakeShared<FJsonObject>();
			Info->SetArrayField(TEXT("mcp_widgets"), Handles);
			Info->SetArrayField(TEXT("viewport_widgets"), Live);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("%d MCP widget(s), %d widget(s) in viewport"), Handles.Num(), Live.Num()), Info);
		});
}

} // namespace MCPWidgetTools::PIE
