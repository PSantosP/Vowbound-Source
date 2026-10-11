#pragma once
#include "CoreMinimal.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "SmartObjectRuntime.h"
#include "VBGASPSmartObjectTypes.generated.h"
class UAnimMontage;

USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPSmartObjectSelectionInputs
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double TargetDistance = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double TargetAngle = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FPoseHistoryReference PoseHistoryNode;
};
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPSmartObjectSelectionOutputs
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double Cost = TNumericLimits<double>::Max();
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double StartTime = 0.0;
};
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPSmartObjectPayload
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FSmartObjectSlotHandle Slot;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<AActor> Actor;
};
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPSmartObjectAnimation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UAnimMontage> MontageToPlay;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PlayTime = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RandomPlaytimeVariance = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float StartTime = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PlayRate = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NumLoops = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform WarpTargetTransform;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bUseWarpTarget = false;
	UPROPERTY() uint32 Serial = 0;
	UPROPERTY() double ServerStartTime = 0.0;
	UPROPERTY() uint8 SupersededGASMontageId = 0;
};
