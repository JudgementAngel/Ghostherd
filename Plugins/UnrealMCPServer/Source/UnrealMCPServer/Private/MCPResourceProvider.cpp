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
		Result.Add(Pair.Value.Definition);
	}
	return Result;
}

FMCPResourceContent FMCPResourceProvider::ReadResource(const FString& Uri) const
{
	FScopeLock Lock(&ResourcesLock);

	const FMCPResourceRegistration* Reg = Resources.Find(Uri);
	if (!Reg || !Reg->Reader.IsBound())
	{
		FMCPResourceContent Empty;
		Empty.Uri = Uri;
		Empty.Text = TEXT("Resource not found");
		return Empty;
	}

	// Execute reader on game thread
	FMCPResourceContent Result;
	if (IsInGameThread())
	{
		Result = Reg->Reader.Execute(Uri);
	}
	else
	{
		FMCPResourceReader ReaderCopy = Reg->Reader;
		FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool();
		AsyncTask(ENamedThreads::GameThread, [&]()
		{
			Result = ReaderCopy.Execute(Uri);
			CompletionEvent->Trigger();
		});
		CompletionEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
	}

	return Result;
}

bool FMCPResourceProvider::HasResource(const FString& Uri) const
{
	FScopeLock Lock(&ResourcesLock);
	return Resources.Contains(Uri);
}
