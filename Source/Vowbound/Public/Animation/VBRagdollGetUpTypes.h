#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "PoseSearch/PoseSearchHistory.h"
#include "VBRagdollGetUpTypes.generated.h"

// Native equivalents of GASP S_CHT_GetUpMontages_IN / OUT.
USTRUCT(BlueprintType)
struct FVBRagdollGetUpInputs
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TEnumAsByte<EMovementMode> MovementMode = MOVE_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool RollingGetup = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FPoseHistoryReference PoseHistory;
};

USTRUCT(BlueprintType)
struct FVBRagdollGetUpOutputs
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) double MontageStartTime = 0.0;
};
