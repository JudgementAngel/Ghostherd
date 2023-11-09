

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ColoringUserWidget.generated.h"

/**
 * 
 */
UCLASS()
class GHOSTHERD_API UColoringUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	const FColor* FormatedImageData;
	UTexture2D* MyTexture2D;
	
	UFUNCTION(BlueprintCallable,Category="UI Coloring")
	void GetFormatedImageData(UTexture2D* MyTexture2D);
	
	UFUNCTION(BlueprintCallable,Category="UI Coloring")
	FColor SampleTexByPixelXY(int32 PixelX ,int32 PixelY);
};
