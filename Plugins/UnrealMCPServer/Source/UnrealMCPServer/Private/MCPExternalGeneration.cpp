// Copyright StraySpark Studio 2026. All Rights Reserved.
// v5 increment 20: external generation boundary (V5-30).
//
// The legacy generate_* tools call a Python script that blocks the game thread while it polls
// the provider. This service keeps the editor responsive: submission, status polling and the
// download are asynchronous HTTP requests driven from the operation ticker; the only synchronous
// editor work is the final import, which runs on the game thread once the file is on disk and
// validated. The API key is read from settings when a request is built and is never placed in
// any result, event, log line or script text.

#include "MCPExternalGeneration.h"
#include "MCPScenarios.h"
#include "MCPToolBuilder.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"
#include "MCPSettings.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetImportTask.h"

namespace MCPExternalGeneration
{
namespace
{
const TCHAR* FalQueueBase = TEXT("https://queue.fal.run");
const TMap<FString, FString>& ModelTable()
{
	static const TMap<FString, FString> M = {
		// text_to_image
		{TEXT("flux-2-flash"), TEXT("fal-ai/flux-2/flash")}, {TEXT("nano-banana-2"), TEXT("fal-ai/nano-banana-2")}, {TEXT("nano-banana"), TEXT("fal-ai/nano-banana")}, {TEXT("flux-dev"), TEXT("fal-ai/flux/dev")}, {TEXT("flux-pro"), TEXT("fal-ai/flux-pro/v1.1")},
		// remove_background
		{TEXT("birefnet-v2"), TEXT("fal-ai/birefnet/v2")},
		// text_to_3d
		{TEXT("meshy-v6"), TEXT("fal-ai/meshy/v6/text-to-3d")}, {TEXT("meshy-v6-preview"), TEXT("fal-ai/meshy/v6-preview/text-to-3d")}, {TEXT("hunyuan-pro"), TEXT("fal-ai/hunyuan-3d/v3.1/pro/text-to-3d")}, {TEXT("hunyuan-rapid"), TEXT("fal-ai/hunyuan-3d/v3.1/rapid/text-to-3d")}, {TEXT("hunyuan-v3"), TEXT("fal-ai/hunyuan3d-v3/text-to-3d")},
		// image_to_3d
		{TEXT("trellis-2"), TEXT("fal-ai/trellis-2")}, {TEXT("meshy-v6-img"), TEXT("fal-ai/meshy/v6/image-to-3d")}, {TEXT("hunyuan-v3-img"), TEXT("fal-ai/hunyuan3d-v3/image-to-3d")}, {TEXT("hunyuan-pro-img"), TEXT("fal-ai/hunyuan-3d/v3.1/pro/image-to-3d")}, {TEXT("rodin-v2"), TEXT("fal-ai/hyper3d/rodin-v2")}, {TEXT("tripo-v2"), TEXT("fal-ai/tripo3d/tripo-v2.5/image-to-3d")} };
	return M;
}
const TArray<FString>& AllowedExtensions() { static const TArray<FString> E = { TEXT("png"), TEXT("jpg"), TEXT("jpeg"), TEXT("webp"), TEXT("glb"), TEXT("gltf"), TEXT("fbx"), TEXT("obj") }; return E; }

struct FJob
{
	FString Provider, Kind, ModelKey, ModelId, DestPath, AssetName, OutDir, OutFile, Extension, ProviderJobId, Stage = TEXT("submitting"), Error, ProviderCancellation = TEXT("not_requested"), ImportedAsset, DownloadUrl;
	bool bAutoImport = true, bRequestInFlight = false, bCancelSent = false, bFinished = false;
	double NextPollAt = 0.0, MockReadyAt = -1.0, MockDelay = 0.5, PollInterval = 3.0;
	int64 MaxBytes = 100 * 1024 * 1024, DownloadedBytes = 0;
	int32 PollCount = 0, HttpErrors = 0;
	TSharedPtr<FJsonObject> Payload, ProviderResult;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Active;
	TArray<FString> Notes;
};

FString ApiKey() { const UMCPSettings* S = UMCPSettings::Get(); return S ? S->FalAIApiKey : FString(); }

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(const FString& Verb, const FString& Url, bool bAuth)
{
	auto R = FHttpModule::Get().CreateRequest();
	R->SetVerb(Verb); R->SetURL(Url); R->SetTimeout(60.0f);
	R->SetHeader(TEXT("Accept"), TEXT("application/json"));
	if (bAuth) R->SetHeader(TEXT("Authorization"), TEXT("Key ") + ApiKey()); // credential stays inside the request object
	return R;
}

TSharedPtr<FJsonObject> ParseJson(const FHttpResponsePtr& Resp)
{
	if (!Resp.IsValid()) return nullptr;
	TSharedPtr<FJsonObject> O; const auto Reader = TJsonReaderFactory<>::Create(Resp->GetContentAsString());
	return FJsonSerializer::Deserialize(Reader, O) ? O : nullptr;
}

FString ExtensionOfUrl(const FString& Url)
{
	FString Path = Url; int32 Q; if (Path.FindChar(TEXT('?'), Q)) Path = Path.Left(Q);
	return FPaths::GetExtension(Path).ToLower();
}

/** Depth-first search for the first "url" string with an allowed extension; preferred keys first. */
FString FindFileUrl(const TSharedPtr<FJsonObject>& O, int32 Depth = 0)
{
	if (!O.IsValid() || Depth > 6) return FString();
	static const TCHAR* Preferred[] = { TEXT("model_glb"), TEXT("model_mesh"), TEXT("model_urls"), TEXT("images"), TEXT("image"), TEXT("output") };
	auto Visit = [&](const TSharedPtr<FJsonValue>& V) -> FString
	{
		if (!V.IsValid()) return FString();
		if (V->Type == EJson::Object) return FindFileUrl(V->AsObject(), Depth + 1);
		if (V->Type == EJson::Array) { for (const auto& E : V->AsArray()) { const FString F = E->Type == EJson::Object ? FindFileUrl(E->AsObject(), Depth + 1) : (E->Type == EJson::String && AllowedExtensions().Contains(ExtensionOfUrl(E->AsString())) ? E->AsString() : FString()); if (!F.IsEmpty()) return F; } }
		return FString();
	};
	FString U;
	if (O->TryGetStringField(TEXT("url"), U) && U.StartsWith(TEXT("http")) && AllowedExtensions().Contains(ExtensionOfUrl(U))) return U;
	for (const TCHAR* K : Preferred) if (O->HasField(K)) { const FString F = Visit(O->TryGetField(K)); if (!F.IsEmpty()) return F; }
	for (const auto& P : O->Values) { const FString F = Visit(P.Value); if (!F.IsEmpty()) return F; }
	return FString();
}

bool WriteMockOutput(FJob& J)
{
	if (J.Kind == TEXT("text_to_image") || J.Kind == TEXT("remove_background"))
	{
		IImageWrapperModule& M = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> W = M.CreateImageWrapper(EImageFormat::PNG);
		TArray<FColor> Px; Px.SetNum(64); for (int32 i = 0; i < 64; ++i) Px[i] = FColor(i * 4, 255 - i * 4, 128, 255);
		if (!W.IsValid() || !W->SetRaw(Px.GetData(), Px.Num() * sizeof(FColor), 8, 8, ERGBFormat::BGRA, 8)) return false;
		const TArray64<uint8> Bytes = W->GetCompressed(0);
		J.Extension = TEXT("png"); J.OutFile = J.OutDir / (J.AssetName + TEXT(".png")); J.DownloadedBytes = Bytes.Num();
		return FFileHelper::SaveArrayToFile(TArrayView<const uint8>(Bytes.GetData(), (int32)Bytes.Num()), *J.OutFile);
	}
	// 3D kinds: a unit cube in Wavefront OBJ.
	const FString Obj = TEXT("# mock cube\nv -50 -50 -50\nv 50 -50 -50\nv 50 50 -50\nv -50 50 -50\nv -50 -50 50\nv 50 -50 50\nv 50 50 50\nv -50 50 50\nf 1 2 3 4\nf 5 8 7 6\nf 1 5 6 2\nf 2 6 7 3\nf 3 7 8 4\nf 5 1 4 8\n");
	J.Extension = TEXT("obj"); J.OutFile = J.OutDir / (J.AssetName + TEXT(".obj")); J.DownloadedBytes = Obj.Len();
	return FFileHelper::SaveStringToFile(Obj, *J.OutFile);
}

bool ImportOutput(FJob& J)
{
	FAssetToolsModule& ATM = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	UAssetImportTask* Task = NewObject<UAssetImportTask>();
	Task->Filename = J.OutFile; Task->DestinationPath = J.DestPath; Task->DestinationName = J.AssetName;
	Task->bAutomated = true; Task->bReplaceExisting = true; Task->bSave = false; Task->bAsync = false;
	TArray<UAssetImportTask*> Tasks; Tasks.Add(Task);
	ATM.Get().ImportAssetTasks(Tasks);
	for (UObject* O : Task->GetObjects()) if (O) { J.ImportedAsset = O->GetPathName(); return true; }
	J.Error = TEXT("Import produced no asset (see the editor log for the factory's message)");
	return false;
}

TSharedPtr<FJsonObject> ResultJson(const FJob& J)
{
	auto R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("provider"), J.Provider); R->SetStringField(TEXT("kind"), J.Kind); R->SetStringField(TEXT("model"), J.ModelKey); R->SetStringField(TEXT("model_id"), J.ModelId);
	R->SetStringField(TEXT("stage"), J.Stage); R->SetStringField(TEXT("provider_job_id"), J.ProviderJobId); R->SetStringField(TEXT("provider_cancellation"), J.ProviderCancellation);
	R->SetNumberField(TEXT("poll_count"), J.PollCount);
	if (!J.OutFile.IsEmpty()) { R->SetStringField(TEXT("file_path"), J.OutFile); R->SetNumberField(TEXT("bytes"), (double)J.DownloadedBytes); R->SetStringField(TEXT("extension"), J.Extension); }
	if (!J.DownloadUrl.IsEmpty()) R->SetStringField(TEXT("download_url"), J.DownloadUrl);
	if (!J.ImportedAsset.IsEmpty()) R->SetStringField(TEXT("imported_asset"), J.ImportedAsset);
	R->SetBoolField(TEXT("auto_import"), J.bAutoImport);
	if (!J.Error.IsEmpty()) R->SetStringField(TEXT("error"), J.Error);
	TArray<TSharedPtr<FJsonValue>> N; for (const FString& S : J.Notes) N.Add(MakeShared<FJsonValueString>(S)); R->SetArrayField(TEXT("notes"), N);
	return R;
}

void StartSubmit(const TSharedPtr<FJob>& Job)
{
	auto R = MakeRequest(TEXT("POST"), FString::Printf(TEXT("%s/%s"), FalQueueBase, *Job->ModelId), true);
	R->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	R->SetContentAsString(JsonToString(Job->Payload));
	Job->bRequestInFlight = true; Job->Active = R;
	R->OnProcessRequestComplete().BindLambda([Job](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		Job->bRequestInFlight = false; Job->Active.Reset();
		if (Job->bFinished) return;
		const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
		const TSharedPtr<FJsonObject> O = ParseJson(Resp);
		if (!bOk || Code < 200 || Code >= 300 || !O.IsValid()) { Job->Error = FString::Printf(TEXT("Submission failed (HTTP %d): %s"), Code, Resp.IsValid() ? *Resp->GetContentAsString().Left(300) : TEXT("no response")); Job->Stage = TEXT("failed"); return; }
		FString Id; if (!O->TryGetStringField(TEXT("request_id"), Id)) { Job->Error = TEXT("Provider response has no request_id"); Job->Stage = TEXT("failed"); return; }
		Job->ProviderJobId = Id; Job->Stage = TEXT("queued"); Job->NextPollAt = 0.0;
	});
	R->ProcessRequest();
}

void StartPoll(const TSharedPtr<FJob>& Job)
{
	auto R = MakeRequest(TEXT("GET"), FString::Printf(TEXT("%s/%s/requests/%s/status"), FalQueueBase, *Job->ModelId, *Job->ProviderJobId), true);
	Job->bRequestInFlight = true; Job->Active = R; ++Job->PollCount;
	R->OnProcessRequestComplete().BindLambda([Job](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		Job->bRequestInFlight = false; Job->Active.Reset();
		if (Job->bFinished) return;
		const TSharedPtr<FJsonObject> O = ParseJson(Resp);
		if (!bOk || !O.IsValid()) { if (++Job->HttpErrors > 5) { Job->Error = TEXT("Status polling failed repeatedly"); Job->Stage = TEXT("failed"); } return; }
		const FString S = O->GetStringField(TEXT("status"));
		if (S == TEXT("COMPLETED")) Job->Stage = TEXT("fetching_result");
		else if (S == TEXT("FAILED") || S == TEXT("CANCELLED")) { Job->Error = FString::Printf(TEXT("Provider reported %s"), *S); Job->Stage = S == TEXT("CANCELLED") ? TEXT("cancelled_by_provider") : TEXT("failed"); }
		else Job->Stage = S == TEXT("IN_PROGRESS") ? TEXT("in_progress") : TEXT("queued");
	});
	R->ProcessRequest();
}

void StartFetchResult(const TSharedPtr<FJob>& Job)
{
	auto R = MakeRequest(TEXT("GET"), FString::Printf(TEXT("%s/%s/requests/%s"), FalQueueBase, *Job->ModelId, *Job->ProviderJobId), true);
	Job->bRequestInFlight = true; Job->Active = R;
	R->OnProcessRequestComplete().BindLambda([Job](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		Job->bRequestInFlight = false; Job->Active.Reset();
		if (Job->bFinished) return;
		const TSharedPtr<FJsonObject> O = ParseJson(Resp);
		if (!bOk || !O.IsValid()) { Job->Error = TEXT("Result fetch failed"); Job->Stage = TEXT("failed"); return; }
		Job->ProviderResult = O;
		Job->DownloadUrl = FindFileUrl(O);
		if (Job->DownloadUrl.IsEmpty()) { Job->Error = TEXT("Provider result contains no downloadable file with an allowed extension"); Job->Stage = TEXT("failed"); return; }
		Job->Extension = ExtensionOfUrl(Job->DownloadUrl);
		Job->Stage = TEXT("downloading");
	});
	R->ProcessRequest();
}

void StartDownload(const TSharedPtr<FJob>& Job)
{
	auto R = MakeRequest(TEXT("GET"), Job->DownloadUrl, false); // asset CDN: no credential
	R->SetTimeout(300.0f);
	Job->bRequestInFlight = true; Job->Active = R;
	R->OnProcessRequestComplete().BindLambda([Job](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		Job->bRequestInFlight = false; Job->Active.Reset();
		if (Job->bFinished) return;
		if (!bOk || !Resp.IsValid() || Resp->GetResponseCode() != 200) { Job->Error = TEXT("Download failed"); Job->Stage = TEXT("failed"); return; }
		const TArray<uint8>& Bytes = Resp->GetContent();
		if ((int64)Bytes.Num() > Job->MaxBytes) { Job->Error = FString::Printf(TEXT("Download of %d bytes exceeds max_bytes %lld"), Bytes.Num(), Job->MaxBytes); Job->Stage = TEXT("failed"); return; }
		const FString CT = Resp->GetHeader(TEXT("Content-Type")).ToLower();
		if (!CT.IsEmpty() && !(CT.StartsWith(TEXT("image/")) || CT.StartsWith(TEXT("model/")) || CT.Contains(TEXT("octet-stream")) || CT.Contains(TEXT("gltf")))) { Job->Error = TEXT("Unexpected content type: ") + CT; Job->Stage = TEXT("failed"); return; }
		Job->OutFile = Job->OutDir / (Job->AssetName + TEXT(".") + Job->Extension);
		if (!FFileHelper::SaveArrayToFile(Bytes, *Job->OutFile)) { Job->Error = TEXT("Could not write ") + Job->OutFile; Job->Stage = TEXT("failed"); return; }
		Job->DownloadedBytes = Bytes.Num(); Job->Stage = TEXT("downloaded");
	});
	R->ProcessRequest();
}

void SendProviderCancel(const TSharedPtr<FJob>& Job)
{
	if (Job->bCancelSent) return;
	Job->bCancelSent = true;
	if (Job->Provider != TEXT("fal") || Job->ProviderJobId.IsEmpty()) { Job->ProviderCancellation = Job->Provider == TEXT("mock") ? TEXT("not_applicable") : TEXT("not_submitted"); return; }
	Job->ProviderCancellation = TEXT("requested");
	auto R = MakeRequest(TEXT("PUT"), FString::Printf(TEXT("%s/%s/requests/%s/cancel"), FalQueueBase, *Job->ModelId, *Job->ProviderJobId), true);
	R->OnProcessRequestComplete().BindLambda([Job](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
	{
		const int32 Code = Resp.IsValid() ? Resp->GetResponseCode() : 0;
		Job->ProviderCancellation = (bOk && Code >= 200 && Code < 300) ? TEXT("accepted") : FString::Printf(TEXT("rejected_http_%d"), Code);
	});
	R->ProcessRequest();
}

/** One ticker step. Returns when the job has finished (Finish called) or needs more time. */
void Drive(const TSharedPtr<FJob>& Job, FMCPOperationTick& T)
{
	if (Job->bFinished) return;
	auto Done = [&](const FString& State, const FString& Err) { Job->bFinished = true; if (Job->Active.IsValid()) { Job->Active->CancelRequest(); Job->Active.Reset(); } T.Finish(State, Err, ResultJson(*Job)); };
	if (T.bCancelRequested)
	{
		SendProviderCancel(Job);
		Job->Notes.Add(Job->Provider == TEXT("fal") ? TEXT("Local polling and download stopped immediately; the provider may still bill the job unless its cancel endpoint accepted the request (see provider_cancellation).") : TEXT("Mock job cancelled; nothing was produced."));
		Job->Stage = TEXT("cancelled");
		Done(TEXT("cancelled"), TEXT("Cancelled by owner"));
		return;
	}
	if (Job->Stage == TEXT("failed") || Job->Stage == TEXT("cancelled_by_provider")) { Done(TEXT("failed"), Job->Error); return; }
	if (Job->Provider == TEXT("mock"))
	{
		if (Job->MockReadyAt < 0.0) { Job->MockReadyAt = T.NowSeconds + Job->MockDelay; Job->ProviderJobId = TEXT("mock-") + FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(12); Job->Stage = TEXT("in_progress"); T.Event(TEXT("submitted"), TEXT("mock job ") + Job->ProviderJobId); }
		if (T.NowSeconds < Job->MockReadyAt) { T.Progress(FMath::Clamp(1.0 - (Job->MockReadyAt - T.NowSeconds) / FMath::Max(Job->MockDelay, 0.001), 0.0, 0.9), TEXT("simulated provider delay")); return; }
		if (Job->OutFile.IsEmpty()) { if (!WriteMockOutput(*Job)) { Job->Error = TEXT("Could not write mock output"); Done(TEXT("failed"), Job->Error); return; } Job->Stage = TEXT("downloaded"); T.Event(TEXT("downloaded"), Job->OutFile); }
	}
	else
	{
		if (Job->bRequestInFlight) return;
		if (Job->Stage == TEXT("submitting")) { T.Progress(0.05, TEXT("submitting to provider")); StartSubmit(Job); return; }
		if (Job->Stage == TEXT("queued") || Job->Stage == TEXT("in_progress"))
		{
			if (Job->PollCount == 0) T.Event(TEXT("submitted"), TEXT("provider job ") + Job->ProviderJobId);
			if (T.NowSeconds < Job->NextPollAt) return;
			Job->NextPollAt = T.NowSeconds + Job->PollInterval;
			T.Progress(Job->Stage == TEXT("in_progress") ? 0.4 : 0.2, Job->Stage);
			StartPoll(Job); return;
		}
		if (Job->Stage == TEXT("fetching_result")) { T.Progress(0.6, TEXT("fetching result")); StartFetchResult(Job); return; }
		if (Job->Stage == TEXT("downloading")) { T.Progress(0.7, TEXT("downloading ") + Job->Extension); StartDownload(Job); return; }
		if (Job->Stage != TEXT("downloaded")) return;
	}
	// downloaded
	if (!Job->bAutoImport) { Job->Stage = TEXT("done"); T.Progress(1.0, TEXT("file ready; import skipped")); Done(TEXT("succeeded"), FString()); return; }
	Job->Stage = TEXT("importing"); T.Progress(0.9, TEXT("importing on the game thread"));
	if (!ImportOutput(*Job)) { Job->Stage = TEXT("failed"); Done(TEXT("failed"), Job->Error); return; }
	Job->Stage = TEXT("done"); T.Progress(1.0, TEXT("imported ") + Job->ImportedAsset); T.Event(TEXT("imported"), Job->ImportedAsset);
	Done(TEXT("succeeded"), FString());
}
} // namespace

void RegisterAll(FMCPToolRegistry& Registry)
{
	MCP_TOOL(Registry, "submit_generation_job")
		.Destructive() // network egress, billable provider call, asset creation
		.Description(TEXT("Submit an external generation job without blocking the editor. Returns an owned operation (poll get_editor_operation, cancel with cancel_editor_operation). Providers: fal (queue API; requires the fal.ai key in Project Settings, which is sent only as a request header and never returned) and mock (no network, produces a small PNG or OBJ after a simulated delay; use it for tests and soak runs). Kinds: text_to_image (prompt), text_to_3d (prompt), image_to_3d (image_url), remove_background (image_url). model is a short key (list_3d_models / generate_ui_image docs); params are merged into the provider payload. The file is downloaded to Saved/MCPV5/Generated/<operation>/ (allowed: png jpg jpeg webp glb gltf fbx obj; max_bytes cap) and, with auto_import (default true), imported into destination_path on the game thread. Cancellation stops local work immediately and reports provider_cancellation honestly (accepted, rejected, not_applicable). The operation result carries provider_job_id, file_path, bytes, imported_asset and notes."))
		.EnumArg(TEXT("provider"), TEXT("fal or mock"), { TEXT("fal"), TEXT("mock") }, true)
		.EnumArg(TEXT("kind"), TEXT("Generation kind"), { TEXT("text_to_image"), TEXT("text_to_3d"), TEXT("image_to_3d"), TEXT("remove_background") }, true)
		.StringArg(TEXT("model"), TEXT("Model key (default per kind: flux-2-flash, meshy-v6, trellis-2, birefnet-v2)"))
		.StringArg(TEXT("prompt"), TEXT("Prompt for text_* kinds"))
		.StringArg(TEXT("image_url"), TEXT("Public image URL for image_to_3d / remove_background"))
		.ObjectArg(TEXT("params"), TEXT("Extra provider payload fields (merged; for mock: delay_seconds)"), StringToJson(TEXT(R"({"type":"object"})")))
		.StringArg(TEXT("destination_path"), TEXT("Content folder for the imported asset (default /Game/Generated)"))
		.StringArg(TEXT("asset_name"), TEXT("Asset and file base name (default generated)"))
		.BoolArg(TEXT("auto_import"), TEXT("Import the downloaded file (default true); false leaves the file on disk"))
		.IntArg(TEXT("max_bytes"), TEXT("Download size cap in bytes (default 104857600)"))
		.IntArg(TEXT("deadline_seconds"), TEXT("Operation deadline 1..600 (default 600)"))
		.HandleCtx([](const TSharedPtr<FJsonObject>& Args, const FMCPRequestContext& Context) -> FMCPToolResult
		{
			if (!IsInGameThread()) return FMCPToolResult::ErrorStructured(EMCPError::RequiresGameThread, TEXT("Generation jobs require the game thread"));
			auto Job = MakeShared<FJob>();
			Job->Provider = Args->GetStringField(TEXT("provider")); Job->Kind = Args->GetStringField(TEXT("kind"));
			Args->TryGetStringField(TEXT("model"), Job->ModelKey);
			if (Job->ModelKey.IsEmpty()) Job->ModelKey = Job->Kind == TEXT("text_to_image") ? TEXT("flux-2-flash") : Job->Kind == TEXT("text_to_3d") ? TEXT("meshy-v6") : Job->Kind == TEXT("image_to_3d") ? TEXT("trellis-2") : TEXT("birefnet-v2");
			const FString* ModelId = ModelTable().Find(Job->ModelKey);
			if (!ModelId) { TArray<FString> Keys; ModelTable().GetKeys(Keys); return FMCPToolResult::ErrorStructured(EMCPError::NotFound, TEXT("Unknown model key: ") + Job->ModelKey, TEXT("Use one of the known keys."), Keys); }
			Job->ModelId = *ModelId;
			FString Prompt, ImageUrl; Args->TryGetStringField(TEXT("prompt"), Prompt); Args->TryGetStringField(TEXT("image_url"), ImageUrl);
			const bool bNeedsPrompt = Job->Kind.StartsWith(TEXT("text_"));
			if (bNeedsPrompt && Prompt.IsEmpty()) return FMCPToolResult::Error(TEXT("prompt is required for ") + Job->Kind);
			if (!bNeedsPrompt && (ImageUrl.IsEmpty() || !ImageUrl.StartsWith(TEXT("http")))) return FMCPToolResult::Error(TEXT("image_url (http/https) is required for ") + Job->Kind);
			Job->Payload = MakeShared<FJsonObject>();
			if (bNeedsPrompt) Job->Payload->SetStringField(TEXT("prompt"), Prompt); else Job->Payload->SetStringField(TEXT("image_url"), ImageUrl);
			if (Args->HasTypedField<EJson::Object>(TEXT("params")))
			{
				const TSharedPtr<FJsonObject> P = Args->GetObjectField(TEXT("params"));
				for (const auto& KV : P->Values) Job->Payload->SetField(KV.Key, KV.Value);
				double Delay; if (P->TryGetNumberField(TEXT("delay_seconds"), Delay)) Job->MockDelay = FMath::Clamp(Delay, 0.0, 60.0);
			}
			Job->DestPath = Args->HasField(TEXT("destination_path")) ? Args->GetStringField(TEXT("destination_path")) : TEXT("/Game/Generated");
			Job->DestPath.RemoveFromEnd(TEXT("/"));
			if (!Job->DestPath.StartsWith(TEXT("/Game/")) || Job->DestPath.Contains(TEXT("..")) || !FPackageName::IsValidLongPackageName(Job->DestPath))
				return FMCPToolResult::ErrorStructured(EMCPError::InvalidPath, TEXT("destination_path must be a valid /Game/... folder without traversal"));
			Args->TryGetStringField(TEXT("asset_name"), Job->AssetName);
			if (Job->AssetName.IsEmpty()) Job->AssetName = FString::Printf(TEXT("Gen_%s_%s"), *Job->Kind, *FGuid::NewGuid().ToString(EGuidFormats::DigitsLower).Left(8));
			for (TCHAR& C : Job->AssetName) if (!FChar::IsAlnum(C) && C != TEXT('_')) C = TEXT('_');
			Job->bAutoImport = !Args->HasField(TEXT("auto_import")) || Args->GetBoolField(TEXT("auto_import"));
			if (Args->HasField(TEXT("max_bytes"))) Job->MaxBytes = FMath::Clamp((int64)Args->GetNumberField(TEXT("max_bytes")), (int64)1024, (int64)1024 * 1024 * 1024);
			const double Deadline = Args->HasField(TEXT("deadline_seconds")) ? Args->GetNumberField(TEXT("deadline_seconds")) : 600.0;
			if (Job->Provider == TEXT("fal"))
			{
				if (ApiKey().IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, TEXT("No fal.ai API key configured"), TEXT("Set it in Project Settings > Plugins > Unreal MCP Server. It is never returned by any tool."));
				if (!FModuleManager::Get().IsModuleLoaded(TEXT("HTTP"))) FModuleManager::Get().LoadModule(TEXT("HTTP"));
			}
			auto Details = MakeShared<FJsonObject>();
			Details->SetStringField(TEXT("provider"), Job->Provider); Details->SetStringField(TEXT("kind"), Job->Kind); Details->SetStringField(TEXT("model"), Job->ModelKey); Details->SetStringField(TEXT("model_id"), Job->ModelId);
			Details->SetStringField(TEXT("destination_path"), Job->DestPath); Details->SetStringField(TEXT("asset_name"), Job->AssetName); Details->SetBoolField(TEXT("auto_import"), Job->bAutoImport);
			FString Err;
			TWeakPtr<FJob> Weak = Job;
			const FString Id = MCPScenarios::StartDrivenOperation(Context, TEXT("external_generation"), Job->Kind + TEXT(" via ") + Job->Provider, Deadline,
				[Job](FMCPOperationTick& T) { Drive(Job, T); },
				[Job](const FString& Reason) { Job->bFinished = true; if (Job->Active.IsValid()) { Job->Active->CancelRequest(); Job->Active.Reset(); } SendProviderCancel(Job); },
				Details, Err);
			if (Id.IsEmpty()) return FMCPToolResult::ErrorStructured(EMCPError::Unsupported, Err);
			Job->OutDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MCPV5/Generated") / Id);
			IFileManager::Get().MakeDirectory(*Job->OutDir, true);
			const TSharedPtr<FJsonObject> Op = MCPScenarios::DescribeOperation(Id, Context, false);
			return FMCPToolResult::SuccessStructured(FString::Printf(TEXT("Generation job queued as %s (%s, %s). Poll get_editor_operation."), *Id, *Job->Provider, *Job->Kind), Op);
		});
}
} // namespace MCPExternalGeneration
