// Copyright StraySpark Studio 2026. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UTexture2D;
class FDeferredCleanupSlateBrush;

/**
 * Phase 3 — an image inside a message.
 *
 * GOTCHA G1 (docs/02_ARCHITECTURE.md §9), handled here in two parts because one
 * alone is not enough:
 *
 *  1. FDeferredCleanupSlateBrush defers destruction of the BRUSH until the
 *     renderer has finished with it — without this, freeing a brush the render
 *     thread still references crashes.
 *  2. FGCObject roots the TEXTURE. The brush holds the UTexture2D only as a
 *     resource pointer, which is NOT a GC reference, so a transient texture is
 *     collected out from under a live brush on the next GC. This is the part
 *     people miss; the crash then looks random and appears minutes later.
 *
 * Decoding happens off the game thread; texture creation is marshalled back on.
 */
class SChatImageBlock : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SChatImageBlock)
		: _MaxDisplayWidth(560.f)
	{}
		/** Absolute path on disk, or a data: URI. */
		SLATE_ARGUMENT(FString, Source)
		SLATE_ARGUMENT(FString, AltText)
		SLATE_ARGUMENT(float, MaxDisplayWidth)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SChatImageBlock() override;

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SChatImageBlock"); }
	//~ End FGCObject

private:
	void BeginLoad();
	void ApplyDecodedImage(const TArray<uint8>& RawBGRA, int32 Width, int32 Height);

	const FSlateBrush* GetImageBrush() const;
	FVector2D          GetDisplaySize() const;
	EVisibility        GetImageVisibility() const;
	EVisibility        GetPlaceholderVisibility() const;
	FText              GetPlaceholderText() const;

	FString Source;
	FString AltText;
	float   MaxDisplayWidth = 560.f;

	/** Rooted through AddReferencedObjects — see the class comment. */
	TObjectPtr<UTexture2D> Texture;
	TSharedPtr<FDeferredCleanupSlateBrush> Brush;

	int32 NativeWidth = 0;
	int32 NativeHeight = 0;

	enum class EState : uint8 { Loading, Ready, Failed } State = EState::Loading;
	FString FailureReason;

	/** Guards against a decode completing after the widget is gone. */
	TSharedPtr<TAtomic<bool>> AliveFlag;
};
