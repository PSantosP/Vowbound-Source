#pragma once
#include "CoreMinimal.h"

// 입력/선택의 순수 정책. 네트워크나 viewport 없이 경계값을 검증한다.
namespace VBTargetLockPolicy
{
// 명중 연타와 직접 시점 입력은 보정 시간을 연장하지 않는다.
struct FHitRecenterWindow
{
	double LastHitTime = -DBL_MAX;
	double LastInputTime = -DBL_MAX;
	double StartTime = 0.0;
	double EndTime = 0.0;
	bool bActive = false;
	void Cancel() { bActive = false; }
	void RecordInput(double Now) { LastInputTime = Now; Cancel(); }
	bool RegisterHit(double Now, float QuietSeconds, float InputGraceSeconds)
	{
		const bool bQuiet = Now - LastHitTime >= FMath::Max(0.0f, QuietSeconds);
		LastHitTime = Now;
		return !bActive && bQuiet && Now - LastInputTime >= FMath::Max(0.0f, InputGraceSeconds);
	}
	void Start(double Now, float Duration)
	{
		StartTime = Now;
		EndTime = Now + FMath::Max(0.0f, Duration);
		bActive = EndTime > StartTime;
	}
	double GetAlpha(double Now) const
	{
		if (EndTime <= StartTime) return 1.0;
		const double T = FMath::Clamp((Now - StartTime) / (EndTime - StartTime), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
};
inline bool IsHitNotificationFresh(double Now, double HitTime, float MaxAge)
{
	// 작은 시각 동기화 오차는 허용하되 크게 어긋난 미래/과거 알림은 버린다.
	return FMath::IsFinite(Now) && FMath::IsFinite(HitTime) && FMath::Abs(Now - HitTime) <= FMath::Max(0.0f, MaxAge);
}
inline bool IsInRecenterSafeFrame(const FVector2D& Screen, const FVector2D& HalfExtent)
{
	return !Screen.ContainsNaN() && FMath::Abs(Screen.X - 0.5) <= FMath::Clamp(HalfExtent.X, 0.0, 0.5) &&
		FMath::Abs(Screen.Y - 0.5) <= FMath::Clamp(HalfExtent.Y, 0.0, 0.5);
}
struct FStickLatch
{
	int32 HeldDirection = 0;
	void Reset() { HeldDirection = 0; }
	int32 Update(float Axis, float PressThreshold, float ReleaseThreshold)
	{
		if (!FMath::IsFinite(Axis)) return 0;
		const float Release = FMath::Clamp(ReleaseThreshold, 0.0f, 0.99f);
		const float Press = FMath::Clamp(PressThreshold, Release + 0.01f, 1.0f);
		if (FMath::Abs(Axis) <= Release) { Reset(); return 0; }
		const int32 Direction = Axis > 0.0f ? 1 : -1;
		if (FMath::Abs(Axis) >= Press && HeldDirection != Direction)
		{
			HeldDirection = Direction;
			return Direction;
		}
		return 0;
	}
};
inline bool IsWithinDistance(double DistanceSquared, float Limit)
{
	return FMath::IsFinite(DistanceSquared) && Limit >= 0.0f && DistanceSquared <= FMath::Square(double(Limit));
}
inline double ScreenScore(const FVector2D& Origin, const FVector2D& Candidate, float VerticalWeight)
{
	const FVector2D Delta = Candidate - Origin;
	return FMath::Square(Delta.X) + FMath::Square(Delta.Y * FMath::Max(0.0f, VerticalWeight));
}
inline bool IsOnSide(const FVector2D& Origin, const FVector2D& Candidate, float Direction, float MinHorizontalDelta)
{
	return (Candidate.X - Origin.X) * FMath::Sign(Direction) > FMath::Max(0.0f, MinHorizontalDelta);
}
}
