// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/PIE/PIECommon.h"

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Engine/EngineBaseTypes.h"

namespace MCPPIETools::Input
{

using namespace MCPPIETools::Common;

static EInputEvent ParseInputEvent(const FString& EventStr)
{
	if (EventStr == TEXT("Released")) return IE_Released;
	if (EventStr == TEXT("Repeat"))   return IE_Repeat;
	return IE_Pressed;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// pie_send_input
	// ================================================================
	MCP_TOOL(Registry, "pie_send_input")
		.Description(TEXT("Synthesize a keyboard/mouse/gamepad event into the active PIE session. Key is an FKey name (e.g. 'SpaceBar', 'LeftMouseButton', 'Gamepad_FaceButton_Bottom')."))
		.Destructive()
		.StringArg(TEXT("key"), TEXT("FKey name (e.g. 'SpaceBar', 'LeftMouseButton')."), true)
		.EnumArg(TEXT("event"), TEXT("Input event type (default 'Pressed')."),
			{TEXT("Pressed"), TEXT("Released"), TEXT("Repeat")})
		.IntArg(TEXT("controller_index"), TEXT("Player controller index (default 0)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString KeyName;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("key"), KeyName));

			FString EventStr = TEXT("Pressed");
			Args->TryGetStringField(TEXT("event"), EventStr);
			static const TArray<FString> AllowedEvents = {
				TEXT("Pressed"), TEXT("Released"), TEXT("Repeat")
			};
			BAIL_IF_INVALID(FMCPValidate::OneOf(EventStr, AllowedEvents, TEXT("event")));

			int32 ControllerIndex = 0;
			if (Args->HasField(TEXT("controller_index")))
			{
				ControllerIndex = (int32)Args->GetNumberField(TEXT("controller_index"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(ControllerIndex, 0, 7, TEXT("controller_index")));
			}

			if (!IsPIEActive())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("PIE is not running"),
					TEXT("Call pie_start first."));
			}

			APlayerController* PC = GetPIEPlayerController(ControllerIndex);
			if (!PC)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("No PIE player controller at index %d"), ControllerIndex));
			}

			FKey Key(*KeyName);
			if (!Key.IsValid())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidName,
					FString::Printf(TEXT("Unknown FKey: '%s'"), *KeyName),
					TEXT("Use FKey names like 'SpaceBar', 'LeftMouseButton', 'Gamepad_FaceButton_Bottom'."));
			}

			const EInputEvent Event = ParseInputEvent(EventStr);
			const float Amount = (Event == IE_Released) ? 0.0f : 1.0f;

			// v4.5 (5.8): uses the FInputKeyEventArgs::CreateSimulated API (the
			// FInputKeyParams overload was deprecated in 5.6 and is gone). Note:
			// synthetic input through PlayerController can be intercepted by Slate
			// focus rules; if the PIE viewport doesn't have focus the input may be
			// ignored — the returned `consumed` flag reflects whether it was handled.
			FInputKeyEventArgs EventArgs = FInputKeyEventArgs::CreateSimulated(
				Key, Event, Amount, /*InNumSamplesOverride*/ -1,
				FInputDeviceId::CreateFromInternalId(ControllerIndex),
				/*bIsTouchEvent*/ false,
				/*Viewport*/ nullptr);

			const bool bConsumed = PC->InputKey(EventArgs);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("ok"), true);
			Result->SetStringField(TEXT("key"), KeyName);
			Result->SetStringField(TEXT("event"), EventStr);
			Result->SetBoolField(TEXT("consumed"), bConsumed);
			Result->SetNumberField(TEXT("controller_index"), ControllerIndex);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Sent %s %s to PC[%d] (consumed=%s)"),
					*KeyName, *EventStr, ControllerIndex, bConsumed ? TEXT("yes") : TEXT("no")),
				Result);
		});

	// ================================================================
	// pie_attach_player_controller
	// ================================================================
	MCP_TOOL(Registry, "pie_attach_player_controller")
		.Description(TEXT("Possess an actor (must be a Pawn) at runtime with the given player controller. Useful for debugging gameplay flows."))
		.Destructive()
		.StringArg(TEXT("actor_label"), TEXT("Editor label of the Pawn actor to possess."), true)
		.IntArg(TEXT("controller_index"), TEXT("Player controller index (default 0)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			FString Label;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("actor_label"), Label));

			int32 ControllerIndex = 0;
			if (Args->HasField(TEXT("controller_index")))
			{
				ControllerIndex = (int32)Args->GetNumberField(TEXT("controller_index"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(ControllerIndex, 0, 7, TEXT("controller_index")));
			}

			if (!IsPIEActive())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("PIE is not running"),
					TEXT("Call pie_start first."));
			}

			UWorld* PIEWorld = GetActivePIEWorld();
			AActor* Actor = FindActorByLabel(PIEWorld, Label);
			if (!Actor)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Actor not found in PIE world: %s"), *Label));
			}

			APawn* Pawn = Cast<APawn>(Actor);
			if (!Pawn)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Unsupported,
					FString::Printf(TEXT("Actor '%s' is not a Pawn"), *Label),
					TEXT("Only APawn-derived actors can be possessed."));
			}

			APlayerController* PC = GetPIEPlayerController(ControllerIndex);
			if (!PC)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("No PIE player controller at index %d"), ControllerIndex));
			}

			PC->Possess(Pawn);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("ok"), true);
			Result->SetStringField(TEXT("possessed_actor"), Label);
			Result->SetNumberField(TEXT("controller_index"), ControllerIndex);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("PC[%d] now possesses '%s'"), ControllerIndex, *Label),
				Result);
		});

} // void RegisterAll

} // namespace MCPPIETools::Input
