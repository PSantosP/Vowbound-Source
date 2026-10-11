#pragma once

#include "CoreMinimal.h"

namespace VBEnvironmentCameraPolicy
{
	struct FCandidate
	{
		FVector Position = FVector::ZeroVector;
		double Score = 0;
		bool bSafe = false;
	};
	/** Invalid candidates cannot win, regardless of score. Retain a valid candidate until meaningfully better. */
	VOWBOUND_API int32 SelectCandidate(TConstArrayView<FCandidate> Candidates, int32 Previous, double SwitchMargin);
	VOWBOUND_API double RecoveryAlpha(double DeltaTime, double HalfLife);
	/** Fraction of the desired anchor-to-camera distance. Inward changes are immediate. */
	struct VOWBOUND_API FRecovery
	{
		double Fraction = 1;
		double ClearTime = 0;
		void Reset() { Fraction = 1; ClearTime = 0; }
		double Update(double SafeFraction, double DeltaTime, double HalfLife, double HoldSeconds, bool bHasClearance);
	};
}
