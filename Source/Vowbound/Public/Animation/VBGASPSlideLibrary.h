#pragma once

#include "CoreMinimal.h"
#include "Animation/TrajectoryTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPSlideLibrary.generated.h"

class UAnimInstance;

UCLASS()
class VOWBOUND_API UVBGASPSlideLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void InitializeMoverPredictor(UAnimInstance* Instance);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP")
	static void Update_PropertiesFromCharacter(UAnimInstance* Instance);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void GenerateSlideTrajectory(UAnimInstance* Instance, float InDeltaTime,
		const FTransformTrajectory& InOutTrajectory, float InOutDesiredControllerYawLastUpdate,
		FTransformTrajectory& OutTrajectory, float InHistorySamplingInterval = 0.033f,
		int32 InTrajectoryHistoryCount = 15, float InPredictionSamplingInterval = 0.1f,
		int32 InTrajectoryPredictionCount = 15);

	static bool HasEnteredSlide(const UAnimInstance* Instance);
	static bool HasCompletedSlideExit(const UAnimInstance* Instance);
};
