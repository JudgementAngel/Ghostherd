// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/PIE/PIECommon.h"

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"
#include "MCPValidate.h"

#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "UnrealClient.h"
#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Modules/ModuleManager.h"
#include "Misc/Base64.h"

namespace MCPPIETools::Capture
{

using namespace MCPPIETools::Common;

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// pie_screenshot
	// ================================================================
	MCP_TOOL(Registry, "pie_screenshot")
		.Description(TEXT("Capture the active PIE viewport as a PNG. Returns base64-encoded image data plus the captured dimensions."))
		.ReadOnly()
		.IntArg(TEXT("width"), TEXT("Target width in pixels (capped at viewport width, default = viewport)."))
		.IntArg(TEXT("height"), TEXT("Target height in pixels (capped at viewport height, default = viewport)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			if (!IsPIEActive() || !GEditor)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					TEXT("PIE is not running"),
					TEXT("Call pie_start first."));
			}

			FViewport* Viewport = GEditor->GetPIEViewport();
			if (!Viewport)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("PIE viewport is not available"));
			}

			TArray<FColor> Bitmap;
			if (!Viewport->ReadPixels(Bitmap))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Failed to read PIE viewport pixels"));
			}

			const int32 ViewW = Viewport->GetSizeXY().X;
			const int32 ViewH = Viewport->GetSizeXY().Y;
			if (ViewW <= 0 || ViewH <= 0 || Bitmap.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("PIE viewport has no pixels"));
			}

			int32 TargetW = ViewW;
			int32 TargetH = ViewH;
			if (Args->HasField(TEXT("width")))
			{
				TargetW = FMath::Clamp((int32)Args->GetNumberField(TEXT("width")), 16, ViewW);
			}
			if (Args->HasField(TEXT("height")))
			{
				TargetH = FMath::Clamp((int32)Args->GetNumberField(TEXT("height")), 16, ViewH);
			}

			TArray<FColor> Resized;
			int32 FinalW = ViewW;
			int32 FinalH = ViewH;
			if (TargetW != ViewW || TargetH != ViewH)
			{
				FinalW = TargetW;
				FinalH = TargetH;
				Resized.SetNumUninitialized(FinalW * FinalH);
				for (int32 Y = 0; Y < FinalH; ++Y)
				{
					for (int32 X = 0; X < FinalW; ++X)
					{
						const int32 SrcX = FMath::Clamp((int32)((float)X / FinalW * ViewW), 0, ViewW - 1);
						const int32 SrcY = FMath::Clamp((int32)((float)Y / FinalH * ViewH), 0, ViewH - 1);
						Resized[Y * FinalW + X] = Bitmap[SrcY * ViewW + SrcX];
					}
				}
			}

			const TArray<FColor>& Final = Resized.Num() > 0 ? Resized : Bitmap;

			IImageWrapperModule& Mod = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> Wrapper = Mod.CreateImageWrapper(EImageFormat::PNG);
			if (!Wrapper.IsValid())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Failed to create PNG wrapper"));
			}

			// FColor pixels are BGRA8.
			if (!Wrapper->SetRaw(Final.GetData(), Final.Num() * sizeof(FColor),
				FinalW, FinalH, ERGBFormat::BGRA, 8))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("Failed to set raw pixels"));
			}

			TArray64<uint8> Compressed = Wrapper->GetCompressed(100);
			if (Compressed.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal, TEXT("PNG compression failed"));
			}

			FString Base64 = FBase64::Encode(Compressed.GetData(), Compressed.Num());

			// Mirror text+image content (matches take_screenshot pattern). Also expose
			// structured payload with width/height/base64 for agents that prefer JSON.
			FMCPToolResult Result = FMCPToolResult::WithImage(
				FString::Printf(TEXT("PIE viewport screenshot (%dx%d, %.1f KB)"),
					FinalW, FinalH, Compressed.Num() / 1024.0f),
				Base64, TEXT("image/png"));

			TSharedPtr<FJsonObject> Structured = MakeShared<FJsonObject>();
			Structured->SetBoolField(TEXT("ok"), true);
			Structured->SetNumberField(TEXT("width"), FinalW);
			Structured->SetNumberField(TEXT("height"), FinalH);
			Structured->SetStringField(TEXT("base64_png"), Base64);
			Result.StructuredContent = Structured;
			return Result;
		});

} // void RegisterAll

} // namespace MCPPIETools::Capture
