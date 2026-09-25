// Copyright StraySpark Studio 2026. All Rights Reserved.

#include "Tools/Spatial/SpatialCommon.h"
#include "Common/MCPActorResolver.h"

#include "MCPToolRegistry.h"
#include "MCPProtocol.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/Engine.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace MCPSpatialTools::Common
{

UWorld* GetEditorWorld()
{
	if (GEditor)
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

AActor* FindActorByLabel(UWorld* World, const FString& Label)
{
	// v4 Phase 1: cached resolver (O(1) amortized) replaces the per-call actor scan.
	return MCPCommon::FindActorByLabel(World, Label);
}

TSharedPtr<FJsonObject> MakeVectorJson(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

ECollisionChannel ParseTraceChannel(const FString& ChannelName)
{
	if (ChannelName == TEXT("Visibility")) return ECC_Visibility;
	if (ChannelName == TEXT("Camera")) return ECC_Camera;
	if (ChannelName == TEXT("WorldStatic")) return ECC_WorldStatic;
	if (ChannelName == TEXT("WorldDynamic")) return ECC_WorldDynamic;
	if (ChannelName == TEXT("Pawn")) return ECC_Pawn;
	if (ChannelName == TEXT("PhysicsBody")) return ECC_PhysicsBody;
	return ECC_Visibility;
}

} // namespace MCPSpatialTools::Common
