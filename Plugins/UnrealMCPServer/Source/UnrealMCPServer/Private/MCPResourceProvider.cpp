// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPResourceProvider.h"

FMCPResourceProvider& FMCPResourceProvider::Get()
{
	static FMCPResourceProvider Instance;
	return Instance;
}

void FMCPResourceProvider::RegisterResource(const FMCPResourceDefinition& Definition, FMCPResourceReader Reader)
{
	FScopeLock Lock(&ResourcesLock);

	FMCPResourceRegistration Reg;
	Reg.Definition = Definition;
	Reg.Reader = Reader;
	Resources.Add(Definition.Uri, Reg);

	UE_LOG(LogUnrealMCP, Log, TEXT("Registered resource: %s"), *Definition.Uri);
}

void FMCPResourceProvider::RegisterResourceCtx(const FMCPResourceDefinition& Definition, FMCPResourceReaderCtx Reader)
{
	FScopeLock Lock(&ResourcesLock);
	FMCPResourceRegistration Reg;
	Reg.Definition = Definition;
	Reg.Definition.bTemplate = Definition.bTemplate || Definition.Uri.Contains(TEXT("{"));
	Reg.ReaderCtx = Reader;
	Resources.Add(Definition.Uri, Reg);
	UE_LOG(LogUnrealMCP, Log, TEXT("Registered %s: %s"), Reg.Definition.bTemplate ? TEXT("resource template") : TEXT("resource"), *Definition.Uri);
}

TArray<FMCPResourceDefinition> FMCPResourceProvider::GetAllTemplates() const
{
	FScopeLock Lock(&ResourcesLock);
	TArray<FMCPResourceDefinition> Result;
	for (const auto& Pair : Resources) if (Pair.Value.Definition.bTemplate) Result.Add(Pair.Value.Definition);
	return Result;
}

void FMCPResourceProvider::UnregisterResource(const FString& Uri)
{
	FScopeLock Lock(&ResourcesLock);
	Resources.Remove(Uri);
}

void FMCPResourceProvider::UnregisterAllResources()
{
	FScopeLock Lock(&ResourcesLock);
	Resources.Empty();
}

TArray<FMCPResourceDefinition> FMCPResourceProvider::GetAllResources() const
{
	FScopeLock Lock(&ResourcesLock);
	TArray<FMCPResourceDefinition> Result;
	for (const auto& Pair : Resources)
	{
		if (!Pair.Value.Definition.bTemplate) Result.Add(Pair.Value.Definition);
	}
	return Result;
}

FMCPResourceContent FMCPResourceProvider::ReadResource(const FString& Uri) const
{
	FMCPRequestContext Internal; // Read scope, principal "internal": owned resources refuse it.
	return ReadResource(Uri, Internal);
}

FMCPResourceContent FMCPResourceProvider::ReadResource(const FString& Uri, const FMCPRequestContext& Context) const
{
	FMCPResourceRegistration RegCopy; bool bFound = false;
	{
		FScopeLock Lock(&ResourcesLock);
		if (const FMCPResourceRegistration* Exact = Resources.Find(Uri)) { RegCopy = *Exact; bFound = true; }
		else
		{
			int32 BestLen = -1;
			for (const auto& Pair : Resources)
			{
				if (!Pair.Value.Definition.bTemplate) continue;
				int32 Brace = INDEX_NONE; Pair.Key.FindChar(TEXT('{'), Brace);
				const FString Prefix = Brace == INDEX_NONE ? Pair.Key : Pair.Key.Left(Brace);
				if (Uri.StartsWith(Prefix) && Uri.Len() > Prefix.Len() && Prefix.Len() > BestLen) { RegCopy = Pair.Value; bFound = true; BestLen = Prefix.Len(); }
			}
		}
	}
	if (!bFound || (!RegCopy.Reader.IsBound() && !RegCopy.ReaderCtx.IsBound()))
		return FMCPResourceContent::NotFound(Uri, TEXT("Resource not found"));

	auto Invoke = [&]() -> FMCPResourceContent
	{
		return RegCopy.ReaderCtx.IsBound() ? RegCopy.ReaderCtx.Execute(Uri, Context) : RegCopy.Reader.Execute(Uri);
	};
	if (IsInGameThread()) return Invoke();
	FMCPResourceContent Result;
	FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool();
	AsyncTask(ENamedThreads::GameThread, [&]()
	{
		Result = Invoke();
		CompletionEvent->Trigger();
	});
	CompletionEvent->Wait();
	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	return Result;
}

bool FMCPResourceProvider::HasResource(const FString& Uri) const
{
	FScopeLock Lock(&ResourcesLock);
	return Resources.Contains(Uri);
}
