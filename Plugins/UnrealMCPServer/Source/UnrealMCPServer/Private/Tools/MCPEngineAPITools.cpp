#include "Tools/MCPEngineAPITools.h"
#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"

namespace MCPEngineAPITools
{

// ================================================================
// Lazy-cached header file lists
// ================================================================

static TArray<FString> CachedPublicHeaders;   // Classes/ + Public/ only
static TArray<FString> CachedAllHeaders;      // All .h files under Engine/Source
static bool bPublicCacheBuilt = false;
static bool bAllCacheBuilt = false;
static FString CachedEngineSourceDir;

static const FString& GetEngineSourceDir()
{
	if (CachedEngineSourceDir.IsEmpty())
	{
		CachedEngineSourceDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Source"));
		FPaths::NormalizeDirectoryName(CachedEngineSourceDir);
	}
	return CachedEngineSourceDir;
}

static void EnsurePublicCache()
{
	if (bPublicCacheBuilt) return;

	const FString& SourceDir = GetEngineSourceDir();
	TArray<FString> AllHeaders;
	IFileManager::Get().FindFilesRecursive(AllHeaders, *SourceDir, TEXT("*.h"), true, false);

	CachedPublicHeaders.Reset();
	for (const FString& Path : AllHeaders)
	{
		if (Path.Contains(TEXT("/Public/")) || Path.Contains(TEXT("/Classes/")) ||
		    Path.Contains(TEXT("\\Public\\")) || Path.Contains(TEXT("\\Classes\\")))
		{
			CachedPublicHeaders.Add(Path);
		}
	}

	bPublicCacheBuilt = true;
}

static void EnsureAllCache()
{
	if (bAllCacheBuilt) return;

	const FString& SourceDir = GetEngineSourceDir();
	IFileManager::Get().FindFilesRecursive(CachedAllHeaders, *SourceDir, TEXT("*.h"), true, false);
	bAllCacheBuilt = true;

	// Also build public cache from the same data if not done yet
	if (!bPublicCacheBuilt)
	{
		CachedPublicHeaders.Reset();
		for (const FString& Path : CachedAllHeaders)
		{
			if (Path.Contains(TEXT("/Public/")) || Path.Contains(TEXT("/Classes/")) ||
			    Path.Contains(TEXT("\\Public\\")) || Path.Contains(TEXT("\\Classes\\")))
			{
				CachedPublicHeaders.Add(Path);
			}
		}
		bPublicCacheBuilt = true;
	}
}

static const TArray<FString>& GetHeaders(bool bIncludePrivate)
{
	if (bIncludePrivate)
	{
		EnsureAllCache();
		return CachedAllHeaders;
	}
	else
	{
		EnsurePublicCache();
		return CachedPublicHeaders;
	}
}

// Strip common UE type prefixes (U, A, F, E, I, T) from a class name
static FString StripTypePrefix(const FString& ClassName)
{
	if (ClassName.Len() > 1 && FChar::IsUpper(ClassName[1]))
	{
		TCHAR Prefix = ClassName[0];
		if (Prefix == TEXT('U') || Prefix == TEXT('A') || Prefix == TEXT('F') ||
		    Prefix == TEXT('E') || Prefix == TEXT('I') || Prefix == TEXT('T'))
		{
			return ClassName.Mid(1);
		}
	}
	return ClassName;
}

// Make a path relative to Engine/Source for display
static FString MakeRelativePath(const FString& AbsolutePath)
{
	const FString& SourceDir = GetEngineSourceDir();
	FString Relative = AbsolutePath;
	FPaths::MakePathRelativeTo(Relative, *(SourceDir + TEXT("/")));
	return Relative;
}

// Extract class body from file content starting at a given line (0-indexed)
// Returns the class declaration + body up to the closing brace + semicolon
static FString ExtractClassBody(const TArray<FString>& Lines, int32 StartLine, int32 MaxLines = 500)
{
	FString Result;
	int32 BraceDepth = 0;
	bool bFoundOpenBrace = false;
	int32 LinesExtracted = 0;

	for (int32 i = StartLine; i < Lines.Num() && LinesExtracted < MaxLines; i++, LinesExtracted++)
	{
		Result += Lines[i] + TEXT("\n");

		for (TCHAR Ch : Lines[i])
		{
			if (Ch == TEXT('{'))
			{
				BraceDepth++;
				bFoundOpenBrace = true;
			}
			else if (Ch == TEXT('}'))
			{
				BraceDepth--;
				if (bFoundOpenBrace && BraceDepth <= 0)
				{
					// Found the closing brace of the class
					return Result;
				}
			}
		}
	}

	if (LinesExtracted >= MaxLines)
	{
		Result += FString::Printf(TEXT("\n// ... truncated at %d lines ...\n"), MaxLines);
	}

	return Result;
}

// ================================================================
// Tool Registration
// ================================================================

void RegisterAll(FMCPToolRegistry& Registry)
{
	// ================================================================
	// search_engine_class - Find and extract a UE class definition
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("class_name"), TEXT("UE class name to search for (e.g., 'UBlendSpace', 'AActor', 'FVector'). Prefix is optional."), true);
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("include_private"), TEXT("If true, also search Private/ headers. Default: false (Public/Classes only)."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("search_engine_class");
		Def.Description = TEXT("Search installed Unreal Engine headers for a class definition and extract its declaration with members. Searches the actual installed engine source, providing accurate API info for the current engine version. Returns file path, line number, and class body.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString ClassName = Args->GetStringField(TEXT("class_name"));
			if (ClassName.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("class_name is required"));
			}

			bool bIncludePrivate = false;
			if (Args->HasField(TEXT("include_private")))
			{
				bIncludePrivate = Args->GetBoolField(TEXT("include_private"));
			}

			const TArray<FString>& Headers = GetHeaders(bIncludePrivate);
			FString StrippedName = StripTypePrefix(ClassName);

			// Pre-filter: only check files whose name might contain the class
			TArray<FString> CandidateFiles;
			for (const FString& FilePath : Headers)
			{
				FString FileName = FPaths::GetBaseFilename(FilePath);
				if (FileName.Contains(StrippedName, ESearchCase::IgnoreCase) ||
				    FileName.Contains(ClassName, ESearchCase::IgnoreCase))
				{
					CandidateFiles.Add(FilePath);
				}
			}

			// If no filename matches, fall back to searching all headers
			if (CandidateFiles.Num() == 0)
			{
				CandidateFiles = Headers;
			}

			// Build regex to find the class declaration
			// Matches: class [EXPORT_API] ClassName [: public Base]
			FString PatternStr = FString::Printf(TEXT("class\\s+(?:\\w+_API\\s+)?%s\\b"), *ClassName);
			FRegexPattern Pattern(PatternStr);

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> MatchesArray;

			for (const FString& FilePath : CandidateFiles)
			{
				FString FileContent;
				if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
				{
					continue;
				}

				// Quick substring check before regex
				if (!FileContent.Contains(ClassName))
				{
					continue;
				}

				// Split into lines for line-number tracking
				TArray<FString> Lines;
				FileContent.ParseIntoArrayLines(Lines);

				for (int32 LineIdx = 0; LineIdx < Lines.Num(); LineIdx++)
				{
					FRegexMatcher Matcher(Pattern, Lines[LineIdx]);
					if (Matcher.FindNext())
					{
						// Verify it's a class declaration (not a forward decl)
						// Forward decls typically end with just ";" on the same or next line
						bool bIsForwardDecl = false;
						FString TrimmedLine = Lines[LineIdx].TrimEnd();
						if (TrimmedLine.EndsWith(TEXT(";")))
						{
							// Check if there's no brace on this line
							if (!TrimmedLine.Contains(TEXT("{")))
							{
								bIsForwardDecl = true;
							}
						}

						if (!bIsForwardDecl)
						{
							FString ClassBody = ExtractClassBody(Lines, LineIdx);

							TSharedPtr<FJsonObject> MatchObj = MakeShared<FJsonObject>();
							MatchObj->SetStringField(TEXT("file"), MakeRelativePath(FilePath));
							MatchObj->SetStringField(TEXT("absolute_path"), FilePath);
							MatchObj->SetNumberField(TEXT("line"), LineIdx + 1);
							MatchObj->SetStringField(TEXT("class_body"), ClassBody);
							MatchesArray.Add(MakeShared<FJsonValueObject>(MatchObj));

							// Usually we want the first real match
							break;
						}
					}
				}

				// Stop after finding a match in a file
				if (MatchesArray.Num() > 0)
				{
					break;
				}
			}

			if (MatchesArray.Num() == 0)
			{
				Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Class '%s' not found in engine headers"), *ClassName));
				Result->SetNumberField(TEXT("headers_searched"), CandidateFiles.Num());
				Result->SetBoolField(TEXT("include_private"), bIncludePrivate);
				if (!bIncludePrivate)
				{
					Result->SetStringField(TEXT("hint"), TEXT("Try setting include_private=true to also search Private/ headers"));
				}
			}
			else
			{
				Result->SetStringField(TEXT("class_name"), ClassName);
				Result->SetArrayField(TEXT("matches"), MatchesArray);
				Result->SetNumberField(TEXT("headers_searched"), CandidateFiles.Num());
				Result->SetStringField(TEXT("engine_source_dir"), GetEngineSourceDir());
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});

		Registry.RegisterTool(Def);
	}

	// ================================================================
	// search_engine_api - Grep-like search through engine headers
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("pattern"), TEXT("Search pattern (substring or regex) to find in engine headers. E.g., 'UpdateParameter', 'void\\s+SetMesh'."), true);
		FMCPSchemaBuilder::AddString(Schema, TEXT("path_filter"), TEXT("Only search headers whose path contains this substring. E.g., 'Animation', 'Materials', 'Runtime/Engine'."));
		FMCPSchemaBuilder::AddString(Schema, TEXT("file_filter"), TEXT("Only search headers whose filename contains this substring. E.g., 'BlendSpace', 'Actor'."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("max_results"), TEXT("Maximum number of matches to return. Default: 20."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("context_lines"), TEXT("Number of lines of context to include before and after each match. Default: 2."));
		FMCPSchemaBuilder::AddBoolean(Schema, TEXT("include_private"), TEXT("If true, also search Private/ headers. Default: false."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("search_engine_api");
		Def.Description = TEXT("Grep-like search through installed Unreal Engine headers. Finds methods, properties, macros, and any text patterns in the engine source. Useful for discovering API signatures, checking method existence, and finding usage patterns.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString SearchPattern = Args->GetStringField(TEXT("pattern"));
			if (SearchPattern.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("pattern is required"));
			}

			FString PathFilter = Args->HasField(TEXT("path_filter")) ? Args->GetStringField(TEXT("path_filter")) : TEXT("");
			FString FileFilter = Args->HasField(TEXT("file_filter")) ? Args->GetStringField(TEXT("file_filter")) : TEXT("");
			int32 MaxResults = Args->HasField(TEXT("max_results")) ? static_cast<int32>(Args->GetNumberField(TEXT("max_results"))) : 20;
			int32 ContextLines = Args->HasField(TEXT("context_lines")) ? static_cast<int32>(Args->GetNumberField(TEXT("context_lines"))) : 2;
			bool bIncludePrivate = Args->HasField(TEXT("include_private")) && Args->GetBoolField(TEXT("include_private"));

			MaxResults = FMath::Clamp(MaxResults, 1, 100);
			ContextLines = FMath::Clamp(ContextLines, 0, 10);

			const TArray<FString>& Headers = GetHeaders(bIncludePrivate);

			// Try to compile as regex; fall back to literal Contains if invalid
			bool bUseRegex = true;
			TUniquePtr<FRegexPattern> RegexPattern;
			{
				RegexPattern = MakeUnique<FRegexPattern>(SearchPattern, ERegexPatternFlags::CaseInsensitive);
				// FRegexPattern doesn't expose a validity check, so we test it
				FRegexMatcher TestMatcher(*RegexPattern, TEXT("test"));
				// If FindNext doesn't crash, pattern is valid (it may or may not match)
				TestMatcher.FindNext();
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> MatchesArray;
			int32 FilesSearched = 0;

			for (const FString& FilePath : Headers)
			{
				// Apply path filter
				if (!PathFilter.IsEmpty() && !FilePath.Contains(PathFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}

				// Apply file filter
				if (!FileFilter.IsEmpty())
				{
					FString FileName = FPaths::GetBaseFilename(FilePath);
					if (!FileName.Contains(FileFilter, ESearchCase::IgnoreCase))
					{
						continue;
					}
				}

				FString FileContent;
				if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
				{
					continue;
				}

				// Quick substring check before detailed search
				if (!FileContent.Contains(SearchPattern.Left(FMath::Min(SearchPattern.Len(), 20)), ESearchCase::IgnoreCase))
				{
					// For simple patterns, skip if substring not found
					// For complex regex this may miss, but it's a perf optimization
					if (!SearchPattern.Contains(TEXT("\\")) && !SearchPattern.Contains(TEXT("[")) &&
					    !SearchPattern.Contains(TEXT("(")) && !SearchPattern.Contains(TEXT(".")))
					{
						continue;
					}
				}

				FilesSearched++;

				TArray<FString> Lines;
				FileContent.ParseIntoArrayLines(Lines);

				for (int32 LineIdx = 0; LineIdx < Lines.Num(); LineIdx++)
				{
					bool bMatched = false;

					if (bUseRegex)
					{
						FRegexMatcher Matcher(*RegexPattern, Lines[LineIdx]);
						bMatched = Matcher.FindNext();
					}
					else
					{
						bMatched = Lines[LineIdx].Contains(SearchPattern, ESearchCase::IgnoreCase);
					}

					if (bMatched)
					{
						// Build context
						FString ContextText;
						int32 CtxStart = FMath::Max(0, LineIdx - ContextLines);
						int32 CtxEnd = FMath::Min(Lines.Num() - 1, LineIdx + ContextLines);
						for (int32 c = CtxStart; c <= CtxEnd; c++)
						{
							FString Prefix = (c == LineIdx) ? TEXT(">>> ") : TEXT("    ");
							ContextText += FString::Printf(TEXT("%s%4d: %s\n"), *Prefix, c + 1, *Lines[c]);
						}

						TSharedPtr<FJsonObject> MatchObj = MakeShared<FJsonObject>();
						MatchObj->SetStringField(TEXT("file"), MakeRelativePath(FilePath));
						MatchObj->SetStringField(TEXT("absolute_path"), FilePath);
						MatchObj->SetNumberField(TEXT("line"), LineIdx + 1);
						MatchObj->SetStringField(TEXT("context"), ContextText);
						MatchesArray.Add(MakeShared<FJsonValueObject>(MatchObj));

						if (MatchesArray.Num() >= MaxResults)
						{
							break;
						}
					}
				}

				if (MatchesArray.Num() >= MaxResults)
				{
					break;
				}
			}

			Result->SetStringField(TEXT("pattern"), SearchPattern);
			Result->SetNumberField(TEXT("total_matches"), MatchesArray.Num());
			Result->SetNumberField(TEXT("files_searched"), FilesSearched);
			Result->SetNumberField(TEXT("max_results"), MaxResults);
			Result->SetArrayField(TEXT("matches"), MatchesArray);

			if (MatchesArray.Num() >= MaxResults)
			{
				Result->SetStringField(TEXT("hint"), TEXT("Results capped at max_results. Use path_filter or file_filter to narrow the search."));
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});

		Registry.RegisterTool(Def);
	}

	// ================================================================
	// get_engine_header - Read a specific engine header file
	// ================================================================
	{
		auto Schema = FMCPSchemaBuilder::Begin();
		FMCPSchemaBuilder::AddString(Schema, TEXT("header_path"), TEXT("Path to the header file. Can be relative to Engine/Source (e.g., 'Runtime/Engine/Classes/Engine/StaticMeshActor.h') or an absolute path."), true);
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("start_line"), TEXT("Start reading from this line number (1-based). Default: 1."));
		FMCPSchemaBuilder::AddInteger(Schema, TEXT("end_line"), TEXT("Stop reading at this line number (inclusive). Default: end of file."));

		FMCPToolDefinition Def;
		Def.Name = TEXT("get_engine_header");
		Def.Description = TEXT("Read the contents of a specific Unreal Engine header file. Use search_engine_class or search_engine_api first to find the file path. Supports reading specific line ranges for large files.");
		Def.InputSchema = Schema;
		Def.bReadOnlyHint = true;
		Def.bIdempotentHint = true;
		Def.Handler.BindLambda([](const TSharedPtr<FJsonObject>& Args) -> FMCPToolResult
		{
			FString HeaderPath = Args->GetStringField(TEXT("header_path"));
			if (HeaderPath.IsEmpty())
			{
				return FMCPToolResult::Error(TEXT("header_path is required"));
			}

			// Resolve relative paths
			FString AbsolutePath = HeaderPath;
			if (!FPaths::IsRelative(HeaderPath))
			{
				// Already absolute - use as-is
			}
			else
			{
				// Relative to Engine/Source
				AbsolutePath = FPaths::Combine(GetEngineSourceDir(), HeaderPath);
			}

			FPaths::NormalizeFilename(AbsolutePath);

			// Security check: only allow reading from Engine directory
			FString EngineDir = FPaths::EngineDir();
			FPaths::NormalizeDirectoryName(EngineDir);
			if (!AbsolutePath.StartsWith(EngineDir))
			{
				return FMCPToolResult::Error(TEXT("Access denied: can only read files within the Engine directory"));
			}

			FString FileContent;
			if (!FFileHelper::LoadFileToString(FileContent, *AbsolutePath))
			{
				return FMCPToolResult::Error(FString::Printf(TEXT("Failed to read file: %s"), *AbsolutePath));
			}

			TArray<FString> Lines;
			FileContent.ParseIntoArrayLines(Lines);

			int32 TotalLines = Lines.Num();
			int32 StartLine = Args->HasField(TEXT("start_line")) ? static_cast<int32>(Args->GetNumberField(TEXT("start_line"))) : 1;
			int32 EndLine = Args->HasField(TEXT("end_line")) ? static_cast<int32>(Args->GetNumberField(TEXT("end_line"))) : TotalLines;

			StartLine = FMath::Clamp(StartLine, 1, TotalLines);
			EndLine = FMath::Clamp(EndLine, StartLine, TotalLines);

			// Cap output to 1000 lines to avoid massive responses
			if ((EndLine - StartLine + 1) > 1000)
			{
				EndLine = StartLine + 999;
			}

			FString OutputContent;
			for (int32 i = StartLine - 1; i < EndLine; i++)
			{
				OutputContent += FString::Printf(TEXT("%4d: %s\n"), i + 1, *Lines[i]);
			}

			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("file"), MakeRelativePath(AbsolutePath));
			Result->SetStringField(TEXT("absolute_path"), AbsolutePath);
			Result->SetNumberField(TEXT("total_lines"), TotalLines);
			Result->SetNumberField(TEXT("start_line"), StartLine);
			Result->SetNumberField(TEXT("end_line"), EndLine);
			Result->SetStringField(TEXT("content"), OutputContent);

			if (EndLine < TotalLines)
			{
				Result->SetStringField(TEXT("hint"), FString::Printf(TEXT("Showing lines %d-%d of %d. Use start_line/end_line to read more."), StartLine, EndLine, TotalLines));
			}

			return FMCPToolResult::Success(JsonToString(Result));
		});

		Registry.RegisterTool(Def);
	}
}

} // namespace MCPEngineAPITools
