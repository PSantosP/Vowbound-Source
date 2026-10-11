#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeReference.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "BoneControllers/AnimNode_OrientationWarping.h"
#include "BoneControllers/AnimNode_FootPlacement.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "VBGASPLocomotionLibrary.generated.h"

class UAnimInstance;

/** Native decisions behind the project GASP graph. Names match original BP overrides. */
UCLASS(meta=(BlueprintThreadSafe))
class VOWBOUND_API UVBGASPLocomotionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(NotBlueprintThreadSafe))
	static int32 ConfigureFootPlacementPelvis(UAnimInstance* Instance, float MaxOffset, float LinearStiffness);

	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool IsMoving(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool IsStarting(UAnimInstance* Instance);
	// 피벗 판정. 원판정(IsPivotingRaw)에 «콘텐츠 없는 피벗 대체 래치» 를 건다 - 래치 중에는 거짓(FVBGASPCMCState::bPivotFallbackLatched).
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool IsPivoting(UAnimInstance* Instance);
	// GASP 원본과 같은 피벗 원판정(궤적 꺾임·속도 범위). 래치를 풀 때와 대체가 피벗 때문인지 볼 때 쓴다.
	static bool IsPivotingRaw(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool ShouldTurnInPlace(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool ShouldSpinTransition(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool JustTraversed(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool JustLanded_Light(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool JustLanded_Heavy(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_LandVelocity(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_TrajectoryTurnAngle(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_MMBlendTime(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static EPoseSearchInterruptMode Get_MMInterruptMode(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_MMNotifyRecencyTimeOut(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static FVector2D Get_AOValue(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_AO_Yaw(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool Enable_AO(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static FVector CalculateRelativeAccelerationAmount(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static FVector2D Get_LeanAmount(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static EOffsetRootBoneMode Get_OffsetRootRotationMode(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static EOffsetRootBoneMode Get_OffsetRootTranslationMode(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_OffsetRootTranslationHalfLife(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_OffsetRootTranslationRadius(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static EOrientationWarpingSpace Get_OrientationWarpingWarpingSpace(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool AllowFootPinning(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static FFootPlacementPlantSettings Get_FootPlacementPlantSettings(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static FFootPlacementInterpolationSettings Get_FootPlacementInterpolationSettings(UAnimInstance* Instance);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static bool EnableSteering(UAnimInstance* Instance, const FAnimNodeReference& Node);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static double Get_DynamicPlayRate(UAnimInstance* Instance, const FAnimNodeReference& BlendStackInput);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP") static FQuat Get_DesiredFacing(UAnimInstance* Instance);
};
