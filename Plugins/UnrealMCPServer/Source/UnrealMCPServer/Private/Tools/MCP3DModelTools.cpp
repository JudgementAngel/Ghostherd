// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/MCP3DModelTools.h"
#include "Common/MCPPythonCall.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPToolBuilder.h"
#include "MCPSettings.h"

#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "IPythonScriptPlugin.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace MCP3DModelTools
{

static FString GetGenerated3DDir()
{
	FString Dir = FPaths::ProjectSavedDir() / TEXT("MCPGenerated3D");
	IFileManager::Get().MakeDirectory(*Dir, true);
	return Dir;
}

// v4 Phase 3: thin wrappers over the shared MCPCommon Python-call helpers
// (deduplicated with MCPUIImageTools.cpp).
static FString BuildPythonCall3D(const FString& ScriptPath, const FString& FunctionCall)
{
	return MCPCommon::BuildPythonScriptCall(ScriptPath,
		{ TEXT("text_to_3d"), TEXT("image_to_3d") }, FunctionCall, TEXT("MCP_3D_RESULT"));
}

static FString PyEscape(const FString& S)
{
	return MCPCommon::PyEscape(S);
}

static FString FindScriptPath()
{
	return MCPCommon::FindPluginPythonScript(TEXT("fal_3d_gen.py"));
}

// Try to import a .glb file into UE via the Interchange/glTF pipeline
static UObject* ImportGLB(const FString& GlbPath, const FString& DestPath)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<FString> FilesToImport;
	FilesToImport.Add(GlbPath);

	TArray<UObject*> ImportedAssets = AssetTools.ImportAssets(FilesToImport, DestPath);

	if (ImportedAssets.Num() > 0)
	{
		return ImportedAssets[0];
	}
	return nullptr;
}

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// generate_3d_model - Text-to-3D via fal.ai
	// ================================================================
	MCP_TOOL(Registry, "generate_3d_model")
		.LongRunning()
		.Description(TEXT(
			"Generate a 3D model from a text prompt using fal.ai and import it into UE as a StaticMesh. "
			"Models: meshy-v6 (recommended, textured, supports PBR), meshy-v6-preview (faster/untextured), "
			"hunyuan-pro (highest fidelity up to 1.5M faces), hunyuan-rapid (faster), hunyuan-v3. "
			"Output is GLB format, imported via UE Interchange/glTF pipeline. "
			"NOTE: 3D generation takes 1-10 minutes depending on model. "
			"Requires fal.ai API key and PythonScriptPlugin."))
		.StringArg(TEXT("prompt"), TEXT("Text description of the 3D model to generate (e.g., 'medieval wooden treasure chest')"), true)
		.StringArg(TEXT("destination_path"), TEXT("Content path for imported mesh (default: '/Game/Meshes/Generated/')"))
		.EnumArg(TEXT("model"), TEXT("fal.ai text-to-3d model. meshy-v6 is recommended for quality+speed. hunyuan-pro for highest fidelity."), { TEXT("meshy-v6"), TEXT("meshy-v6-preview"), TEXT("hunyuan-pro"), TEXT("hunyuan-rapid"), TEXT("hunyuan-v3") })
		.EnumArg(TEXT("art_style"), TEXT("Art style for Meshy models"), { TEXT("realistic"), TEXT("sculpture") })
		.EnumArg(TEXT("topology"), TEXT("Mesh topology type"), { TEXT("triangle"), TEXT("quad") })
		.NumberArg(TEXT("target_polycount"), TEXT("Target polygon count (default: 30000). Higher = more detail, larger file."))
		.BoolArg(TEXT("enable_pbr"), TEXT("Generate PBR texture maps (base_color, metallic, normal, roughness)"))
		.StringArg(TEXT("model_name"), TEXT("Override the generated mesh asset name (auto-generated if omitted)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const UMCPSettings* Settings = UMCPSettings::Get();

			FString ApiKey = Settings->FalAIApiKey;
			if (ApiKey.IsEmpty())
				return FMCPToolResult::Error(TEXT("fal.ai API key is not configured. Set it in Project Settings > Plugins > Unreal MCP Server."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is not loaded. Enable it in Edit > Plugins."));

			FString Prompt;
			if (!Args->TryGetStringField(TEXT("prompt"), Prompt))
				return FMCPToolResult::Error(TEXT("prompt is required"));

			FString DestPath = TEXT("/Game/Meshes/Generated/");
			Args->TryGetStringField(TEXT("destination_path"), DestPath);

			FString Model = Settings->DefaultTextTo3DModel.IsEmpty() ? TEXT("meshy-v6") : Settings->DefaultTextTo3DModel;
			Args->TryGetStringField(TEXT("model"), Model);

			FString ArtStyle = TEXT("realistic");
			Args->TryGetStringField(TEXT("art_style"), ArtStyle);

			FString Topology = TEXT("triangle");
			Args->TryGetStringField(TEXT("topology"), Topology);

			double PolyCount = 30000;
			Args->TryGetNumberField(TEXT("target_polycount"), PolyCount);

			bool bEnablePBR = false;
			Args->TryGetBoolField(TEXT("enable_pbr"), bEnablePBR);

			FString ModelName;
			Args->TryGetStringField(TEXT("model_name"), ModelName);

			FString OutputDir = GetGenerated3DDir();
			FString ScriptPath = FindScriptPath();

			FString FnCall = FString::Printf(TEXT(
				"_fn_text_to_3d(\n"
				"        prompt='%s',\n"
				"        api_key='%s',\n"
				"        output_dir='%s',\n"
				"        model='%s',\n"
				"        model_name=%s,\n"
				"        art_style='%s',\n"
				"        topology='%s',\n"
				"        target_polycount=%d,\n"
				"        enable_pbr=%s,\n"
				"    )"
			),
				*PyEscape(Prompt),
				*PyEscape(ApiKey),
				*PyEscape(OutputDir),
				*Model,
				ModelName.IsEmpty() ? TEXT("None") : *FString::Printf(TEXT("'%s'"), *PyEscape(ModelName)),
				*ArtStyle,
				*Topology,
				(int32)PolyCount,
				bEnablePBR ? TEXT("True") : TEXT("False")
			);

			FString PythonCode = BuildPythonCall3D(ScriptPath, FnCall);
			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
				return FMCPToolResult::Error(TEXT("Python execution failed. Check Output Log for fal.ai API errors."));

			// Find the most recently created GLB in output dir
			TArray<FString> GlbFiles;
			IFileManager::Get().FindFiles(GlbFiles, *(OutputDir / TEXT("*.glb")), true, false);

			if (GlbFiles.Num() == 0)
				return FMCPToolResult::Error(TEXT("3D generation may have failed - no GLB files found. Check Output Log for errors. Note: 3D generation can take several minutes."));

			FString LatestFile;
			FDateTime LatestTime = FDateTime::MinValue();
			for (const FString& File : GlbFiles)
			{
				FString FullPath = OutputDir / File;
				FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
				if (ModTime > LatestTime)
				{
					LatestTime = ModTime;
					LatestFile = FullPath;
				}
			}

			if (LatestFile.IsEmpty())
				return FMCPToolResult::Error(TEXT("Could not find generated GLB file"));

			// Import GLB into UE content browser
			UObject* ImportedAsset = ImportGLB(LatestFile, DestPath);

			if (!ImportedAsset)
			{
				// GLB was downloaded but import failed - still useful
				return FMCPToolResult::Success(FString::Printf(
					TEXT("3D model generated and saved to: %s (GLB import into UE failed - you may need to import manually via Content Browser. "
						 "Ensure the Interchange/glTF plugin is enabled.)"),
					*LatestFile));
			}

			FString ContentPath = ImportedAsset->GetPathName();
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Generated and imported 3D model. Asset: %s (model: %s, polycount: %d%s). GLB file: %s"),
				*ContentPath, *Model, (int32)PolyCount,
				bEnablePBR ? TEXT(", PBR enabled") : TEXT(""),
				*LatestFile));
		});

	// ================================================================
	// image_to_3d_model - Image-to-3D via fal.ai
	// ================================================================
	MCP_TOOL(Registry, "image_to_3d_model")
		.LongRunning()
		.Description(TEXT(
			"Generate a 3D model from an image using fal.ai and import it into UE as a StaticMesh. "
			"Models: trellis-2 (best quality, native 3D generation), meshy-v6-img (supports PBR/rigging/animation), "
			"hunyuan-v3-img, hunyuan-pro-img (high fidelity), rodin-v2 (production-ready), tripo-v2 (good stylized output). "
			"Provide a publicly accessible image URL. Output is GLB, imported via Interchange. "
			"NOTE: 3D generation takes 1-10 minutes. "
			"Requires fal.ai API key and PythonScriptPlugin."))
		.StringArg(TEXT("image_url"), TEXT("URL of source image (publicly accessible). Can also be a base64 data URI."), true)
		.StringArg(TEXT("destination_path"), TEXT("Content path for imported mesh (default: '/Game/Meshes/Generated/')"))
		.EnumArg(TEXT("model"), TEXT("fal.ai image-to-3d model. trellis-2 is recommended for best quality. meshy-v6-img for PBR/rigging support."), { TEXT("trellis-2"), TEXT("meshy-v6-img"), TEXT("hunyuan-v3-img"), TEXT("hunyuan-pro-img"), TEXT("rodin-v2"), TEXT("tripo-v2") })
		.EnumArg(TEXT("topology"), TEXT("Mesh topology type (Meshy models only)"), { TEXT("triangle"), TEXT("quad") })
		.NumberArg(TEXT("target_polycount"), TEXT("Target polygon/vertex count (default: 30000). Range depends on model."))
		.BoolArg(TEXT("enable_pbr"), TEXT("Generate PBR texture maps (Meshy/Hunyuan models)"))
		.NumberArg(TEXT("texture_size"), TEXT("Texture resolution for Trellis-2: 1024, 2048, or 4096 (default: 2048)"))
		.StringArg(TEXT("model_name"), TEXT("Override the generated mesh asset name (auto-generated if omitted)"))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			const UMCPSettings* Settings = UMCPSettings::Get();

			FString ApiKey = Settings->FalAIApiKey;
			if (ApiKey.IsEmpty())
				return FMCPToolResult::Error(TEXT("fal.ai API key is not configured. Set it in Project Settings > Plugins > Unreal MCP Server."));

			IPythonScriptPlugin* PythonPlugin = FModuleManager::GetModulePtr<IPythonScriptPlugin>(TEXT("PythonScriptPlugin"));
			if (!PythonPlugin)
				return FMCPToolResult::Error(TEXT("PythonScriptPlugin is not loaded. Enable it in Edit > Plugins."));

			FString ImageUrl;
			if (!Args->TryGetStringField(TEXT("image_url"), ImageUrl))
				return FMCPToolResult::Error(TEXT("image_url is required"));

			FString DestPath = TEXT("/Game/Meshes/Generated/");
			Args->TryGetStringField(TEXT("destination_path"), DestPath);

			FString Model = Settings->DefaultImageTo3DModel.IsEmpty() ? TEXT("trellis-2") : Settings->DefaultImageTo3DModel;
			Args->TryGetStringField(TEXT("model"), Model);

			FString Topology = TEXT("triangle");
			Args->TryGetStringField(TEXT("topology"), Topology);

			double PolyCount = 30000;
			Args->TryGetNumberField(TEXT("target_polycount"), PolyCount);

			bool bEnablePBR = false;
			Args->TryGetBoolField(TEXT("enable_pbr"), bEnablePBR);

			double TextureSize = 2048;
			Args->TryGetNumberField(TEXT("texture_size"), TextureSize);

			FString ModelName;
			Args->TryGetStringField(TEXT("model_name"), ModelName);

			FString OutputDir = GetGenerated3DDir();
			FString ScriptPath = FindScriptPath();

			FString FnCall = FString::Printf(TEXT(
				"_fn_image_to_3d(\n"
				"        image_url='%s',\n"
				"        api_key='%s',\n"
				"        output_dir='%s',\n"
				"        model='%s',\n"
				"        model_name=%s,\n"
				"        topology='%s',\n"
				"        target_polycount=%d,\n"
				"        enable_pbr=%s,\n"
				"        texture_size=%d,\n"
				"    )"
			),
				*PyEscape(ImageUrl),
				*PyEscape(ApiKey),
				*PyEscape(OutputDir),
				*Model,
				ModelName.IsEmpty() ? TEXT("None") : *FString::Printf(TEXT("'%s'"), *PyEscape(ModelName)),
				*Topology,
				(int32)PolyCount,
				bEnablePBR ? TEXT("True") : TEXT("False"),
				(int32)TextureSize
			);

			FString PythonCode = BuildPythonCall3D(ScriptPath, FnCall);
			bool bSuccess = PythonPlugin->ExecPythonCommand(*PythonCode);

			if (!bSuccess)
				return FMCPToolResult::Error(TEXT("Python execution failed. Check Output Log for fal.ai API errors."));

			// Find most recently created GLB
			TArray<FString> GlbFiles;
			IFileManager::Get().FindFiles(GlbFiles, *(OutputDir / TEXT("*.glb")), true, false);

			if (GlbFiles.Num() == 0)
				return FMCPToolResult::Error(TEXT("3D generation may have failed - no GLB files found. Check Output Log. Note: image-to-3D can take several minutes."));

			FString LatestFile;
			FDateTime LatestTime = FDateTime::MinValue();
			for (const FString& File : GlbFiles)
			{
				FString FullPath = OutputDir / File;
				FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
				if (ModTime > LatestTime)
				{
					LatestTime = ModTime;
					LatestFile = FullPath;
				}
			}

			if (LatestFile.IsEmpty())
				return FMCPToolResult::Error(TEXT("Could not find generated GLB file"));

			// Import GLB into UE content browser
			UObject* ImportedAsset = ImportGLB(LatestFile, DestPath);

			if (!ImportedAsset)
			{
				return FMCPToolResult::Success(FString::Printf(
					TEXT("3D model generated and saved to: %s (GLB import into UE failed - import manually via Content Browser. "
						 "Ensure the Interchange/glTF plugin is enabled.)"),
					*LatestFile));
			}

			FString ContentPath = ImportedAsset->GetPathName();
			return FMCPToolResult::Success(FString::Printf(
				TEXT("Generated and imported 3D model from image. Asset: %s (model: %s%s). GLB file: %s"),
				*ContentPath, *Model,
				bEnablePBR ? TEXT(", PBR enabled") : TEXT(""),
				*LatestFile));
		});

	// ================================================================
	// list_3d_models - List available text-to-3d and image-to-3d models
	// ================================================================
	MCP_TOOL(Registry, "list_3d_models")
		.ReadOnly()
		.Description(TEXT("List all available fal.ai 3D generation models with their capabilities."))
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

			// Text-to-3D models
			TArray<TSharedPtr<FJsonValue>> TextModels;

			auto AddTextModel = [&](const FString& Key, const FString& Name, const FString& Desc, const FString& Speed, const FString& Quality)
			{
				TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
				M->SetStringField(TEXT("key"), Key);
				M->SetStringField(TEXT("name"), Name);
				M->SetStringField(TEXT("description"), Desc);
				M->SetStringField(TEXT("speed"), Speed);
				M->SetStringField(TEXT("quality"), Quality);
				TextModels.Add(MakeShared<FJsonValueObject>(M));
			};

			AddTextModel(TEXT("meshy-v6"), TEXT("Meshy 6"), TEXT("Best all-round. Textured, supports PBR/rigging/animation."), TEXT("3-5 min"), TEXT("High"));
			AddTextModel(TEXT("meshy-v6-preview"), TEXT("Meshy 6 Preview"), TEXT("Faster untextured preview for iteration."), TEXT("1-2 min"), TEXT("Medium"));
			AddTextModel(TEXT("hunyuan-pro"), TEXT("Hunyuan 3D v3.1 Pro"), TEXT("Highest fidelity, up to 1.5M faces."), TEXT("5-10 min"), TEXT("Very High"));
			AddTextModel(TEXT("hunyuan-rapid"), TEXT("Hunyuan 3D v3.1 Rapid"), TEXT("Faster Hunyuan variant."), TEXT("2-4 min"), TEXT("High"));
			AddTextModel(TEXT("hunyuan-v3"), TEXT("Hunyuan3D v3"), TEXT("Tencent's flagship text-to-3d."), TEXT("3-5 min"), TEXT("High"));

			// Image-to-3D models
			TArray<TSharedPtr<FJsonValue>> ImageModels;

			auto AddImageModel = [&](const FString& Key, const FString& Name, const FString& Desc, const FString& Speed, const FString& Quality)
			{
				TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
				M->SetStringField(TEXT("key"), Key);
				M->SetStringField(TEXT("name"), Name);
				M->SetStringField(TEXT("description"), Desc);
				M->SetStringField(TEXT("speed"), Speed);
				M->SetStringField(TEXT("quality"), Quality);
				ImageModels.Add(MakeShared<FJsonValueObject>(M));
			};

			AddImageModel(TEXT("trellis-2"), TEXT("Trellis 2"), TEXT("Best overall quality. Native 3D generation, strong geometry."), TEXT("2-5 min"), TEXT("Very High"));
			AddImageModel(TEXT("meshy-v6-img"), TEXT("Meshy 6 Image-to-3D"), TEXT("Supports PBR/rigging/animation from image."), TEXT("5-10 min"), TEXT("High"));
			AddImageModel(TEXT("hunyuan-v3-img"), TEXT("Hunyuan3D v3 Image-to-3D"), TEXT("Ultra-high-resolution from image."), TEXT("3-5 min"), TEXT("High"));
			AddImageModel(TEXT("hunyuan-pro-img"), TEXT("Hunyuan 3D v3.1 Pro Image"), TEXT("Highest fidelity image-to-3D."), TEXT("5-10 min"), TEXT("Very High"));
			AddImageModel(TEXT("rodin-v2"), TEXT("Hyper3D Rodin v2"), TEXT("Production-ready assets, clean geometry."), TEXT("3-5 min"), TEXT("High"));
			AddImageModel(TEXT("tripo-v2"), TEXT("Tripo3D v2.5"), TEXT("Good stylized output support."), TEXT("2-4 min"), TEXT("High"));

			Result->SetArrayField(TEXT("text_to_3d_models"), TextModels);
			Result->SetArrayField(TEXT("image_to_3d_models"), ImageModels);
			Result->SetStringField(TEXT("note"), TEXT("All models output GLB format. Import uses UE Interchange/glTF pipeline. Generation times are approximate."));

			FString JsonStr;
			auto Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize(Result.ToSharedRef(), Writer);
			return FMCPToolResult::Success(JsonStr);
		});
}

} // namespace MCP3DModelTools
