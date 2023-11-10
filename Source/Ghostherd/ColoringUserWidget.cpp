


#include "ColoringUserWidget.h"

#include "Kismet/KismetSystemLibrary.h"


void UColoringUserWidget::GetFormatedImageData(UTexture2D* TargetTexture2D)
{
	MyTexture2D = TargetTexture2D;
	TextureCompressionSettings OldCompressionSettings = MyTexture2D->CompressionSettings; 
	TextureMipGenSettings OldMipGenSettings = MyTexture2D->MipGenSettings; 
	bool OldSRGB = MyTexture2D->SRGB;

	MyTexture2D->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
	MyTexture2D->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
	MyTexture2D->SRGB = false;
	MyTexture2D->UpdateResource();

	UColoringUserWidget::FormatedImageData = static_cast<const FColor*>( MyTexture2D->PlatformData->Mips[0].BulkData.LockReadOnly());

	MyTexture2D->PlatformData->Mips[0].BulkData.Unlock();

	MyTexture2D->CompressionSettings = OldCompressionSettings;
	MyTexture2D->MipGenSettings = OldMipGenSettings;
	MyTexture2D->SRGB = OldSRGB;
	MyTexture2D->UpdateResource();
}

FColor UColoringUserWidget::SampleTexByPixelXY(int32 PixelX, int32 PixelY)
{
	FColor PixelColor = FormatedImageData[PixelY * MyTexture2D->GetSizeX() + PixelX];
	return PixelColor;
}

FName UColoringUserWidget::GetColoringParamNameByFloat(float AlphaFloat)
{
	FString ParamStr = "Color";
	int32 Id = 0;

	if(AlphaFloat <= 4.0f) Id = 0;
	else if(AlphaFloat <= 12.0f) Id = 1;
	else if(AlphaFloat <= 20.0f) Id = 2;
	else if(AlphaFloat <= 30.0f) Id = 3;
	else if(AlphaFloat <= 42.0f) Id = 4;
	else if(AlphaFloat <= 55.0f) Id = 5;
	else if(AlphaFloat <= 69.0f) Id = 6;
	else if(AlphaFloat <= 84.0f) Id = 7;
	else if(AlphaFloat <= 101.0f) Id = 8;
	else if(AlphaFloat <= 118.0f) Id = 9;
	else if(AlphaFloat <= 136.0f) Id = 10;
	else if(AlphaFloat <= 156.0f) Id = 11;
	else if(AlphaFloat <= 176.0f) Id = 12;
	else if(AlphaFloat <= 198.0f) Id = 13;
	else if(AlphaFloat <= 220.0f) Id = 14;
	else if(AlphaFloat <= 244.0f) Id = 15;
	else Id = 16;

	ParamStr = ParamStr.Append(FString::FromInt(Id));
	return FName(ParamStr);
}

void UColoringUserWidget::ColoringTargetImageByPixelXY(UImage* TargetImage, int32 PixelX, int32 PixelY,
	FLinearColor TintingColor)
{
	FColor SampleColor = UColoringUserWidget::SampleTexByPixelXY(PixelX, PixelY);
	FName ParamName = GetColoringParamNameByFloat(float(SampleColor.A));
	TargetImage->GetDynamicMaterial()->SetVectorParameterValue(ParamName,TintingColor);
}


