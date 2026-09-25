// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MCPChatTypes.h"
#include "MCPChatContext.h"

/**
 * Phase 6 — what is riding along with the message.
 *
 * Three shapes, one strip:
 *   File     a document — inlined as text if it is text, referenced if it is not
 *   Image    a picture — sent to vision models, shown as a thumbnail chip
 *   Context  an @mention — no bytes at all, resolved at send time
 *
 * Bytes are NOT copied into the session folder while you are still typing. A
 * pasted screenshot you then delete should leave nothing behind, so ingestion is
 * split: attach now (cheap, in memory or by reference), commit on send.
 */
struct UNREALMCPCHAT_API FChatAttachment
{
	enum class EKind : uint8
	{
		File,
		Image,
		Context,
	};

	EKind   Kind = EKind::File;

	/** Chip label. */
	FString DisplayName;

	/** Where it came from. Empty for pasted/captured images, which exist only in
	 *  PendingBytes until the message is sent. */
	FString AbsolutePath;

	/** Filled by Commit() — relative to the session's attachments folder, so the
	 *  session directory can be moved or archived without rewriting paths. */
	FString StoredPath;

	FString MimeType;
	int64   SizeBytes = 0;
	int32   Width = 0;
	int32   Height = 0;

	/** In-memory payload for a paste or a viewport capture. */
	TArray<uint8> PendingBytes;

	/** Context kind only. */
	EChatContextKind ContextKind = EChatContextKind::Unknown;
	FString          ContextTarget;
	/** Pinned context survives the turn and is re-resolved on every later turn. */
	bool             bPinned = false;

	/** Non-empty when the attachment is unusable — the chip renders amber and the
	 *  send still goes through without it. One bad file must not block a message. */
	FText Error;

	/** Stable identity for the strip's remove buttons. */
	FGuid Id;

	bool IsImage() const   { return Kind == EKind::Image; }
	bool IsContext() const { return Kind == EKind::Context; }
	bool IsValidForSend() const { return Error.IsEmpty(); }

	/** "4.2 KB" / "340×220" / "/Game/Props/SM_Crate". */
	FString GetDetailText() const;
};

using FChatAttachmentPtr = TSharedPtr<FChatAttachment>;

class UNREALMCPCHAT_API FMCPChatAttachments
{
public:
	// ---- Construction ----

	/** From a path on disk. Fails (with a filled-in Error) on oversize files, which
	 *  the caller offers to attach as a path reference instead. */
	static FChatAttachmentPtr FromFile(const FString& AbsolutePath);

	/** From bytes already in memory — a paste, a viewport capture. Bytes must be an
	 *  encoded image (PNG/JPEG); raw pixels go through EncodePngFromBGRA first. */
	static FChatAttachmentPtr FromImageBytes(TArray<uint8>&& Bytes, const FString& SuggestedName);

	/** Content Browser drag-drop → a reference chip, not a copy of the .uasset.
	 *  Attaching a 200 MB mesh as bytes helps nobody; the model wants the path. */
	static FChatAttachmentPtr FromAssetPath(const FString& ObjectPath, const FString& AssetName);

	/** World Outliner drag-drop. */
	static FChatAttachmentPtr FromActorLabel(const FString& ActorLabel);

	static FChatAttachmentPtr FromContext(EChatContextKind Kind, const FString& Target);

	/** Path reference fallback for a file too big to inline. */
	static FChatAttachmentPtr AsPathReference(const FString& AbsolutePath);

	// ---- Clipboard ----

	/** True when the OS clipboard holds a bitmap we can attach. Windows only for
	 *  now; elsewhere this returns false and Ctrl+V pastes text as usual. */
	static bool ClipboardHasImage();

	/** Pull the clipboard bitmap out as PNG bytes. */
	static FChatAttachmentPtr FromClipboardImage();

	// ---- Send ----

	/**
	 * Write pending bytes and copy referenced files into the session's attachments
	 * folder, filling in StoredPath. Called once, at send time.
	 * @return false when at least one attachment could not be written (its Error is set).
	 */
	static bool Commit(TArray<FChatAttachmentPtr>& Attachments, const FGuid& SessionId);

	/** Committed attachment → transcript block. Context chips resolve here, which is
	 *  why this takes the resolver's failure path rather than asserting. */
	static FChatContentBlock ToContentBlock(const FChatAttachment& Attachment);

	// ---- Helpers ----

	static FString GuessMimeType(const FString& Extension);
	static bool    IsImageMime(const FString& MimeType);
	/** Text-ish files are inlined into the prompt; everything else is referenced. */
	static bool    IsInlinableTextMime(const FString& MimeType);
	static FString FormatBytes(int64 Bytes);

	/** Read an encoded image's dimensions without decoding the pixels. */
	static bool ReadImageDimensions(const TArray<uint8>& Bytes, int32& OutWidth, int32& OutHeight);

	/** Raw BGRA8 → PNG. Used by the clipboard and viewport-capture paths. */
	static bool EncodePngFromBGRA(const TArray<uint8>& BGRA, int32 Width, int32 Height, TArray<uint8>& OutPng);

	/** Defaults; both overridable in settings once the settings UI lands (Phase 8). */
	static constexpr int64 MaxAttachmentBytes = 20ll * 1024 * 1024;
	static constexpr int32 MaxAttachmentsPerMessage = 10;
	/** Above this, an inlined text file is truncated rather than dropped. */
	static constexpr int32 MaxInlinedTextChars = 200 * 1024;
};
