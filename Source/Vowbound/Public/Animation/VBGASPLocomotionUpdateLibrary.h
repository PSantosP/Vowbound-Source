#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimExecutionContext.h"
#include "Animation/AnimNodeReference.h"
#include "VBGASPLocomotionUpdateLibrary.generated.h"

class UAnimInstance;
class UAnimSequence;
class UChooserTable;
struct FVBGSlopeSettings;
struct FPoseSearchTrajectoryData;
struct FTransformTrajectory;

/** Native updates for the original GASP Chooser context and authored AnimGraph. */
UCLASS()
class VOWBOUND_API UVBGASPLocomotionUpdateLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// 호스트/링크드 그래프가 같은 이동 의도 궤적을 쓴다. 실제 CMC 속도는 수정하지 않는다.
	static void GenerateLocomotionTrajectory(const UObject* Context, const FPoseSearchTrajectoryData& Data,
		float DeltaSeconds, bool bSuppressAttackMotion, FTransformTrajectory& History, float& LastYaw,
		FTransformTrajectory& Generated);
	static FVector CalculateCMCSlopeInput(float PitchDegrees, bool bEnabled, const FVBGSlopeSettings& Settings);
	static void Update_CMCSlope(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void Update_PropertiesFromCharacter(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void Update_CVarDrivenVariables(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_Trajectory(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_EssentialValues(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_States(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_MovementDirection(UAnimInstance* Instance);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_TargetRotation(UAnimInstance* Instance, UAnimSequence* StrafeCurve);
	UFUNCTION(BlueprintPure, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static double Get_StrafeYawRotationOffset(UAnimInstance* Instance, UAnimSequence* StrafeCurve);
	UFUNCTION(BlueprintPure, CustomThunk, Category="Vowbound|GASP", meta=(BlueprintThreadSafe, CustomStructureParam="ReturnValue"))
	static void Get_MovementDirectionThresholds(UAnimInstance* Instance, int32& ReturnValue);
	DECLARE_FUNCTION(execGet_MovementDirectionThresholds);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_Logic(UAnimInstance* Instance, UAnimSequence* StrafeCurve);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void BlueprintThreadSafeUpdateAnimation(UAnimInstance* Instance, float DeltaTime, UAnimSequence* StrafeCurve);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_MotionMatching(UAnimInstance* Instance, const FAnimUpdateContext& Context, const FAnimNodeReference& Node, UChooserTable* Chooser);
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void Update_MotionMatching_PostSelection(UAnimInstance* Instance, const FAnimUpdateContext& Context, const FAnimNodeReference& Node);

};
