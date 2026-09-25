// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Phase 6 — `@` context references.
 *
 * A mention is stored as (kind, target) and resolved to text LATE, at send time.
 * That timing is the entire design:
 *
 *   Type `@Selection` → select three actors → press Enter
 *     → the model is told about the three actors.
 *
 * Resolving at type time would have sent whatever happened to be selected while
 * the user was still composing, which is almost never what they meant. It also
 * means a pinned chip re-resolves every turn for free.
 *
 * Most kinds delegate to the host plugin's MCP resources (unreal://…), so the
 * chat panel and an external agent see byte-identical context — one source of
 * truth, and no second implementation to drift.
 */

enum class EChatContextKind : uint8
{
	Asset,        // @Asset:/Game/Foo         → registry metadata
	Actor,        // @Actor:BP_Player_2       → label, class, transform
	Blueprint,    // @Blueprint:/Game/BP_Foo  → registry metadata + graph hint
	Level,        // @Level                   → unreal://level/current
	Selection,    // @Selection               → unreal://editor/selection
	Viewport,     // @Viewport                → unreal://editor/viewport
	Log,          // @Log                     → unreal://editor/log
	Project,      // @Project                 → unreal://project/info
	Performance,  // @Performance             → unreal://editor/performance
	Analysis,     // @Analysis                → unreal://level/analysis
	File,         // @File:Source/Foo.cpp     → disk, contents inlined
	Folder,       // @Folder:Source/Bar       → disk, recursive listing
	Unknown,
};

/** One row in the `@` popup. */
struct FChatContextCandidate
{
	EChatContextKind Kind = EChatContextKind::Unknown;
	/** What follows the colon. Empty for the zero-argument kinds. */
	FString Target;
	/** What the row shows: "@Asset:SM_Crate". */
	FString Label;
	/** Dimmed right-hand column: "/Game/Props", "StaticMesh", "12 actors". */
	FString Detail;
	/** True when picking this row should insert a chip immediately; false when it
	 *  only narrows the query (choosing the "Asset:" category, say). */
	bool bIsTerminal = true;
};

class UNREALMCPCHAT_API FMCPChatContextResolver
{
public:
	static FMCPChatContextResolver& Get();

	// ---- Kind helpers ----

	static const TCHAR*     KindToString(EChatContextKind Kind);
	static EChatContextKind KindFromString(const FString& In);
	static FText            KindDisplayName(EChatContextKind Kind);
	static FText            KindDescription(EChatContextKind Kind);
	/** False for @Selection, @Level, @Log … — kinds that stand alone. */
	static bool             KindNeedsTarget(EChatContextKind Kind);

	/** Chip text: "@Selection", "@Asset:SM_Crate". Targets are shortened to their
	 *  leaf name because a chip showing a full /Game path eats the composer. */
	static FString MakeLabel(EChatContextKind Kind, const FString& Target);

	// ---- Autocomplete ----

	/**
	 * Suggestions for the text following an `@`.
	 *
	 *   ""              → the zero-argument kinds plus the category stubs
	 *   "sel"           → @Selection
	 *   "Asset:"        → recent/likely assets
	 *   "Asset:crate"   → fuzzy asset search
	 *   "crate"         → falls back to searching assets and actors directly, so a
	 *                     user who does not know the categories still gets a hit
	 */
	TArray<FChatContextCandidate> Suggest(const FString& Query, int32 Limit = 8) const;

	// ---- Resolution ----

	/**
	 * Produce the text the model sees. Game thread only — this touches GEditor,
	 * the asset registry and the world.
	 *
	 * A failure is NOT fatal to the turn: the caller renders the error on the chip
	 * and sends the remaining context. Losing one @mention should not lose the
	 * message the user typed.
	 */
	bool Resolve(EChatContextKind Kind, const FString& Target, FString& OutText, FText& OutError) const;
	bool Resolve(const FString& KindStr, const FString& Target, FString& OutText, FText& OutError) const;

	/** Largest inlined @File payload. Bigger files are truncated with a marker
	 *  rather than refused — a truncated head is usually still useful. */
	static constexpr int64 MaxInlinedFileBytes = 256 * 1024;
	/** Cap on @Folder entries, for the same reason. */
	static constexpr int32 MaxFolderEntries = 400;

private:
	FMCPChatContextResolver() = default;

	/** Read one of the host plugin's MCP resources. */
	bool ReadResource(const FString& Uri, FString& OutText, FText& OutError) const;

	bool ResolveAsset(const FString& Target, FString& OutText, FText& OutError) const;
	bool ResolveActor(const FString& Target, FString& OutText, FText& OutError) const;
	bool ResolveFile(const FString& Target, FString& OutText, FText& OutError) const;
	bool ResolveFolder(const FString& Target, FString& OutText, FText& OutError) const;
};
