// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBMatchState.generated.h"

UENUM(BlueprintType)
enum class EVBMatchPhase : uint8
{
	None UMETA(DisplayName = "None"),
	Warmup UMETA(DisplayName = "Warmup"),
	InProgress UMETA(DisplayName = "In Progress"),
	PostMatch UMETA(DisplayName = "Post Match")
};