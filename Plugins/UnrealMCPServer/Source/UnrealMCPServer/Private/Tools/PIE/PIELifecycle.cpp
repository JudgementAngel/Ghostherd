// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/PIE/PIECommon.h"

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Misc/App.h"

namespace MCPPIETools::Lifecycle
{

using namespace MCPPIETools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// pie_start
	// ================================================================
	MCP_TOOL(Registry, "pie_start")
		.Description(TEXT("Start a Play-In-Editor session. Mode determines viewport (Selected/Standalone/MobilePreview/VRPreview). Returns an error if PIE is already running."))
		.Destructive()
		.EnumArg(TEXT("mode"), TEXT("PIE preview mode (default 'Selected')."),
			{TEXT("Selected"), TEXT("Standalone"), TEXT("MobilePreview"), TEXT("VRPreview")})
		.IntArg(TEXT("num_players"), TEXT("Number of players (1-4, default 1)."))
		.IntArg(TEXT("window_width"), TEXT("Window width (only used for Standalone/MobilePreview)."))
		.IntArg(TEXT("window_height"), TEXT("Window height."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			if (!GEditor)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("GEditor is not available"));
			}

			if (IsPIEActive())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::AlreadyExists,
					TEXT("PIE is already running"),
					TEXT("Call pie_stop first."));
			}

			FString Mode = TEXT("Selected");
			Args->TryGetStringField(TEXT("mode"), Mode);
			static const TArray<FString> AllowedModes = {
				TEXT("Selected"), TEXT("Standalone"), TEXT("MobilePreview"), TEXT("VRPreview")
			};
			BAIL_IF_INVALID(FMCPValidate::OneOf(Mode, AllowedModes, TEXT("mode")));

			int32 NumPlayers = 1;
			if (Args->HasField(TEXT("num_players")))
			{
				NumPlayers = (int32)Args->GetNumberField(TEXT("num_players"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(NumPlayers, 1, 4, TEXT("num_players")));
			}

			int32 WinW = 0, WinH = 0;
			if (Args->HasField(TEXT("window_width")))
			{
				WinW = (int32)Args->GetNumberField(TEXT("window_width"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(WinW, 64, 7680, TEXT("window_width")));
			}
			if (Args->HasField(TEXT("window_height")))
			{
				WinH = (int32)Args->GetNumberField(TEXT("window_height"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(WinH, 64, 4320, TEXT("window_height")));
			}

			// v4.5: apply player count + window size through ULevelEditorPlaySettings
			// (the same settings the Editor Preferences "Play" page drives). These are
			// read by RequestPlaySession when it builds the session.
			if (ULevelEditorPlaySettings* PlaySettings = GetMutableDefault<ULevelEditorPlaySettings>())
			{
				PlaySettings->SetPlayNumberOfClients(NumPlayers);
				if (WinW > 0 && WinH > 0)
				{
					PlaySettings->NewWindowWidth = WinW;
					PlaySettings->NewWindowHeight = WinH;
				}
				PlaySettings->PostEditChange();
			}

			FRequestPlaySessionParams Params;
			Params.SessionDestination = EPlaySessionDestinationType::InProcess;
			Params.WorldType = EPlaySessionWorldType::PlayInEditor;

			if (Mode == TEXT("MobilePreview"))
			{
				Params.SessionPreviewTypeOverride = EPlaySessionPreviewType::MobilePreview;
			}
			else if (Mode == TEXT("VRPreview"))
			{
				Params.SessionPreviewTypeOverride = EPlaySessionPreviewType::VRPreview;
			}
			else if (Mode == TEXT("Standalone"))
			{
				// Standalone == NewProcess.
				Params.SessionDestination = EPlaySessionDestinationType::NewProcess;
			}
			// "Selected" -> default in-process editor viewport, no preview override.

			// v4.5: num_players / window size are applied via ULevelEditorPlaySettings above.
			GEditor->RequestPlaySession(Params);
			GEditor->StartQueuedPlaySessionRequest();

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("ok"), true);
			Result->SetStringField(TEXT("mode"), Mode);
			Result->SetNumberField(TEXT("num_players"), NumPlayers);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("PIE start requested (mode=%s, num_players=%d)"), *Mode, NumPlayers),
				Result);
		});

	// ================================================================
	// pie_stop
	// ================================================================
	MCP_TOOL(Registry, "pie_stop")
		.Description(TEXT("Terminate the active Play-In-Editor session. Idempotent: succeeds even if PIE is not running."))
		.Destructive()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			if (!GEditor)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("GEditor is not available"));
			}

			if (IsPIEActive())
			{
				GEditor->RequestEndPlayMap();
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("ok"), true);
			return FMCPToolResult::SuccessStructured(TEXT("PIE stop requested"), Result);
		});

	// ================================================================
	// pie_pause
	// ================================================================
	MCP_TOOL(Registry, "pie_pause")
		.Description(TEXT("Pause the active PIE session. Returns was_running=false if PIE was not active."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			if (!IsPIEActive())
			{
				TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
				R->SetBoolField(TEXT("ok"), false);
				R->SetBoolField(TEXT("was_running"), false);
				return FMCPToolResult::SuccessStructured(TEXT("PIE is not running"), R);
			}

			const bool bOk = GEditor->SetPIEWorldsPaused(true);
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("ok"), bOk);
			Result->SetBoolField(TEXT("was_running"), true);
			return FMCPToolResult::SuccessStructured(
				bOk ? TEXT("PIE paused") : TEXT("PIE pause request failed"), Result);
		});

	// ================================================================
	// pie_resume
	// ================================================================
	MCP_TOOL(Registry, "pie_resume")
		.Description(TEXT("Resume a paused PIE session."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			if (!IsPIEActive())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("PIE is not running"),
					TEXT("Call pie_start first."));
			}

			const bool bOk = GEditor->SetPIEWorldsPaused(false);
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("ok"), bOk);
			return FMCPToolResult::SuccessStructured(
				bOk ? TEXT("PIE resumed") : TEXT("PIE resume request failed"), Result);
		});

	// ================================================================
	// pie_step_frame
	// ================================================================
	MCP_TOOL(Registry, "pie_step_frame")
		.Description(TEXT("Advance N frames while paused via UEditorEngine::PlaySessionSingleStepped(). PIE must be paused. Default 1 frame, range 1-60."))
		.IntArg(TEXT("frames"), TEXT("Number of frames to advance (1-60, default 1)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			if (!IsPIEActive())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("PIE is not running"),
					TEXT("Call pie_start, then pie_pause first."));
			}

			int32 Frames = 1;
			if (Args->HasField(TEXT("frames")))
			{
				Frames = (int32)Args->GetNumberField(TEXT("frames"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(Frames, 1, 60, TEXT("frames")));
			}

			// TODO(D1): PlaySessionSingleStepped advances one tick when next-tick fires.
			// Calling it N times in a single tool invocation queues N steps but they
			// only advance as the editor ticks; agents stepping >1 frame should poll
			// pie_get_state in between or expect a single step in practice.
			for (int32 i = 0; i < Frames; ++i)
			{
				GEditor->PlaySessionSingleStepped();
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("ok"), true);
			Result->SetNumberField(TEXT("frames_advanced"), Frames);
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Stepped %d frame(s)"), Frames), Result);
		});

	// ================================================================
	// pie_get_state
	// ================================================================
	MCP_TOOL(Registry, "pie_get_state")
		.Description(TEXT("Query PIE state: is_running, is_paused, num_players, world_time_seconds, world_path, fps_estimate."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			const bool bRunning = IsPIEActive();
			Result->SetBoolField(TEXT("is_running"), bRunning);

			if (!bRunning)
			{
				Result->SetBoolField(TEXT("is_paused"), false);
				Result->SetNumberField(TEXT("num_players"), 0);
				Result->SetNumberField(TEXT("world_time_seconds"), 0.0);
				Result->SetStringField(TEXT("world_path"), TEXT(""));
				const float Delta = FApp::GetDeltaTime();
				Result->SetNumberField(TEXT("fps_estimate"), Delta > 0.0f ? 1.0f / Delta : 0.0f);
				return FMCPToolResult::SuccessStructured(TEXT("PIE not running"), Result);
			}

			UWorld* PIEWorld = GetActivePIEWorld();
			bool bPaused = false;
			int32 NumPlayers = 0;
			double WorldTime = 0.0;
			FString WorldPath;

			if (PIEWorld)
			{
				bPaused = UGameplayStatics::IsGamePaused(PIEWorld);
				WorldTime = PIEWorld->GetTimeSeconds();
				WorldPath = PIEWorld->GetPathName();

				for (FConstPlayerControllerIterator It = PIEWorld->GetPlayerControllerIterator(); It; ++It)
				{
					if (It->IsValid()) ++NumPlayers;
				}
			}

			const float Delta = FApp::GetDeltaTime();
			const float Fps = Delta > 0.0f ? 1.0f / Delta : 0.0f;

			Result->SetBoolField(TEXT("is_paused"), bPaused);
			Result->SetNumberField(TEXT("num_players"), NumPlayers);
			Result->SetNumberField(TEXT("world_time_seconds"), WorldTime);
			Result->SetStringField(TEXT("world_path"), WorldPath);
			Result->SetNumberField(TEXT("fps_estimate"), Fps);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("PIE running (paused=%s, players=%d, t=%.2fs, fps=%.1f)"),
					bPaused ? TEXT("yes") : TEXT("no"), NumPlayers, WorldTime, Fps),
				Result);
		});

} // void RegisterAll

} // namespace MCPPIETools::Lifecycle
