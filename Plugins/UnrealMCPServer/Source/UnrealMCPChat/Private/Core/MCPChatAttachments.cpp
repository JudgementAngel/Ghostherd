// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "MCPChatAttachments.h"
#include "MCPChatStore.h"
#include "UnrealMCPChatModule.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Internationalization/Internationalization.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#define LOCTEXT_NAMESPACE "MCPChatAttachments"

// ============================================================================
// FChatAttachment
// ============================================================================

FString FChatAttachment::GetDetailText() const
{
	switch (Kind)
	{
	case EKind::Image:
		// Dimensions say more than a byte count for a picture — "is that the whole
		// screenshot or a crop?" is the question people actually have.
		return (Width > 0 && Height > 0)
			? FString::Printf(TEXT("%d×%d"), Width, Height)
			: FMCPChatAttachments::FormatBytes(SizeBytes);

	case EKind::Context:
		return ContextTarget.IsEmpty()
			? FMCPChatContextResolver::KindDescription(ContextKind).ToString()
			: ContextTarget;

	case EKind::File:
	default:
		return FMCPChatAttachments::FormatBytes(SizeBytes);
	}
}

// ============================================================================
// Construction
// ============================================================================

FChatAttachmentPtr FMCPChatAttachments::FromFile(const FString& AbsolutePath)
{
	FChatAttachmentPtr A = MakeShared<FChatAttachment>();
	A->Id           = FGuid::NewGuid();
	A->AbsolutePath = AbsolutePath;
	A->DisplayName  = FPaths::GetCleanFilename(AbsolutePath);
	A->MimeType     = GuessMimeType(FPaths::GetExtension(AbsolutePath));
	A->Kind         = IsImageMime(A->MimeType) ? FChatAttachment::EKind::Image : FChatAttachment::EKind::File;

	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.FileExists(*AbsolutePath))
	{
		A->Error = FText::Format(LOCTEXT("FileGone", "'{0}' no longer exists."),
			FText::FromString(A->DisplayName));
		return A;
	}

	A->SizeBytes = PF.FileSize(*AbsolutePath);
	if (A->SizeBytes > MaxAttachmentBytes)
	{
		A->Error = FText::Format(
			LOCTEXT("FileTooBig", "{0} is over the {1} limit. Attach it as a path reference instead."),
			FText::FromString(FormatBytes(A->SizeBytes)),
			FText::FromString(FormatBytes(MaxAttachmentBytes)));
		return A;
	}

	if (A->Kind == FChatAttachment::EKind::Image)
	{
		TArray<uint8> Bytes;
		if (FFileHelper::LoadFileToArray(Bytes, *AbsolutePath))
		{
			ReadImageDimensions(Bytes, A->Width, A->Height);
		}
	}

	return A;
}

FChatAttachmentPtr FMCPChatAttachments::FromImageBytes(TArray<uint8>&& Bytes, const FString& SuggestedName)
{
	FChatAttachmentPtr A = MakeShared<FChatAttachment>();
	A->Id          = FGuid::NewGuid();
	A->Kind        = FChatAttachment::EKind::Image;
	A->DisplayName = SuggestedName.IsEmpty() ? TEXT("pasted.png") : SuggestedName;
	A->MimeType    = GuessMimeType(FPaths::GetExtension(A->DisplayName));
	if (A->MimeType.IsEmpty()) { A->MimeType = TEXT("image/png"); }
	A->SizeBytes   = Bytes.Num();

	ReadImageDimensions(Bytes, A->Width, A->Height);
	A->PendingBytes = MoveTemp(Bytes);

	if (A->SizeBytes > MaxAttachmentBytes)
	{
		A->Error = FText::Format(LOCTEXT("ImageTooBig", "Image is over the {0} limit."),
			FText::FromString(FormatBytes(MaxAttachmentBytes)));
	}
	return A;
}

FChatAttachmentPtr FMCPChatAttachments::FromAssetPath(const FString& ObjectPath, const FString& AssetName)
{
	FChatAttachmentPtr A = MakeShared<FChatAttachment>();
	A->Id            = FGuid::NewGuid();
	A->Kind          = FChatAttachment::EKind::Context;
	A->ContextKind   = EChatContextKind::Asset;
	A->ContextTarget = ObjectPath;
	A->DisplayName   = FMCPChatContextResolver::MakeLabel(EChatContextKind::Asset,
		AssetName.IsEmpty() ? ObjectPath : AssetName);
	return A;
}

FChatAttachmentPtr FMCPChatAttachments::FromActorLabel(const FString& ActorLabel)
{
	FChatAttachmentPtr A = MakeShared<FChatAttachment>();
	A->Id            = FGuid::NewGuid();
	A->Kind          = FChatAttachment::EKind::Context;
	A->ContextKind   = EChatContextKind::Actor;
	A->ContextTarget = ActorLabel;
	A->DisplayName   = FMCPChatContextResolver::MakeLabel(EChatContextKind::Actor, ActorLabel);
	return A;
}

FChatAttachmentPtr FMCPChatAttachments::FromContext(EChatContextKind Kind, const FString& Target)
{
	FChatAttachmentPtr A = MakeShared<FChatAttachment>();
	A->Id            = FGuid::NewGuid();
	A->Kind          = FChatAttachment::EKind::Context;
	A->ContextKind   = Kind;
	A->ContextTarget = Target;
	A->DisplayName   = FMCPChatContextResolver::MakeLabel(Kind, Target);
	return A;
}

FChatAttachmentPtr FMCPChatAttachments::AsPathReference(const FString& AbsolutePath)
{
	FChatAttachmentPtr A = MakeShared<FChatAttachment>();
	A->Id            = FGuid::NewGuid();
	A->Kind          = FChatAttachment::EKind::Context;
	A->ContextKind   = EChatContextKind::File;
	A->DisplayName   = FPaths::GetCleanFilename(AbsolutePath);

	// The resolver only reads inside the project, so store the relative form when we
	// can and let it fail cleanly when we cannot.
	FString Rel = AbsolutePath;
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	if (FPaths::MakePathRelativeTo(Rel, *Root)) { A->ContextTarget = Rel; }
	else                                        { A->ContextTarget = AbsolutePath; }

	return A;
}

// ============================================================================
// Clipboard
// ============================================================================

#if PLATFORM_WINDOWS
namespace
{
	/**
	 * CF_DIB → BGRA8.
	 *
	 * The clipboard hands over a BITMAPINFOHEADER followed by pixels, bottom-up
	 * unless Height is negative, and 24-bit rows are padded to 4 bytes. Getting
	 * either of those wrong yields an upside-down or sheared image, which is the
	 * classic bug in every hand-rolled clipboard paste.
	 */
	bool DibToBGRA(const uint8* Dib, SIZE_T DibSize, TArray<uint8>& OutBGRA, int32& OutW, int32& OutH)
	{
		if (!Dib || DibSize < sizeof(BITMAPINFOHEADER)) { return false; }

		const BITMAPINFOHEADER* Header = reinterpret_cast<const BITMAPINFOHEADER*>(Dib);
		if (Header->biCompression != BI_RGB && Header->biCompression != BI_BITFIELDS) { return false; }
		if (Header->biBitCount != 24 && Header->biBitCount != 32) { return false; }

		const int32 Width  = static_cast<int32>(Header->biWidth);
		const int32 AbsH   = FMath::Abs(static_cast<int32>(Header->biHeight));
		const bool  bTopDown = Header->biHeight < 0;
		if (Width <= 0 || AbsH <= 0 || Width > 16384 || AbsH > 16384) { return false; }

		SIZE_T Offset = Header->biSize;
		if (Header->biCompression == BI_BITFIELDS) { Offset += 3 * sizeof(DWORD); }
		Offset += static_cast<SIZE_T>(Header->biClrUsed) * sizeof(RGBQUAD);
		if (Offset >= DibSize) { return false; }

		const int32 BytesPerPixel = Header->biBitCount / 8;
		const SIZE_T SrcStride = ((static_cast<SIZE_T>(Width) * BytesPerPixel + 3) / 4) * 4;
		if (Offset + SrcStride * AbsH > DibSize) { return false; }

		OutBGRA.SetNumUninitialized(Width * AbsH * 4);
		const uint8* Pixels = Dib + Offset;

		for (int32 Y = 0; Y < AbsH; ++Y)
		{
			const int32 SrcRow = bTopDown ? Y : (AbsH - 1 - Y);
			const uint8* Src = Pixels + SrcStride * SrcRow;
			uint8* Dst = OutBGRA.GetData() + static_cast<SIZE_T>(Y) * Width * 4;
			for (int32 X = 0; X < Width; ++X)
			{
				Dst[0] = Src[0];
				Dst[1] = Src[1];
				Dst[2] = Src[2];
				// A 32-bit DIB's fourth byte is usually unused rather than a real alpha;
				// treating it as alpha turns most pastes fully transparent.
				Dst[3] = 0xFF;
				Src += BytesPerPixel;
				Dst += 4;
			}
		}

		OutW = Width;
		OutH = AbsH;
		return true;
	}
}
#endif // PLATFORM_WINDOWS

bool FMCPChatAttachments::ClipboardHasImage()
{
#if PLATFORM_WINDOWS
	return ::IsClipboardFormatAvailable(CF_DIB) != 0;
#else
	// No portable engine API for clipboard bitmaps. Ctrl+V still pastes text; the
	// file dialog and drag-drop cover images on these platforms.
	return false;
#endif
}

FChatAttachmentPtr FMCPChatAttachments::FromClipboardImage()
{
#if PLATFORM_WINDOWS
	if (!::IsClipboardFormatAvailable(CF_DIB)) { return nullptr; }
	if (!::OpenClipboard(nullptr))             { return nullptr; }

	FChatAttachmentPtr Result;
	if (HANDLE Handle = ::GetClipboardData(CF_DIB))
	{
		if (const void* Locked = ::GlobalLock(Handle))
		{
			TArray<uint8> BGRA;
			int32 W = 0, H = 0;
			if (DibToBGRA(static_cast<const uint8*>(Locked), ::GlobalSize(Handle), BGRA, W, H))
			{
				TArray<uint8> Png;
				if (EncodePngFromBGRA(BGRA, W, H, Png))
				{
					Result = FromImageBytes(MoveTemp(Png), TEXT("pasted.png"));
				}
			}
			::GlobalUnlock(Handle);
		}
	}
	::CloseClipboard();
	return Result;
#else
	return nullptr;
#endif
}

// ============================================================================
// Commit
// ============================================================================

bool FMCPChatAttachments::Commit(TArray<FChatAttachmentPtr>& Attachments, const FGuid& SessionId)
{
	const FString Dir = FMCPChatStore::GetAttachmentsDirectory(SessionId);
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();

	bool bAllOk = true;
	for (const FChatAttachmentPtr& A : Attachments)
	{
		if (!A.IsValid() || A->IsContext() || !A->Error.IsEmpty()) { continue; }
		if (!A->StoredPath.IsEmpty()) { continue; }   // already committed

		if (!PF.DirectoryExists(*Dir) && !PF.CreateDirectoryTree(*Dir))
		{
			A->Error = LOCTEXT("NoAttachDir", "Could not create the attachments folder.");
			bAllOk = false;
			continue;
		}

		// Prefix with a short id: two files called screenshot.png in one conversation
		// is the normal case, not the exception.
		const FString Unique = FString::Printf(TEXT("%s_%s"),
			*A->Id.ToString(EGuidFormats::Digits).Left(8), *A->DisplayName);
		const FString Target = Dir / Unique;

		bool bWritten = false;
		if (A->PendingBytes.Num() > 0)
		{
			bWritten = FFileHelper::SaveArrayToFile(A->PendingBytes, *Target);
			if (bWritten) { A->PendingBytes.Empty(); }
		}
		else if (!A->AbsolutePath.IsEmpty())
		{
			bWritten = PF.CopyFile(*Target, *A->AbsolutePath);
		}

		if (bWritten)
		{
			A->StoredPath = Unique;
		}
		else
		{
			A->Error = FText::Format(LOCTEXT("AttachWriteFailed", "Could not store '{0}'."),
				FText::FromString(A->DisplayName));
			bAllOk = false;
		}
	}

	return bAllOk;
}

FChatContentBlock FMCPChatAttachments::ToContentBlock(const FChatAttachment& Attachment)
{
	FChatContentBlock B;

	if (Attachment.IsContext())
	{
		B.Type           = FChatContentBlock::EType::ContextRef;
		B.ContextKind    = FMCPChatContextResolver::KindToString(Attachment.ContextKind);
		B.ContextTarget  = Attachment.ContextTarget;
		B.bContextPinned = Attachment.bPinned;
		B.DisplayName    = Attachment.DisplayName;

		// Resolve here — this runs at send time, which is exactly the guarantee
		// @Selection depends on. A failure becomes a note the model can read rather
		// than silence it cannot.
		FString Resolved;
		FText   Error;
		if (FMCPChatContextResolver::Get().Resolve(Attachment.ContextKind, Attachment.ContextTarget, Resolved, Error))
		{
			B.Text = Resolved;
		}
		else
		{
			B.Text = FString::Printf(TEXT("[%s could not be resolved: %s]"),
				*Attachment.DisplayName, *Error.ToString());
		}
		return B;
	}

	B.Type        = Attachment.IsImage() ? FChatContentBlock::EType::Image : FChatContentBlock::EType::File;
	B.DisplayName = Attachment.DisplayName;
	B.MimeType    = Attachment.MimeType;
	B.StoredPath  = Attachment.StoredPath;
	B.SizeBytes   = Attachment.SizeBytes;

	// A text file's usefulness is its contents, so inline them; a binary's is its
	// existence, so name it and stop.
	if (B.Type == FChatContentBlock::EType::File && IsInlinableTextMime(Attachment.MimeType))
	{
		const FString Source = !Attachment.AbsolutePath.IsEmpty() ? Attachment.AbsolutePath : FString();
		FString Contents;
		if (!Source.IsEmpty() && FFileHelper::LoadFileToString(Contents, *Source))
		{
			bool bTruncated = false;
			if (Contents.Len() > MaxInlinedTextChars)
			{
				Contents.LeftInline(MaxInlinedTextChars);
				bTruncated = true;
			}
			B.Text = FString::Printf(TEXT("%s\n\n%s%s"),
				*Attachment.DisplayName, *Contents,
				bTruncated ? TEXT("\n\n… (truncated)") : TEXT(""));
		}
	}

	return B;
}

// ============================================================================
// Helpers
// ============================================================================

FString FMCPChatAttachments::GuessMimeType(const FString& Extension)
{
	const FString E = Extension.ToLower().Replace(TEXT("."), TEXT(""));

	if (E == TEXT("png"))  { return TEXT("image/png"); }
	if (E == TEXT("jpg") || E == TEXT("jpeg")) { return TEXT("image/jpeg"); }
	if (E == TEXT("gif"))  { return TEXT("image/gif"); }
	if (E == TEXT("bmp"))  { return TEXT("image/bmp"); }
	if (E == TEXT("webp")) { return TEXT("image/webp"); }
	if (E == TEXT("tga"))  { return TEXT("image/x-tga"); }
	if (E == TEXT("exr"))  { return TEXT("image/x-exr"); }

	if (E == TEXT("json")) { return TEXT("application/json"); }
	if (E == TEXT("md"))   { return TEXT("text/markdown"); }
	if (E == TEXT("txt") || E == TEXT("log") || E == TEXT("ini") || E == TEXT("csv")
		|| E == TEXT("xml") || E == TEXT("yaml") || E == TEXT("yml"))
	{
		return TEXT("text/plain");
	}
	if (E == TEXT("cpp") || E == TEXT("h") || E == TEXT("hpp") || E == TEXT("c")
		|| E == TEXT("cs") || E == TEXT("py") || E == TEXT("js") || E == TEXT("ts")
		|| E == TEXT("usf") || E == TEXT("ush") || E == TEXT("hlsl") || E == TEXT("build"))
	{
		return TEXT("text/x-source");
	}

	return TEXT("application/octet-stream");
}

bool FMCPChatAttachments::IsImageMime(const FString& MimeType)
{
	// EXR and TGA are image/* but no provider accepts them; treating them as images
	// would fail at the API instead of at attach time.
	return MimeType == TEXT("image/png")
		|| MimeType == TEXT("image/jpeg")
		|| MimeType == TEXT("image/gif")
		|| MimeType == TEXT("image/webp");
}

bool FMCPChatAttachments::IsInlinableTextMime(const FString& MimeType)
{
	return MimeType.StartsWith(TEXT("text/")) || MimeType == TEXT("application/json");
}

FString FMCPChatAttachments::FormatBytes(int64 Bytes)
{
	if (Bytes < 1024)              { return FString::Printf(TEXT("%lld B"), Bytes); }
	if (Bytes < 1024 * 1024)       { return FString::Printf(TEXT("%.1f KB"), Bytes / 1024.0); }
	if (Bytes < 1024ll * 1024 * 1024) { return FString::Printf(TEXT("%.1f MB"), Bytes / (1024.0 * 1024.0)); }
	return FString::Printf(TEXT("%.2f GB"), Bytes / (1024.0 * 1024.0 * 1024.0));
}

bool FMCPChatAttachments::ReadImageDimensions(const TArray<uint8>& Bytes, int32& OutWidth, int32& OutHeight)
{
	if (Bytes.Num() == 0) { return false; }

	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const EImageFormat Format = Module.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (Format == EImageFormat::Invalid) { return false; }

	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num())) { return false; }

	OutWidth  = Wrapper->GetWidth();
	OutHeight = Wrapper->GetHeight();
	return true;
}

bool FMCPChatAttachments::EncodePngFromBGRA(const TArray<uint8>& BGRA, int32 Width, int32 Height,
                                            TArray<uint8>& OutPng)
{
	if (Width <= 0 || Height <= 0 || BGRA.Num() < Width * Height * 4) { return false; }

	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid()) { return false; }

	if (!Wrapper->SetRaw(BGRA.GetData(), BGRA.Num(), Width, Height, ERGBFormat::BGRA, 8))
	{
		return false;
	}

	// GetCompressed returns a TArray64 — a screenshot is nowhere near 2 GB, but the
	// types do not convert implicitly, so copy rather than assign.
	const TArray64<uint8> Compressed = Wrapper->GetCompressed(100);
	if (Compressed.Num() == 0) { return false; }

	OutPng.SetNumUninitialized(static_cast<int32>(Compressed.Num()));
	FMemory::Memcpy(OutPng.GetData(), Compressed.GetData(), Compressed.Num());
	return true;
}

#undef LOCTEXT_NAMESPACE
