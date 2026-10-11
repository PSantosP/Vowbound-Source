#pragma once

#include "CoreMinimal.h"
#include "VBSlideMovementSettings.generated.h"

USTRUCT(BlueprintType)
struct VOWBOUND_API FVBSlideMovementSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float InitialBoostTime = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float InitialBoostSpeed = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float InitialBoostAcceleration = 2000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float AfterBoostAcceleration = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0", ClampMax="89"))
	float ShallowSlopeAngle = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0", ClampMax="89"))
	float SteepSlopeAngle = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float ShallowSlopeSpeed = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float SteepSlopeSpeed = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float FlatGroundSpeed = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float FlatGroundDeceleration = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Slide", meta=(ClampMin="0"))
	float SteepSlopeDeceleration = 2000.f;

	float GetTargetSpeed(float SlopeAngle, float Elapsed) const
	{
		if (Elapsed < InitialBoostTime) return InitialBoostSpeed;
		return SlopeAngle > -ShallowSlopeAngle ? FlatGroundSpeed : FMath::GetMappedRangeValueClamped(
			FVector2D(-ShallowSlopeAngle, -SteepSlopeAngle), FVector2D(ShallowSlopeSpeed, SteepSlopeSpeed), SlopeAngle);
	}

	float GetDeceleration(float SlopeAngle) const
	{
		return FMath::GetMappedRangeValueClamped(FVector2D(ShallowSlopeAngle, SteepSlopeAngle),
			FVector2D(FlatGroundDeceleration, SteepSlopeDeceleration), SlopeAngle);
	}
};
