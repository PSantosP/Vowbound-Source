#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Data/VBWeaponVisualConfig.h"
#include "Animation/AnimMontage.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponVisualTimingTest, "Vowbound.Weapons.VisualTiming.Windows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBWeaponVisualTimingTest::RunTest(const FString& Parameters)
{
	FVBWeaponVisualTiming Timing;
	Timing.DissolveStart = 0.2f;
	Timing.DissolveEnd = 0.4f;
	Timing.FormationStart = 0.8f;
	Timing.FormationEnd = 1.2f;
	TestTrue(TEXT("Ordered windows fit montage"), Timing.IsValid(1.5f));
	TestEqual(TEXT("Outgoing sword stays during approach"), Timing.GetOutgoingProgress(0.1f), 1.0f);
	TestEqual(TEXT("Incoming sword is absent during sheathing"), Timing.GetIncomingProgress(0.3f), 0.0f);
	TestTrue(TEXT("Outgoing sword dissolves partway"), FMath::IsNearlyEqual(Timing.GetOutgoingProgress(0.3f), 0.5f, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Outgoing sword is gone at boundary"), Timing.GetOutgoingProgress(0.4f), 0.0f);
	TestEqual(TEXT("Empty interval has no outgoing sword"), Timing.GetOutgoingProgress(0.6f), 0.0f);
	TestEqual(TEXT("Empty interval has no incoming sword"), Timing.GetIncomingProgress(0.6f), 0.0f);
	TestTrue(TEXT("Drawing forms the new sword"), FMath::IsNearlyEqual(Timing.GetIncomingProgress(1.0f), 0.5f, KINDA_SMALL_NUMBER));
	TestEqual(TEXT("New sword completes at boundary"), Timing.GetIncomingProgress(1.2f), 1.0f);
	// 절대 위치 평가이므로 두 구간을 한 프레임에 건너뛰어도 누락된 Notify에 의존하지 않는다.
	TestEqual(TEXT("Seek across both windows removes old sword"), Timing.GetOutgoingProgress(1.4f), 0.0f);
	TestEqual(TEXT("Seek across both windows completes new sword"), Timing.GetIncomingProgress(1.4f), 1.0f);
	TestTrue(TEXT("Partly formed sword never jumps to full on rechange"),
		FMath::IsNearlyEqual(0.3f * Timing.GetOutgoingProgress(0.1f), 0.3f));
	TestFalse(TEXT("Window past montage end is rejected"), Timing.IsValid(1.0f));
	Timing.FormationStart = 0.3f;
	TestFalse(TEXT("Overlapping windows are rejected"), Timing.IsValid(1.5f));
	Timing.FormationStart = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Nonfinite window is rejected"), Timing.IsValid(1.5f));
	TestEqual(TEXT("Zero duration before boundary stays hidden"), FVBWeaponVisualTiming::EvaluateWindow(0.1f, 0.2f, 0.2f), 0.0f);
	TestEqual(TEXT("Zero duration at boundary is an explicit cut"), FVBWeaponVisualTiming::EvaluateWindow(0.2f, 0.2f, 0.2f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponVisualTimingAssetTest, "Vowbound.Weapons.VisualTiming.AuthoredAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBWeaponVisualTimingAssetTest::RunTest(const FString& Parameters)
{
	const UVBWeaponVisualConfig* Config = LoadObject<UVBWeaponVisualConfig>(nullptr, TEXT("/Game/Vowbound/Weapons/DA_WeaponVisuals.DA_WeaponVisuals"));
	if (!TestNotNull(TEXT("Weapon visual config loads"), Config)) return false;
	TestTrue(TEXT("Timing data was authored and saved"), Config->MontageTimings.Num() > 0);
	for (const auto& Pair : Config->MontageTimings)
	{
		if (!TestNotNull(TEXT("Timing montage reference is valid"), Pair.Key.Get())) continue;
		TestTrue(*FString::Printf(TEXT("%s windows fit its playback duration"), *Pair.Key->GetName()), Pair.Value.IsValid(Pair.Key->GetPlayLength()));
	}
	return true;
}
#endif
