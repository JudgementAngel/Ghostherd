// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatContext.h"
#include "UnrealMCPChatModule.h"

#include "MCPResourceProvider.h"
#include "MCPSearchIndex.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Selection.h"

#define LOCTEXT_NAMESPACE "MCPChatContext"

namespace
{
	/** Every kind, in the order the popup lists them when the query is empty. The
	 *  zero-argument ones come first because they are the ones people reach for
	 *  without thinking — "look at what I have selected". */
	const EChatContextKind AllKinds[] =
	{
		EChatContextKind::Selection,
		EChatContextKind::Viewport,
		EChatContextKind::Level,
		EChatContextKind::Asset,
		EChatContextKind::Actor,
		EChatContextKind::Blueprint,
		EChatContextKind::File,
		EChatContextKind::Folder,
		EChatContextKind::Log,
		EChatContextKind::Performance,
		EChatContextKind::Analysis,
		EChatContextKind::Project,
	};

	/** Case-insensitive subsequence match — "sel" hits "Selection", "bpp" hits
	 *  "BP_Player". Cheap, and it is what people expect from a mention popup. */
	bool FuzzyMatches(const FString& Needle, const FString& Haystack)
	{
		if (Needle.IsEmpty()) { return true; }
		int32 H = 0;
		for (int32 N = 0; N < Needle.Len(); ++N)
		{
			const TCHAR C = FChar::ToLower(Needle[N]);
			bool bFound = false;
			while (H < Haystack.Len())
			{
				if (FChar::ToLower(Haystack[H++]) == C) { bFound = true; break; }
			}
			if (!bFound) { return false; }
		}
		return true;
	}

	FString LeafName(const FString& Target)
	{
		FString Leaf = Target;
		int32 Slash = INDEX_NONE;
		if (Leaf.FindLastChar(TEXT('/'), Slash)) { Leaf.MidInline(Slash + 1); }
		int32 Dot = INDEX_NONE;
		if (Leaf.FindLastChar(TEXT('.'), Dot))   { Leaf.LeftInline(Dot); }
		return Leaf.IsEmpty() ? Target : Leaf;
	}
}

FMCPChatContextResolver& FMCPChatContextResolver::Get()
{
	static FMCPChatContextResolver Instance;
	return Instance;
}

// ============================================================================
// Kind helpers
// ============================================================================

const TCHAR* FMCPChatContextResolver::KindToString(EChatContextKind Kind)
{
	switch (Kind)
	{
	case EChatContextKind::Asset:       return TEXT("asset");
	case EChatContextKind::Actor:       return TEXT("actor");
	case EChatContextKind::Blueprint:   return TEXT("blueprint");
	case EChatContextKind::Level:       return TEXT("level");
	case EChatContextKind::Selection:   return TEXT("selection");
	case EChatContextKind::Viewport:    return TEXT("viewport");
	case EChatContextKind::Log:         return TEXT("log");
	case EChatContextKind::Project:     return TEXT("project");
	case EChatContextKind::Performance: return TEXT("performance");
	case EChatContextKind::Analysis:    return TEXT("analysis");
	case EChatContextKind::File:        return TEXT("file");
	case EChatContextKind::Folder:      return TEXT("folder");
	default:                            return TEXT("unknown");
	}
}

EChatContextKind FMCPChatContextResolver::KindFromString(const FString& In)
{
	const FString L = In.ToLower();
	if (L == TEXT("asset"))       { return EChatContextKind::Asset; }
	if (L == TEXT("actor"))       { return EChatContextKind::Actor; }
	if (L == TEXT("blueprint"))   { return EChatContextKind::Blueprint; }
	if (L == TEXT("level"))       { return EChatContextKind::Level; }
	if (L == TEXT("selection"))   { return EChatContextKind::Selection; }
	if (L == TEXT("viewport"))    { return EChatContextKind::Viewport; }
	if (L == TEXT("log"))         { return EChatContextKind::Log; }
	if (L == TEXT("project"))     { return EChatContextKind::Project; }
	if (L == TEXT("performance")) { return EChatContextKind::Performance; }
	if (L == TEXT("analysis"))    { return EChatContextKind::Analysis; }
	if (L == TEXT("file"))        { return EChatContextKind::File; }
	if (L == TEXT("folder"))      { return EChatContextKind::Folder; }
	return EChatContextKind::Unknown;
}

FText FMCPChatContextResolver::KindDisplayName(EChatContextKind Kind)
{
	switch (Kind)
	{
	case EChatContextKind::Asset:       return LOCTEXT("KindAsset", "Asset");
	case EChatContextKind::Actor:       return LOCTEXT("KindActor", "Actor");
	case EChatContextKind::Blueprint:   return LOCTEXT("KindBlueprint", "Blueprint");
	case EChatContextKind::Level:       return LOCTEXT("KindLevel", "Level");
	case EChatContextKind::Selection:   return LOCTEXT("KindSelection", "Selection");
	case EChatContextKind::Viewport:    return LOCTEXT("KindViewport", "Viewport");
	case EChatContextKind::Log:         return LOCTEXT("KindLog", "Log");
	case EChatContextKind::Project:     return LOCTEXT("KindProject", "Project");
	case EChatContextKind::Performance: return LOCTEXT("KindPerformance", "Performance");
	case EChatContextKind::Analysis:    return LOCTEXT("KindAnalysis", "Analysis");
	case EChatContextKind::File:        return LOCTEXT("KindFile", "File");
	case EChatContextKind::Folder:      return LOCTEXT("KindFolder", "Folder");
	default:                            return LOCTEXT("KindUnknown", "Unknown");
	}
}

FText FMCPChatContextResolver::KindDescription(EChatContextKind Kind)
{
	switch (Kind)
	{
	case EChatContextKind::Asset:       return LOCTEXT("DescAsset", "An asset's path, class and metadata");
	case EChatContextKind::Actor:       return LOCTEXT("DescActor", "An actor's label, class and transform");
	case EChatContextKind::Blueprint:   return LOCTEXT("DescBlueprint", "A Blueprint asset, with a hint to inspect its graphs");
	case EChatContextKind::Level:       return LOCTEXT("DescLevel", "The open level: name, actor count, bounds");
	case EChatContextKind::Selection:   return LOCTEXT("DescSelection", "What is selected when you press Enter");
	case EChatContextKind::Viewport:    return LOCTEXT("DescViewport", "Viewport camera position and orientation");
	case EChatContextKind::Log:         return LOCTEXT("DescLog", "Recent Output Log lines");
	case EChatContextKind::Project:     return LOCTEXT("DescProject", "Project name, engine version, enabled plugins");
	case EChatContextKind::Performance: return LOCTEXT("DescPerformance", "Frame time and render statistics");
	case EChatContextKind::Analysis:    return LOCTEXT("DescAnalysis", "Automated scene health report");
	case EChatContextKind::File:        return LOCTEXT("DescFile", "A project file, contents inlined");
	case EChatContextKind::Folder:      return LOCTEXT("DescFolder", "A project folder, listed recursively");
	default:                            return FText::GetEmpty();
	}
}

bool FMCPChatContextResolver::KindNeedsTarget(EChatContextKind Kind)
{
	switch (Kind)
	{
	case EChatContextKind::Asset:
	case EChatContextKind::Actor:
	case EChatContextKind::Blueprint:
	case EChatContextKind::File:
	case EChatContextKind::Folder:
		return true;
	default:
		return false;
	}
}

FString FMCPChatContextResolver::MakeLabel(EChatContextKind Kind, const FString& Target)
{
	const FString KindName = KindDisplayName(Kind).ToString();
	if (!KindNeedsTarget(Kind) || Target.IsEmpty())
	{
		return FString::Printf(TEXT("@%s"), *KindName);
	}
	// A chip showing "/Game/Environment/Props/Meshes/SM_Crate" would eat the whole
	// composer; the full path lives in the tooltip.
	return FString::Printf(TEXT("@%s:%s"), *KindName, *LeafName(Target));
}

// ============================================================================
// Autocomplete
// ============================================================================

TArray<FChatContextCandidate> FMCPChatContextResolver::Suggest(const FString& Query, int32 Limit) const
{
	TArray<FChatContextCandidate> Out;

	// Split "Asset:crate" into category + remainder.
	FString CategoryPart = Query;
	FString TargetPart;
	int32 Colon = INDEX_NONE;
	const bool bHasColon = Query.FindChar(TEXT(':'), Colon);
	if (bHasColon)
	{
		CategoryPart = Query.Left(Colon);
		TargetPart   = Query.Mid(Colon + 1);
	}

	const EChatContextKind TypedKind = KindFromString(CategoryPart);

	// ---- Category chosen: search inside it ----
	if (bHasColon && TypedKind != EChatContextKind::Unknown && KindNeedsTarget(TypedKind))
	{
		switch (TypedKind)
		{
		case EChatContextKind::Asset:
		case EChatContextKind::Blueprint:
		{
			const FString Filter = (TypedKind == EChatContextKind::Blueprint) ? TEXT("Blueprint") : TEXT("Asset");
			for (const FMCPSearchResult& R : FMCPSearchIndex::Get().Search(TargetPart, TEXT("Asset"), Limit * 2))
			{
				// The index has no dedicated Blueprint category, so filter on class.
				if (TypedKind == EChatContextKind::Blueprint && !R.Category.Contains(Filter)) { continue; }
				FChatContextCandidate C;
				C.Kind   = TypedKind;
				C.Target = R.Path;
				C.Label  = R.Name;
				C.Detail = R.Category;
				Out.Add(MoveTemp(C));
				if (Out.Num() >= Limit) { break; }
			}
			break;
		}
		case EChatContextKind::Actor:
		{
			for (const FMCPSearchResult& R : FMCPSearchIndex::Get().Search(TargetPart, TEXT("Actor"), Limit))
			{
				FChatContextCandidate C;
				C.Kind   = EChatContextKind::Actor;
				C.Target = R.Name;
				C.Label  = R.Name;
				C.Detail = R.Category;
				Out.Add(MoveTemp(C));
			}
			break;
		}
		case EChatContextKind::File:
		case EChatContextKind::Folder:
		{
			// Complete against the project directory. Anything outside it is not
			// offered — a mention popup is not a filesystem browser, and inlining
			// arbitrary machine paths into a provider request is a bad default.
			const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
			const bool bWantDirs = (TypedKind == EChatContextKind::Folder);

			TArray<FString> Found;
			IFileManager& FM = IFileManager::Get();
			// Search only the top few levels; a full recursive scan of a real project
			// would stall the popup.
			TArray<FString> Roots = { Root, Root / TEXT("Source"), Root / TEXT("Config"), Root / TEXT("Content") };
			for (const FString& Dir : Roots)
			{
				TArray<FString> Entries;
				if (bWantDirs) { FM.FindFiles(Entries, *(Dir / TEXT("*")), false, true); }
				else           { FM.FindFiles(Entries, *(Dir / TEXT("*")), true, false); }
				for (const FString& E : Entries)
				{
					FString Rel = (Dir / E);
					FPaths::MakePathRelativeTo(Rel, *Root);
					Found.AddUnique(Rel);
				}
			}

			for (const FString& Rel : Found)
			{
				if (!FuzzyMatches(TargetPart, Rel)) { continue; }
				FChatContextCandidate C;
				C.Kind   = TypedKind;
				C.Target = Rel;
				C.Label  = FPaths::GetCleanFilename(Rel);
				C.Detail = FPaths::GetPath(Rel);
				Out.Add(MoveTemp(C));
				if (Out.Num() >= Limit) { break; }
			}
			break;
		}
		default:
			break;
		}
		return Out;
	}

	// ---- No category yet: offer kinds, then fall back to a direct search ----
	for (const EChatContextKind Kind : AllKinds)
	{
		const FString Name = KindDisplayName(Kind).ToString();
		if (!FuzzyMatches(CategoryPart, Name)) { continue; }

		FChatContextCandidate C;
		C.Kind        = Kind;
		C.Label       = KindNeedsTarget(Kind) ? (Name + TEXT(":")) : Name;
		C.Detail      = KindDescription(Kind).ToString();
		// A kind that needs a target only narrows the query; picking it types the
		// prefix and leaves the popup open.
		C.bIsTerminal = !KindNeedsTarget(Kind);
		Out.Add(MoveTemp(C));
		if (Out.Num() >= Limit) { return Out; }
	}

	// Someone who does not know the categories still expects "@crate" to find their
	// crate, so spend the remaining rows on a direct search.
	if (!CategoryPart.IsEmpty() && Out.Num() < Limit)
	{
		for (const FMCPSearchResult& R : FMCPSearchIndex::Get().Search(CategoryPart, TEXT(""), Limit))
		{
			const bool bIsActor = (R.Type == TEXT("Actor"));
			FChatContextCandidate C;
			C.Kind   = bIsActor ? EChatContextKind::Actor : EChatContextKind::Asset;
			C.Target = bIsActor ? R.Name : R.Path;
			C.Label  = R.Name;
			C.Detail = R.Category;
			Out.Add(MoveTemp(C));
			if (Out.Num() >= Limit) { break; }
		}
	}

	return Out;
}

// ============================================================================
// Resolution
// ============================================================================

bool FMCPChatContextResolver::ReadResource(const FString& Uri, FString& OutText, FText& OutError) const
{
	FMCPResourceProvider& Provider = FMCPResourceProvider::Get();
	if (!Provider.HasResource(Uri))
	{
		OutError = FText::Format(
			LOCTEXT("NoResource", "The MCP server does not expose {0}."), FText::FromString(Uri));
		return false;
	}

	const FMCPResourceContent Content = Provider.ReadResource(Uri);
	if (Content.Text.IsEmpty())
	{
		OutError = FText::Format(
			LOCTEXT("EmptyResource", "{0} returned nothing."), FText::FromString(Uri));
		return false;
	}

	OutText = Content.Text;
	return true;
}

bool FMCPChatContextResolver::Resolve(const FString& KindStr, const FString& Target,
                                      FString& OutText, FText& OutError) const
{
	return Resolve(KindFromString(KindStr), Target, OutText, OutError);
}

bool FMCPChatContextResolver::Resolve(EChatContextKind Kind, const FString& Target,
                                      FString& OutText, FText& OutError) const
{
	check(IsInGameThread());

	switch (Kind)
	{
	case EChatContextKind::Selection:   return ReadResource(TEXT("unreal://editor/selection"),   OutText, OutError);
	case EChatContextKind::Viewport:    return ReadResource(TEXT("unreal://editor/viewport"),    OutText, OutError);
	case EChatContextKind::Level:       return ReadResource(TEXT("unreal://level/current"),      OutText, OutError);
	case EChatContextKind::Log:         return ReadResource(TEXT("unreal://editor/log"),         OutText, OutError);
	case EChatContextKind::Project:     return ReadResource(TEXT("unreal://project/info"),       OutText, OutError);
	case EChatContextKind::Performance: return ReadResource(TEXT("unreal://editor/performance"), OutText, OutError);
	case EChatContextKind::Analysis:    return ReadResource(TEXT("unreal://level/analysis"),     OutText, OutError);

	case EChatContextKind::Asset:
	case EChatContextKind::Blueprint:   return ResolveAsset(Target, OutText, OutError);
	case EChatContextKind::Actor:       return ResolveActor(Target, OutText, OutError);
	case EChatContextKind::File:        return ResolveFile(Target, OutText, OutError);
	case EChatContextKind::Folder:      return ResolveFolder(Target, OutText, OutError);

	default:
		OutError = LOCTEXT("UnknownKind", "Unknown context type.");
		return false;
	}
}

bool FMCPChatContextResolver::ResolveAsset(const FString& Target, FString& OutText, FText& OutError) const
{
	if (Target.IsEmpty())
	{
		OutError = LOCTEXT("NoAssetPath", "No asset path.");
		return false;
	}

	const FAssetRegistryModule& Module =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	const IAssetRegistry& Registry = Module.Get();

	FAssetData Data = Registry.GetAssetByObjectPath(FSoftObjectPath(Target));
	if (!Data.IsValid())
	{
		// The popup inserts object paths, but a hand-typed package path is common and
		// worth accepting rather than failing on a missing ".Foo" suffix.
		TArray<FAssetData> InPackage;
		Registry.GetAssetsByPackageName(FName(*Target), InPackage);
		if (InPackage.Num() > 0) { Data = InPackage[0]; }
	}

	if (!Data.IsValid())
	{
		OutError = FText::Format(LOCTEXT("AssetNotFound", "No asset at {0}."), FText::FromString(Target));
		return false;
	}

	TStringBuilder<1024> SB;
	SB.Appendf(TEXT("Asset: %s\n"), *Data.AssetName.ToString());
	SB.Appendf(TEXT("Path: %s\n"), *Data.GetObjectPathString());
	SB.Appendf(TEXT("Class: %s\n"), *Data.AssetClassPath.ToString());
	SB.Appendf(TEXT("Package: %s\n"), *Data.PackageName.ToString());

	// Registry tags carry the useful per-class summary (vertex counts, material
	// slots, parent class …) without loading the asset, which is the point.
	int32 TagCount = 0;
	for (const TPair<FName, FAssetTagValueRef>& Tag : Data.TagsAndValues)
	{
		if (TagCount == 0) { SB.Append(TEXT("Metadata:\n")); }
		SB.Appendf(TEXT("  %s: %s\n"), *Tag.Key.ToString(), *Tag.Value.AsString());
		if (++TagCount >= 24) { SB.Append(TEXT("  …\n")); break; }
	}

	OutText = SB.ToString();
	return true;
}

bool FMCPChatContextResolver::ResolveActor(const FString& Target, FString& OutText, FText& OutError) const
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		OutError = LOCTEXT("NoWorld", "No editor world.");
		return false;
	}

	AActor* Match = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)) { continue; }
		if (Actor->GetActorLabel() == Target || Actor->GetName() == Target) { Match = Actor; break; }
	}

	if (!Match)
	{
		OutError = FText::Format(LOCTEXT("ActorNotFound", "No actor named '{0}' in the open level."),
			FText::FromString(Target));
		return false;
	}

	const FTransform T = Match->GetActorTransform();
	const FVector Loc = T.GetLocation();
	const FRotator Rot = T.Rotator();
	const FVector Scale = T.GetScale3D();

	TStringBuilder<1024> SB;
	SB.Appendf(TEXT("Actor: %s\n"), *Match->GetActorLabel());
	SB.Appendf(TEXT("Name: %s\n"), *Match->GetName());
	SB.Appendf(TEXT("Class: %s\n"), *Match->GetClass()->GetPathName());
	SB.Appendf(TEXT("Location: (%.2f, %.2f, %.2f)\n"), Loc.X, Loc.Y, Loc.Z);
	SB.Appendf(TEXT("Rotation: (P=%.2f, Y=%.2f, R=%.2f)\n"), Rot.Pitch, Rot.Yaw, Rot.Roll);
	SB.Appendf(TEXT("Scale: (%.3f, %.3f, %.3f)\n"), Scale.X, Scale.Y, Scale.Z);

	TArray<UActorComponent*> Components;
	Match->GetComponents(Components);
	SB.Appendf(TEXT("Components (%d):\n"), Components.Num());
	int32 Written = 0;
	for (const UActorComponent* C : Components)
	{
		if (!C) { continue; }
		SB.Appendf(TEXT("  %s (%s)\n"), *C->GetName(), *C->GetClass()->GetName());
		if (++Written >= 20) { SB.Append(TEXT("  …\n")); break; }
	}

	OutText = SB.ToString();
	return true;
}

bool FMCPChatContextResolver::ResolveFile(const FString& Target, FString& OutText, FText& OutError) const
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Absolute = FPaths::ConvertRelativePathToFull(Root / Target);

	// Containment check, not a string prefix on the raw input: "../../.." resolves
	// away and would otherwise walk out of the project.
	if (!Absolute.StartsWith(Root))
	{
		OutError = LOCTEXT("FileOutsideProject", "Only files inside the project can be attached this way.");
		return false;
	}

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.FileExists(*Absolute))
	{
		OutError = FText::Format(LOCTEXT("FileMissing", "No file at {0}."), FText::FromString(Target));
		return false;
	}

	FString Contents;
	if (!FFileHelper::LoadFileToString(Contents, *Absolute))
	{
		OutError = FText::Format(LOCTEXT("FileUnreadable", "Could not read {0}."), FText::FromString(Target));
		return false;
	}

	bool bTruncated = false;
	if (Contents.Len() > MaxInlinedFileBytes)
	{
		Contents.LeftInline(static_cast<int32>(MaxInlinedFileBytes));
		bTruncated = true;
	}

	OutText = FString::Printf(TEXT("File: %s\n\n%s%s"),
		*Target, *Contents,
		bTruncated ? TEXT("\n\n… (truncated — attach the file itself for the rest)") : TEXT(""));
	return true;
}

bool FMCPChatContextResolver::ResolveFolder(const FString& Target, FString& OutText, FText& OutError) const
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const FString Absolute = FPaths::ConvertRelativePathToFull(Root / Target);

	if (!Absolute.StartsWith(Root))
	{
		OutError = LOCTEXT("FolderOutsideProject", "Only folders inside the project can be attached this way.");
		return false;
	}

	IFileManager& FM = IFileManager::Get();
	if (!FM.DirectoryExists(*Absolute))
	{
		OutError = FText::Format(LOCTEXT("FolderMissing", "No folder at {0}."), FText::FromString(Target));
		return false;
	}

	TArray<FString> Files;
	FM.FindFilesRecursive(Files, *Absolute, TEXT("*"), true, false);

	TStringBuilder<4096> SB;
	SB.Appendf(TEXT("Folder: %s (%d files)\n"), *Target, Files.Num());
	const int32 Count = FMath::Min(Files.Num(), MaxFolderEntries);
	for (int32 i = 0; i < Count; ++i)
	{
		FString Rel = Files[i];
		FPaths::MakePathRelativeTo(Rel, *(Absolute / TEXT("")));
		SB.Appendf(TEXT("  %s (%lld bytes)\n"), *Rel, FM.FileSize(*Files[i]));
	}
	if (Files.Num() > Count)
	{
		SB.Appendf(TEXT("  … %d more\n"), Files.Num() - Count);
	}

	OutText = SB.ToString();
	return true;
}

#undef LOCTEXT_NAMESPACE
