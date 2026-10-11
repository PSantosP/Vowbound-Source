#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "VBEnvironmentCameraConfig.generated.h"

/** Local camera comfort settings. Collision safety is never interpolated past an obstacle. */
UCLASS(BlueprintType)
class VOWBOUND_API UVBEnvironmentCameraConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Camera") bool bEnabled = true;
	UPROPERTY(EditAnywhere, Category="Collision") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Camera;
	UPROPERTY(EditAnywhere, Category="Collision", meta=(ClampMin="1", Units="cm")) float MinimumProbeRadius = 12.f;
	UPROPERTY(EditAnywhere, Category="Collision", meta=(ClampMin="0", Units="cm")) float SurfaceMargin = 2.f;
	UPROPERTY(EditAnywhere, Category="Collision", meta=(ClampMin="0", ClampMax="1")) float AnchorHeightFraction = 0.55f;
	UPROPERTY(EditAnywhere, Category="Composition", meta=(ClampMin="0", Units="cm")) float MaxLateralAdjustment = 15.f;
	UPROPERTY(EditAnywhere, Category="Composition", meta=(ClampMin="0", Units="cm")) float MaxHeightAdjustment = 10.f;
	UPROPERTY(EditAnywhere, Category="Composition", meta=(ClampMin="0")) float VisibilityPenalty = 75.f;
	UPROPERTY(EditAnywhere, Category="Composition", meta=(ClampMin="0")) float CandidateSwitchMargin = 8.f;
	UPROPERTY(EditAnywhere, Category="Recovery", meta=(ClampMin="0", Units="s")) float ClearHoldSeconds = 0.2f;
	UPROPERTY(EditAnywhere, Category="Recovery", meta=(ClampMin="0.001", Units="s")) float RecoveryHalfLife = 0.2f;
	UPROPERTY(EditAnywhere, Category="Recovery", meta=(ClampMin="0", Units="cm")) float ClearDistanceMargin = 5.f;
	UPROPERTY(EditAnywhere, Category="Recovery", meta=(ClampMin="1", Units="cm")) float TeleportResetDistance = 200.f;
};
