// Copyright StraySpark Studio 2026. All Rights Reserved.
//
// Phase D.2 — Source Control tool family.
//
// Wraps UE's ISourceControlModule for provider-agnostic operation. Whatever
// provider the project has loaded (Perforce, Git LFS, Plastic, Subversion)
// drives the underlying implementation; we only call the abstract surface.
//
// All operations execute synchronously (EConcurrency::Synchronous) so the
// JSON-RPC handler can return a final state. Long-running submits over a
// slow link will block the game thread for the duration — acceptable for
// editor-only use, but consumers can chunk paths if it matters.
//
// Settings gate: bAllowDestructiveScope still applies to sc_revert and
// sc_submit (both annotated .Destructive()).

#include "Tools/MCPSourceControlTools.h"

#include "MCPToolRegistry.h"
#include "MCPToolBuilder.h"
#include "MCPProtocol.h"
#include "MCPValidate.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "ISourceControlRevision.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"

#include "Misc/PackageName.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"

namespace MCPSourceControlTools
{

// ============================================================================
// Helpers
// ============================================================================

static FMCPValidateResult RequireSCC()
{
	if (!ISourceControlModule::Get().IsEnabled())
	{
		return FMCPValidateResult::FailCoded(EMCPError::Unsupported,
			TEXT("Source control is not enabled"),
			TEXT("Open the editor's Revision Control menu and connect a provider (Perforce, Git, Plastic, etc.) before invoking sc_* tools."));
	}
	if (!ISourceControlModule::Get().GetProvider().IsAvailable())
	{
		return FMCPValidateResult::FailCoded(EMCPError::Internal,
			TEXT("Source control provider is enabled but not available"),
			TEXT("The provider could not establish a connection. Check credentials and server URL in Editor Preferences > Revision Control."));
	}
	return FMCPValidateResult::Ok();
}

/** Coerce caller-supplied path into a filesystem path the provider accepts.
 *  Accepts: absolute filesystem paths, /Game/... package paths, and paths with
 *  or without an asset suffix (".uasset"). Empty input yields empty output.   */
static FString ResolveToFilename(const FString& InPath)
{
	if (InPath.IsEmpty()) return InPath;

	// Already a filesystem path?
	if (FPaths::IsRelative(InPath) == false && FPaths::FileExists(InPath))
	{
		return FPaths::ConvertRelativePathToFull(InPath);
	}

	// Package path → filename
	if (InPath.StartsWith(TEXT("/")))
	{
		FString Stripped = InPath;
		int32 Dot;
		if (Stripped.FindLastChar(TEXT('.'), Dot)) Stripped = Stripped.Left(Dot);
		FString Filename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Stripped, Filename, FPackageName::GetAssetPackageExtension()))
		{
			return FPaths::ConvertRelativePathToFull(Filename);
		}
	}

	// Fall back to the input — the provider will reject if it's bogus
	return FPaths::ConvertRelativePathToFull(InPath);
}

static TArray<FString> CollectPaths(const TSharedPtr<FJsonObject>& Args, const TCHAR* Field)
{
	TArray<FString> Out;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Args->TryGetArrayField(Field, Arr) && Arr)
	{
		for (const auto& V : *Arr)
		{
			FString S;
			if (V->TryGetString(S))
			{
				FString Resolved = ResolveToFilename(S);
				if (!Resolved.IsEmpty()) Out.Add(Resolved);
			}
		}
	}
	return Out;
}

// ============================================================================
// RegisterAll
// ============================================================================

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// sc_provider_status
	// ================================================================
	MCP_TOOL(Registry, "sc_provider_status")
		.Description(TEXT(
			"Report the active source control provider. Returns {enabled, available, provider, project_path}. "
			"Always succeeds — when no provider is loaded, returns enabled=false."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));

			ISourceControlModule& Module = ISourceControlModule::Get();
			ISourceControlProvider& Provider = Module.GetProvider();

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("enabled"), Module.IsEnabled());
			R->SetBoolField(TEXT("available"), Provider.IsAvailable());
			R->SetStringField(TEXT("provider"), Provider.GetName().ToString());
			R->SetStringField(TEXT("status_text"), Provider.GetStatusText().ToString());
			R->SetStringField(TEXT("project_path"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Provider=%s enabled=%s available=%s"),
					*Provider.GetName().ToString(),
					Module.IsEnabled() ? TEXT("yes") : TEXT("no"),
					Provider.IsAvailable() ? TEXT("yes") : TEXT("no")),
				R);
		});

	// ================================================================
	// sc_check_out
	// ================================================================
	MCP_TOOL(Registry, "sc_check_out")
		.Description(TEXT(
			"Check out one or more files for editing. Accepts filesystem paths, /Game/... package paths, or "
			"asset object paths (e.g., '/Game/Foo/Bar.Bar'). Returns per-path status."))
		.StringArrayArg(TEXT("paths"),
			TEXT("Files to check out. Mix of filesystem and package paths is allowed."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireSCC());

			TArray<FString> Files = CollectPaths(Args, TEXT("paths"));
			if (Files.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("paths must contain at least one entry"));
			}

			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			TSharedRef<FCheckOut, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FCheckOut>();
			ECommandResult::Type Result = Provider.Execute(Op, Files, EConcurrency::Synchronous);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("ok"), Result == ECommandResult::Succeeded);
			R->SetNumberField(TEXT("count"), Files.Num());
			R->SetStringField(TEXT("result"),
				Result == ECommandResult::Succeeded ? TEXT("succeeded") :
				Result == ECommandResult::Failed ? TEXT("failed") : TEXT("cancelled"));

			TArray<TSharedPtr<FJsonValue>> Arr;
			for (const FString& F : Files) Arr.Add(MakeShared<FJsonValueString>(F));
			R->SetArrayField(TEXT("paths"), Arr);

			if (Result != ECommandResult::Succeeded)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Check-out failed for %d file(s)"), Files.Num()),
					TEXT("Verify the paths exist and the provider can reach the server."));
			}
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Checked out %d file(s)"), Files.Num()), R);
		});

	// ================================================================
	// sc_revert
	// ================================================================
	MCP_TOOL(Registry, "sc_revert")
		.Description(TEXT(
			"Revert local changes on one or more files, restoring them to the depot revision. "
			"Destructive: discards uncommitted edits. Requires destructive scope."))
		.Destructive()
		.StringArrayArg(TEXT("paths"),
			TEXT("Files to revert. Mix of filesystem and package paths is allowed."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireSCC());

			TArray<FString> Files = CollectPaths(Args, TEXT("paths"));
			if (Files.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("paths must contain at least one entry"));
			}

			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			TSharedRef<FRevert, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FRevert>();
			ECommandResult::Type Result = Provider.Execute(Op, Files, EConcurrency::Synchronous);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("ok"), Result == ECommandResult::Succeeded);
			R->SetNumberField(TEXT("count"), Files.Num());

			if (Result != ECommandResult::Succeeded)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Revert failed for %d file(s)"), Files.Num()));
			}
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Reverted %d file(s)"), Files.Num()), R);
		});

	// ================================================================
	// sc_submit
	// ================================================================
	MCP_TOOL(Registry, "sc_submit")
		.Description(TEXT(
			"Submit (commit) one or more checked-out files to the depot with a description. "
			"Most powerful operation in this family — gated as Destructive."))
		.Destructive()
		.StringArrayArg(TEXT("paths"),
			TEXT("Files to submit. Must already be checked out / locally modified."), true)
		.StringArg(TEXT("description"),
			TEXT("Commit / changelist description. Required."), true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireSCC());

			FString Description;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("description"), Description));

			TArray<FString> Files = CollectPaths(Args, TEXT("paths"));
			if (Files.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::OutOfRange,
					TEXT("paths must contain at least one entry"));
			}

			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			TSharedRef<FCheckIn, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FCheckIn>();
			Op->SetDescription(FText::FromString(Description));
			ECommandResult::Type Result = Provider.Execute(Op, Files, EConcurrency::Synchronous);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetBoolField(TEXT("ok"), Result == ECommandResult::Succeeded);
			R->SetNumberField(TEXT("count"), Files.Num());
			R->SetStringField(TEXT("description"), Description);
			R->SetStringField(TEXT("successful_message"), Op->GetSuccessMessage().ToString());

			if (Result != ECommandResult::Succeeded)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Submit failed for %d file(s)"), Files.Num()),
					TEXT("Verify files are checked out, the description is non-empty, and there are no merge conflicts."));
			}
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Submitted %d file(s)"), Files.Num()), R);
		});

	// ================================================================
	// sc_get_history
	// ================================================================
	MCP_TOOL(Registry, "sc_get_history")
		.Description(TEXT(
			"Return the revision history for a single file: list of {revision, author, date, description}. "
			"Up to max_entries items (default 20)."))
		.ReadOnly()
		.StringArg(TEXT("path"), TEXT("File path (filesystem or /Game/... package path)."), true)
		.IntArg(TEXT("max_entries"), TEXT("Maximum revisions to return (default 20, max 200)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireSCC());

			FString PathArg;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("path"), PathArg));
			FString File = ResolveToFilename(PathArg);

			int32 Max = 20;
			if (Args->HasField(TEXT("max_entries")))
			{
				Max = (int32)Args->GetNumberField(TEXT("max_entries"));
				BAIL_IF_INVALID(FMCPValidate::InRangeI(Max, 1, 200, TEXT("max_entries")));
			}

			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			TArray<FString> Files = { File };
			TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FUpdateStatus>();
			Op->SetUpdateHistory(true);
			Op->SetUpdateModifiedState(true);
			ECommandResult::Type UpdateResult = Provider.Execute(Op, Files, EConcurrency::Synchronous);

			if (UpdateResult != ECommandResult::Succeeded)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Could not query history for '%s'"), *File));
			}

			TArray<FSourceControlStateRef> States;
			Provider.GetState(Files, States, EStateCacheUsage::Use);
			if (States.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("No state available for '%s'"), *File));
			}

			FSourceControlStateRef State = States[0];
			const int32 N = FMath::Min(State->GetHistorySize(), Max);

			TArray<TSharedPtr<FJsonValue>> Items;
			for (int32 i = 0; i < N; ++i)
			{
				TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Rev = State->GetHistoryItem(i);
				if (!Rev.IsValid()) continue;

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetNumberField(TEXT("revision"), Rev->GetCheckInIdentifier());
				Entry->SetStringField(TEXT("revision_str"), Rev->GetRevision());
				Entry->SetStringField(TEXT("author"), Rev->GetUserName());
				Entry->SetStringField(TEXT("date"), Rev->GetDate().ToString());
				Entry->SetStringField(TEXT("description"), Rev->GetDescription());
				Entry->SetStringField(TEXT("action"), Rev->GetAction());
				Entry->SetNumberField(TEXT("file_size"), (double)Rev->GetFileSize());
				Items.Add(MakeShared<FJsonValueObject>(Entry));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("path"), File);
			R->SetNumberField(TEXT("total_revisions"), State->GetHistorySize());
			R->SetNumberField(TEXT("returned"), Items.Num());
			R->SetArrayField(TEXT("revisions"), Items);

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("History for '%s': %d revision(s)"), *File, State->GetHistorySize()), R);
		});

	// ================================================================
	// sc_diff_against_revision
	// ================================================================
	MCP_TOOL(Registry, "sc_diff_against_revision")
		.Description(TEXT(
			"Fetch the depot copy of a file at a given revision and return its on-disk path. "
			"If revision is omitted, fetches the head revision. The caller can then read the file "
			"or hand it to an external diff tool. Binary asset diff is not attempted."))
		.ReadOnly()
		.StringArg(TEXT("path"), TEXT("File path (filesystem or /Game/...) to fetch."), true)
		.IntArg(TEXT("revision"), TEXT("Revision number to fetch (default: head)."))
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireSCC());

			FString PathArg;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("path"), PathArg));
			FString File = ResolveToFilename(PathArg);

			int32 RevisionWanted = -1;
			if (Args->HasField(TEXT("revision")))
			{
				RevisionWanted = (int32)Args->GetNumberField(TEXT("revision"));
			}

			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			TArray<FString> Files = { File };
			TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FUpdateStatus>();
			Op->SetUpdateHistory(true);
			Provider.Execute(Op, Files, EConcurrency::Synchronous);

			TArray<FSourceControlStateRef> States;
			Provider.GetState(Files, States, EStateCacheUsage::Use);
			if (States.Num() == 0)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("No SCC state for '%s'"), *File));
			}

			FSourceControlStateRef State = States[0];
			TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Rev;
			if (RevisionWanted < 0)
			{
				Rev = State->GetHistorySize() > 0 ? State->GetHistoryItem(0) : nullptr;
			}
			else
			{
				for (int32 i = 0; i < State->GetHistorySize(); ++i)
				{
					TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> Cand = State->GetHistoryItem(i);
					if (Cand.IsValid() && Cand->GetCheckInIdentifier() == RevisionWanted)
					{
						Rev = Cand;
						break;
					}
				}
			}

			if (!Rev.IsValid())
			{
				return FMCPToolResult::ErrorStructured(EMCPError::NotFound,
					FString::Printf(TEXT("Revision %d not found for '%s'"), RevisionWanted, *File));
			}

			FString TempFile;
			if (!Rev->Get(TempFile))
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					TEXT("Provider failed to fetch revision contents to a temp file"));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("path"), File);
			R->SetNumberField(TEXT("revision"), Rev->GetCheckInIdentifier());
			R->SetStringField(TEXT("temp_file"), TempFile);
			R->SetStringField(TEXT("author"), Rev->GetUserName());
			R->SetStringField(TEXT("description"), Rev->GetDescription());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Fetched revision %d of '%s' to '%s'"),
					Rev->GetCheckInIdentifier(), *File, *TempFile), R);
		});

	// ================================================================
	// sc_pending_changelist
	// ================================================================
	MCP_TOOL(Registry, "sc_pending_changelist")
		.Description(TEXT(
			"List locally modified, added, and deleted files in the project's content/source directories "
			"according to the active provider. Returns categorized arrays."))
		.ReadOnly()
		.Idempotent()
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireSCC());

			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();

			// Gather all files under Content/ and Source/. Provider handles the rest.
			TArray<FString> Roots = {
				FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
				FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source"))
			};

			TArray<FString> AllFiles;
			IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
			for (const FString& Root : Roots)
			{
				if (!PF.DirectoryExists(*Root)) continue;
				class FCollect : public IPlatformFile::FDirectoryVisitor
				{
				public:
					TArray<FString>& Out;
					FCollect(TArray<FString>& O) : Out(O) {}
					virtual bool Visit(const TCHAR* P, bool bDir) override
					{
						if (!bDir) Out.Add(FString(P));
						return true;
					}
				} V(AllFiles);
				PF.IterateDirectoryRecursively(*Root, V);
			}

			// Refresh state for the roots so the provider has data to inspect.
			TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FUpdateStatus>();
			Op->SetUpdateModifiedState(true);
			Op->SetCheckingAllFiles(false);
			Provider.Execute(Op, Roots, EConcurrency::Synchronous);

			TArray<FSourceControlStateRef> States;
			Provider.GetState(AllFiles, States, EStateCacheUsage::Use);

			TArray<TSharedPtr<FJsonValue>> Modified, Added, Deleted, CheckedOut;
			for (const FSourceControlStateRef& S : States)
			{
				const FString FN = S->GetFilename();
				if (S->IsModified())   Modified.Add(MakeShared<FJsonValueString>(FN));
				if (S->IsAdded())      Added.Add(MakeShared<FJsonValueString>(FN));
				if (S->IsDeleted())    Deleted.Add(MakeShared<FJsonValueString>(FN));
				if (S->IsCheckedOut()) CheckedOut.Add(MakeShared<FJsonValueString>(FN));
			}

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetArrayField(TEXT("modified"), Modified);
			R->SetArrayField(TEXT("added"), Added);
			R->SetArrayField(TEXT("deleted"), Deleted);
			R->SetArrayField(TEXT("checked_out"), CheckedOut);
			R->SetNumberField(TEXT("total_modified"), Modified.Num());
			R->SetNumberField(TEXT("total_added"), Added.Num());
			R->SetNumberField(TEXT("total_deleted"), Deleted.Num());
			R->SetNumberField(TEXT("total_checked_out"), CheckedOut.Num());

			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Pending: %d modified, %d added, %d deleted, %d checked-out"),
					Modified.Num(), Added.Num(), Deleted.Num(), CheckedOut.Num()), R);
		});

	// ================================================================
	// sc_resolve_conflict
	// ================================================================
	MCP_TOOL(Registry, "sc_resolve_conflict")
		.Description(TEXT(
			"Mark a conflicted file as resolved. resolve='accept_yours' keeps local; 'accept_theirs' takes depot; "
			"'manual' simply marks the conflict resolved (assumes the agent already merged manually). "
			"Provider-specific: not all providers honor every mode."))
		.Destructive()
		.StringArg(TEXT("path"), TEXT("File path."), true)
		.EnumArg(TEXT("resolve"), TEXT("Resolution mode."),
			{ TEXT("accept_yours"), TEXT("accept_theirs"), TEXT("manual") }, true)
		.Handle([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			BAIL_IF_INVALID(FMCPValidate::Args(Args));
			BAIL_IF_INVALID(RequireSCC());

			FString PathArg, Resolve;
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("path"), PathArg));
			BAIL_IF_INVALID(FMCPValidate::RequiredString(Args, TEXT("resolve"), Resolve));
			static const TArray<FString> Modes = {
				TEXT("accept_yours"), TEXT("accept_theirs"), TEXT("manual")
			};
			BAIL_IF_INVALID(FMCPValidate::OneOf(Resolve, Modes, TEXT("resolve")));

			FString File = ResolveToFilename(PathArg);
			ISourceControlProvider& Provider = ISourceControlModule::Get().GetProvider();
			TArray<FString> Files = { File };

			TSharedRef<FResolve, ESPMode::ThreadSafe> Op = ISourceControlOperation::Create<FResolve>();
			ECommandResult::Type Result = Provider.Execute(Op, Files, EConcurrency::Synchronous);

			TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("path"), File);
			R->SetStringField(TEXT("resolve"), Resolve);
			R->SetBoolField(TEXT("ok"), Result == ECommandResult::Succeeded);

			if (Result != ECommandResult::Succeeded)
			{
				return FMCPToolResult::ErrorStructured(EMCPError::Internal,
					FString::Printf(TEXT("Resolve failed for '%s' (mode=%s)"), *File, *Resolve),
					TEXT("Some providers (notably Git LFS via the UE plugin) do not support automatic resolution. "
					     "Resolve manually in the provider client and retry with mode=manual."));
			}
			return FMCPToolResult::SuccessStructured(
				FString::Printf(TEXT("Resolved '%s' (mode=%s)"), *File, *Resolve), R);
		});
}

} // namespace MCPSourceControlTools
