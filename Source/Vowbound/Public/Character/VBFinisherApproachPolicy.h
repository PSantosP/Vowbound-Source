#pragma once

#include "CoreMinimal.h"
#include "Data/VBFinisherConfig.h"

namespace VBFinisherApproachPolicy
{
inline bool Matches(EVBFinisherApproach Approach, const FVector& AttackerLocation,
                    const FVector& VictimLocation, const FVector& VictimForward)
{
	if (AttackerLocation.ContainsNaN() || VictimLocation.ContainsNaN() || VictimForward.ContainsNaN()) return false;
	const FVector ToAttacker = (AttackerLocation - VictimLocation).GetSafeNormal2D();
	const FVector Forward = VictimForward.GetSafeNormal2D();
	if (ToAttacker.IsNearlyZero() || Forward.IsNearlyZero()) return false;
	const bool bFront = FVector::DotProduct(Forward, ToAttacker) >= 0.0f;
	return bFront ? Approach == EVBFinisherApproach::Front : Approach == EVBFinisherApproach::Back;
}
}
