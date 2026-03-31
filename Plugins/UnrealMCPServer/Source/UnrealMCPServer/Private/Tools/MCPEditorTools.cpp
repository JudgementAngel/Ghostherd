#include "Tools/MCPEditorTools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "SLevelViewport.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"
#include "Engine/GameViewportClient.h"
#include "Slate/SceneViewport.h"

namespace MCPEditorTools
{

static FLevelEditorViewportClient* GetActiveViewportClient()
{
	FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor.GetFirstActiveLevelViewport();
	if (ActiveViewport.IsValid())
	{
		return &ActiveViewport->GetLevelViewportClient();
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// focus_viewport - Focus the editor viewport
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("actor_name"), TEXT("Focus on this actor by label"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("Focus on this X position"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Focus on this Y position"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Focus on this Z position"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("distance"), TEXT("Camera distance from target (default: 500)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("focus_viewport");
		Def.Description = TEXT("Focus the editor viewport camera on a specific actor or world position. Provide either actor_name or x/y/z coordinates.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient();
			if (!ViewportClient) return FMCPToolResult::Error(TEXT("No active viewport"));

			FString ActorName;
			if (Args->TryGetStringField(TEXT("actor_name"), ActorName))
			{
				UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
				if (!World) return FMCPToolResult::Error(TEXT("No editor world"));

				for (TActorIterator<AActor> It(World); It; ++It)
				{
					if ((*It)->GetActorLabel() == ActorName)
					{
						GEditor->MoveViewportCamerasToActor(**It, false);
						return FMCPToolResult::Success(FString::Printf(TEXT("Focused viewport on actor '%s'"), *ActorName));
					}
				}
				return FMCPToolResult::Error(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
			}

			if (Args->HasField(TEXT("x")) || Args->HasField(TEXT("y")) || Args->HasField(TEXT("z")))
			{
				FVector Location(
					Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0,
					Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0,
					Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 0.0
				);

				double Distance = Args->HasField(TEXT("distance")) ? Args->GetNumberField(TEXT("distance")) : 500.0;

				ViewportClient->SetViewLocation(Location - ViewportClient->GetViewRotation().Vector() * Distance);
				ViewportClient->Invalidate();

				return FMCPToolResult::Success(FString::Printf(TEXT("Focused viewport on (%.1f, %.1f, %.1f)"),
					Location.X, Location.Y, Location.Z));
			}

			return FMCPToolResult::Error(TEXT("Provide either actor_name or x/y/z coordinates"));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// set_viewport_camera - Set viewport camera position and rotation
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("x"), TEXT("Camera X position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("y"), TEXT("Camera Y position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("z"), TEXT("Camera Z position"), true);
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("pitch"), TEXT("Camera pitch (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("yaw"), TEXT("Camera yaw (default: 0)"));
		FMCPSchemaBuilder::AddNumber(Schema, TEXT("roll"), TEXT("Camera roll (default: 0)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("set_viewport_camera");
		Def.Description = TEXT("Set the editor viewport camera position and rotation directly.");
		Def.InputSchema = Schema;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient();
			if (!ViewportClient) return FMCPToolResult::Error(TEXT("No active viewport"));

			FVector Location(
				Args->GetNumberField(TEXT("x")),
				Args->GetNumberField(TEXT("y")),
				Args->GetNumberField(TEXT("z"))
			);
			FRotator Rotation(
				Args->HasField(TEXT("pitch")) ? Args->GetNumberField(TEXT("pitch")) : 0.0,
				Args->HasField(TEXT("yaw")) ? Args->GetNumberField(TEXT("yaw")) : 0.0,
				Args->HasField(TEXT("roll")) ? Args->GetNumberField(TEXT("roll")) : 0.0
			);

			ViewportClient->SetViewLocation(Location);
			ViewportClient->SetViewRotation(Rotation);
			ViewportClient->Invalidate();

			return FMCPToolResult::Success(FString::Printf(
				TEXT("Camera set to Position(%.1f, %.1f, %.1f) Rotation(%.1f, %.1f, %.1f)"),
				Location.X, Location.Y, Location.Z, Rotation.Pitch, Rotation.Yaw, Rotation.Roll));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// take_screenshot - Capture viewport as base64 JPEG image
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("width"), TEXT("Image width in pixels (default: 1280)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("height"), TEXT("Image height in pixels (default: 720)"));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("quality"), TEXT("JPEG quality 1-100 (default: 70)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("take_screenshot");
		Def.Description = TEXT("Capture the current editor viewport as an image. Returns a base64-encoded JPEG. Useful for visual verification of scene state.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FLevelEditorViewportClient* ViewportClient = GetActiveViewportClient();
			if (!ViewportClient) return FMCPToolResult::Error(TEXT("No active viewport"));

			int32 TargetW = Args->HasField(TEXT("width")) ? (int32)Args->GetNumberField(TEXT("width")) : 1280;
			int32 TargetH = Args->HasField(TEXT("height")) ? (int32)Args->GetNumberField(TEXT("height")) : 720;
			TargetW = FMath::Clamp(TargetW, 64, 1920);
			TargetH = FMath::Clamp(TargetH, 64, 1080);

			int32 Quality = Args->HasField(TEXT("quality")) ? (int32)Args->GetNumberField(TEXT("quality")) : 70;
			Quality = FMath::Clamp(Quality, 1, 100);

			FViewport* Viewport = ViewportClient->Viewport;
			if (!Viewport) return FMCPToolResult::Error(TEXT("Viewport not available"));

			// Read pixels from viewport
			TArray<FColor> Bitmap;
			if (!Viewport->ReadPixels(Bitmap))
			{
				return FMCPToolResult::Error(TEXT("Failed to read viewport pixels"));
			}

			int32 ViewW = Viewport->GetSizeXY().X;
			int32 ViewH = Viewport->GetSizeXY().Y;

			if (ViewW <= 0 || ViewH <= 0 || Bitmap.Num() == 0)
			{
				return FMCPToolResult::Error(TEXT("Viewport has no pixels"));
			}

			// Downscale if viewport is larger than target
			TArray<FColor> ResizedBitmap;
			int32 FinalW = ViewW;
			int32 FinalH = ViewH;

			if (ViewW > TargetW || ViewH > TargetH)
			{
				FinalW = TargetW;
				FinalH = TargetH;
				ResizedBitmap.SetNumUninitialized(FinalW * FinalH);

				// Bilinear downsample
				for (int32 Y = 0; Y < FinalH; Y++)
				{
					for (int32 X = 0; X < FinalW; X++)
					{
						float SrcX = (float)X / (float)FinalW * (float)ViewW;
						float SrcY = (float)Y / (float)FinalH * (float)ViewH;

						int32 SrcXi = FMath::Clamp((int32)SrcX, 0, ViewW - 1);
						int32 SrcYi = FMath::Clamp((int32)SrcY, 0, ViewH - 1);

						ResizedBitmap[Y * FinalW + X] = Bitmap[SrcYi * ViewW + SrcXi];
					}
				}
			}

			const TArray<FColor>& FinalBitmap = ResizedBitmap.Num() > 0 ? ResizedBitmap : Bitmap;

			// Compress to JPEG for much smaller size
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

			if (!ImageWrapper.IsValid())
			{
				return FMCPToolResult::Error(TEXT("Failed to create image wrapper"));
			}

			TArray<uint8> RawData;
			RawData.SetNumUninitialized(FinalW * FinalH * 4);
			FMemory::Memcpy(RawData.GetData(), FinalBitmap.GetData(), RawData.Num());

			if (!ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), FinalW, FinalH, ERGBFormat::BGRA, 8))
			{
				return FMCPToolResult::Error(TEXT("Failed to set raw image data"));
			}

			TArray64<uint8> JpegData = ImageWrapper->GetCompressed(Quality);
			if (JpegData.Num() == 0)
			{
				return FMCPToolResult::Error(TEXT("Failed to compress JPEG"));
			}

			FString Base64 = FBase64::Encode(JpegData.GetData(), JpegData.Num());

			return FMCPToolResult::WithImage(
				FString::Printf(TEXT("Captured viewport screenshot (%dx%d, JPEG q%d, %dKB)"),
					FinalW, FinalH, Quality, JpegData.Num() / 1024),
				Base64, TEXT("image/jpeg"));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_selection - Get currently selected actors
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_selection");
		Def.Description = TEXT("Get the list of currently selected actors in the editor.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			TArray<FString> SelectedNames;
			USelection* Selection = GEditor->GetSelectedActors();
			for (int32 i = 0; i < Selection->Num(); i++)
			{
				AActor* Actor = Cast<AActor>(Selection->GetSelectedObject(i));
				if (Actor)
				{
					SelectedNames.Add(FString::Printf(TEXT("%s (%s)"), *Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
				}
			}

			if (SelectedNames.Num() == 0)
			{
				return FMCPToolResult::Success(TEXT("No actors selected"));
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Selected %d actors:\n%s"),
				SelectedNames.Num(), *FString::Join(SelectedNames, TEXT("\n"))));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// undo - Undo last operation
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("count"), TEXT("Number of undo steps (default: 1)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("undo");
		Def.Description = TEXT("Undo the last editor operation(s). Equivalent to Ctrl+Z.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			int32 Count = 1;
			if (Args->HasField(TEXT("count")))
			{
				Count = FMath::Clamp((int32)Args->GetNumberField(TEXT("count")), 1, 50);
			}

			bool bSuccess = false;
			for (int32 i = 0; i < Count; i++)
			{
				bSuccess = GEditor->UndoTransaction(true);
				if (!bSuccess) break;
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Undid %d operation(s)"), Count));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// redo - Redo last undone operation
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("count"), TEXT("Number of redo steps (default: 1)"));

		FMCPToolDefinition Def;
		Def.Name = TEXT("redo");
		Def.Description = TEXT("Redo the last undone operation(s). Equivalent to Ctrl+Y.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			int32 Count = 1;
			if (Args->HasField(TEXT("count")))
			{
				Count = FMath::Clamp((int32)Args->GetNumberField(TEXT("count")), 1, 50);
			}

			for (int32 i = 0; i < Count; i++)
			{
				GEditor->RedoTransaction();
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Redid %d operation(s)"), Count));
		});
		Registry.RegisterTool(Def);
	}

	// ================================================================
	// run_console_command - Execute a UE console command
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("command"), TEXT("Console command to execute (e.g., 'stat fps', 'show collision')"), true);

		FMCPToolDefinition Def;
		Def.Name = TEXT("run_console_command");
		Def.Description = TEXT("Execute an Unreal Engine console command. Useful for toggling debug visualizations, changing rendering settings, and more.");
		Def.InputSchema = Schema;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString Command;
			if (!Args->TryGetStringField(TEXT("command"), Command))
				return FMCPToolResult::Error(TEXT("command is required"));

			// Safety check - block dangerous commands
			FString UpperCmd = Command.ToUpper().TrimStartAndEnd();
			if (UpperCmd.StartsWith(TEXT("EXIT")) || UpperCmd.StartsWith(TEXT("QUIT")))
			{
				return FMCPToolResult::Error(TEXT("This command is not allowed via MCP"));
			}

			UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
			if (World)
			{
				GEditor->Exec(World, *Command);
			}

			return FMCPToolResult::Success(FString::Printf(TEXT("Executed console command: %s"), *Command));
		});
		Registry.RegisterTool(Def);
	}
}

} // namespace MCPEditorTools
