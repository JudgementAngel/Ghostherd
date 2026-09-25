// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "UI/SChatImageBlock.h"
#include "MCPChatStyle.h"
#include "UnrealMCPChatModule.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SChatImageBlock"

namespace
{
	/** Refuse absurd images rather than allocating hundreds of MB of VRAM for a
	 *  chat attachment. */
	constexpr int32 MaxImageDimension = 8192;
	constexpr int64 MaxImageBytes = 64ll * 1024 * 1024;
}

void SChatImageBlock::Construct(const FArguments& InArgs)
{
	Source          = InArgs._Source;
	AltText         = InArgs._AltText;
	MaxDisplayWidth = InArgs._MaxDisplayWidth;
	AliveFlag       = MakeShared<TAtomic<bool>>(true);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBox)
			.Visibility(this, &SChatImageBlock::GetImageVisibility)
			.WidthOverride_Lambda([this]() { return GetDisplaySize().X; })
			.HeightOverride_Lambda([this]() { return GetDisplaySize().Y; })
			[
				SNew(SImage)
				.Image(this, &SChatImageBlock::GetImageBrush)
			]
		]

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.TextStyle(&FMCPChatStyle::TextStyle(TEXT("Chat.Text.Small")))
			.Text(this, &SChatImageBlock::GetPlaceholderText)
			.Visibility(this, &SChatImageBlock::GetPlaceholderVisibility)
			.AutoWrapText(true)
		]
	];

	BeginLoad();
}

SChatImageBlock::~SChatImageBlock()
{
	// A decode task may still be in flight; tell it not to touch us.
	if (AliveFlag.IsValid())
	{
		AliveFlag->Store(false, EMemoryOrder::Relaxed);
	}
}

void SChatImageBlock::AddReferencedObjects(FReferenceCollector& Collector)
{
	// THE line that prevents the "random crash minutes later" failure: the brush
	// holds Texture as an untracked resource pointer, so without this the GC frees
	// it while the renderer is still drawing from it.
	Collector.AddReferencedObject(Texture);
}

// ============================================================================
// Loading
// ============================================================================

void SChatImageBlock::BeginLoad()
{
	TSharedPtr<TAtomic<bool>> Alive = AliveFlag;
	const FString SourceCopy = Source;

	// Weak self: the widget can be destroyed while the decode runs.
	TWeakPtr<SChatImageBlock> WeakSelf = StaticCastSharedRef<SChatImageBlock>(AsShared());

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [WeakSelf, Alive, SourceCopy]()
	{
		TArray<uint8> Compressed;
		FString Failure;

		if (SourceCopy.StartsWith(TEXT("data:")))
		{
			// data:image/png;base64,XXXX
			int32 CommaIdx = INDEX_NONE;
			if (SourceCopy.FindChar(TEXT(','), CommaIdx) && SourceCopy.Contains(TEXT("base64")))
			{
				if (!FBase64::Decode(SourceCopy.Mid(CommaIdx + 1), Compressed))
				{
					Failure = TEXT("data URI is not valid base64");
				}
			}
			else
			{
				Failure = TEXT("unsupported data URI (expected base64)");
			}
		}
		else if (SourceCopy.StartsWith(TEXT("http://")) || SourceCopy.StartsWith(TEXT("https://")))
		{
			// Deliberate: a transcript must not silently make network requests.
			// Phase 6 adds an explicit "load remote image" affordance.
			Failure = TEXT("remote images are not loaded automatically");
		}
		else if (!FFileHelper::LoadFileToArray(Compressed, *SourceCopy))
		{
			Failure = TEXT("file not found");
		}

		if (Failure.IsEmpty() && Compressed.Num() > MaxImageBytes)
		{
			Failure = TEXT("image is too large");
		}

		TArray<uint8> RawBGRA;
		int32 Width = 0, Height = 0;

		if (Failure.IsEmpty())
		{
			IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			const EImageFormat Format = Module.DetectImageFormat(Compressed.GetData(), Compressed.Num());

			if (Format == EImageFormat::Invalid)
			{
				Failure = TEXT("unrecognised image format");
			}
			else
			{
				const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(Format);
				if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()))
				{
					Failure = TEXT("could not decode image");
				}
				else if (Wrapper->GetWidth() > MaxImageDimension || Wrapper->GetHeight() > MaxImageDimension)
				{
					Failure = TEXT("image dimensions are too large");
				}
				else if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawBGRA))
				{
					Failure = TEXT("could not read image pixels");
				}
				else
				{
					Width  = Wrapper->GetWidth();
					Height = Wrapper->GetHeight();
				}
			}
		}

		// Texture creation is game-thread only.
		AsyncTask(ENamedThreads::GameThread,
			[WeakSelf, Alive, RawBGRA = MoveTemp(RawBGRA), Width, Height, Failure]()
		{
			if (!Alive.IsValid() || !Alive->Load(EMemoryOrder::Relaxed)) { return; }

			const TSharedPtr<SChatImageBlock> Self = WeakSelf.Pin();
			if (!Self.IsValid()) { return; }

			if (!Failure.IsEmpty() || Width <= 0 || Height <= 0)
			{
				Self->State = EState::Failed;
				Self->FailureReason = Failure.IsEmpty() ? TEXT("unknown error") : Failure;
				Self->Invalidate(EInvalidateWidgetReason::Layout);
				return;
			}

			Self->ApplyDecodedImage(RawBGRA, Width, Height);
		});
	});
}

void SChatImageBlock::ApplyDecodedImage(const TArray<uint8>& RawBGRA, int32 Width, int32 Height)
{
	check(IsInGameThread());

	Texture = UTexture2D::CreateTransient(Width, Height, PF_B8G8R8A8);
	if (!Texture)
	{
		State = EState::Failed;
		FailureReason = TEXT("could not create texture");
		return;
	}

	Texture->SRGB = true;
	Texture->CompressionSettings = TC_EditorIcon;   // no DXT for a UI thumbnail

	void* Dest = Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, RawBGRA.GetData(), RawBGRA.Num());
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();

	NativeWidth  = Width;
	NativeHeight = Height;

	// Deferred cleanup so the render thread is never left holding a freed brush.
	Brush = FDeferredCleanupSlateBrush::CreateBrush(Texture);

	State = EState::Ready;
	Invalidate(EInvalidateWidgetReason::Layout);
}

// ============================================================================
// Rendering
// ============================================================================

const FSlateBrush* SChatImageBlock::GetImageBrush() const
{
	return Brush.IsValid() ? Brush->GetSlateBrush() : FStyleDefaults::GetNoBrush();
}

FVector2D SChatImageBlock::GetDisplaySize() const
{
	if (NativeWidth <= 0 || NativeHeight <= 0)
	{
		return FVector2D::ZeroVector;
	}

	// Scale down to fit the column; never scale up — an upscaled screenshot looks
	// broken and hides detail the model may be referring to.
	const float Scale = FMath::Min(1.f, MaxDisplayWidth / static_cast<float>(NativeWidth));
	return FVector2D(NativeWidth * Scale, NativeHeight * Scale);
}

EVisibility SChatImageBlock::GetImageVisibility() const
{
	return (State == EState::Ready) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SChatImageBlock::GetPlaceholderVisibility() const
{
	return (State == EState::Ready) ? EVisibility::Collapsed : EVisibility::Visible;
}

FText SChatImageBlock::GetPlaceholderText() const
{
	const FString Label = AltText.IsEmpty() ? FPaths::GetCleanFilename(Source) : AltText;

	if (State == EState::Loading)
	{
		return FText::Format(LOCTEXT("Loading", "🖼 {0} — loading…"), FText::FromString(Label));
	}
	return FText::Format(LOCTEXT("Failed", "🖼 {0} — {1}"),
		FText::FromString(Label), FText::FromString(FailureReason));
}

#undef LOCTEXT_NAMESPACE
