#include "Camera/VBEnvironmentCameraPolicy.h"

namespace VBEnvironmentCameraPolicy
{
	int32 SelectCandidate(TConstArrayView<FCandidate> Candidates, int32 Previous, double SwitchMargin)
	{
		int32 Best = INDEX_NONE;
		for (int32 I = 0; I < Candidates.Num(); ++I)
		{
			const auto& C = Candidates[I];
			if (C.bSafe && !C.Position.ContainsNaN() && FMath::IsFinite(C.Score) &&
				(Best == INDEX_NONE || C.Score < Candidates[Best].Score)) Best = I;
		}
		if (Best != INDEX_NONE && Candidates.IsValidIndex(Previous))
		{
			const auto& Old = Candidates[Previous];
			if (Old.bSafe && !Old.Position.ContainsNaN() && FMath::IsFinite(Old.Score) &&
				Old.Score <= Candidates[Best].Score + FMath::Max(0.0, SwitchMargin)) return Previous;
		}
		return Best;
	}

	double RecoveryAlpha(double DeltaTime, double HalfLife)
	{
		if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0) return 0;
		if (!FMath::IsFinite(HalfLife) || HalfLife <= 0) return 1;
		return 1.0 - FMath::Exp2(-DeltaTime / HalfLife);
	}

	double FRecovery::Update(double SafeFraction, double DeltaTime, double HalfLife, double HoldSeconds, bool bHasClearance)
	{
		if (!FMath::IsFinite(SafeFraction)) { Reset(); Fraction = 0; return Fraction; }
		SafeFraction = FMath::Clamp(SafeFraction, 0.0, 1.0);
		if (SafeFraction < Fraction)
		{
			Fraction = SafeFraction;
			ClearTime = 0;
		}
		else if (!bHasClearance)
		{
			ClearTime = 0;
		}
		else
		{
			const double Dt = FMath::IsFinite(DeltaTime) ? FMath::Max(0.0, DeltaTime) : 0;
			const double PreviousTime = ClearTime;
			ClearTime += Dt;
			// Only the portion of this frame after the hold contributes; independent of sample rate.
			const double RecoveryDt = FMath::Max(0.0, ClearTime - FMath::Max(PreviousTime, HoldSeconds));
			Fraction = FMath::Lerp(Fraction, SafeFraction, RecoveryAlpha(RecoveryDt, HalfLife));
			if (FMath::Abs(Fraction - SafeFraction) < 0.0001) Fraction = SafeFraction;
		}
		return Fraction;
	}
}
