


#include "ColoringUserWidget.h"

#include "Kismet/KismetSystemLibrary.h"

FColor UColoringUserWidget::SampleTexByPixelXY(int32 PixelX, int32 PixelY)
{
	FColor PixelColor = FormatedImageData[PixelY * MyTexture2D->GetSizeX() + PixelX];
	return PixelColor;
}

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
