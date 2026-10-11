#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Character/VBTargetLockPolicy.h"
#include "Data/VBTargetLockConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputCoreTypes.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockInputPolicyTest, "Vowbound.LockOn.InputLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLockInputPolicyTest::RunTest(const FString& Parameters)
{
	VBTargetLockPolicy::FStickLatch Latch;
	TestEqual(TEXT("Below press does not switch"), Latch.Update(0.6f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Cross press switches once"), Latch.Update(0.7f, 0.65f, 0.25f), 1);
	for (int32 Frame = 0; Frame < 120; ++Frame)
		TestEqual(TEXT("Holding cannot repeat"), Latch.Update(1.0f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Noise above release keeps latch"), Latch.Update(0.3f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Still held after noise"), Latch.Update(0.9f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Neutral rearms"), Latch.Update(0.2f, 0.65f, 0.25f), 0);
	TestEqual(TEXT("Second gesture switches"), Latch.Update(0.9f, 0.65f, 0.25f), 1);
	TestEqual(TEXT("Crossing neutral between samples can reverse"), Latch.Update(-0.9f, 0.65f, 0.25f), -1);
	TestEqual(TEXT("NaN cannot switch"), Latch.Update(std::numeric_limits<float>::quiet_NaN(), 0.65f, 0.25f), 0);
	Latch.Reset();
	TestEqual(TEXT("Device reset rearms"), Latch.Update(-0.9f, 0.65f, 0.25f), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockSelectionPolicyTest, "Vowbound.LockOn.SelectionBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLockSelectionPolicyTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	TestFalse(TEXT("Beyond acquisition distance"), IsWithinDistance(FMath::Square(2200.0), 2000.0f));
	TestTrue(TEXT("Same target remains within retention distance"), IsWithinDistance(FMath::Square(2200.0), 2500.0f));
	TestTrue(TEXT("Exact retention boundary"), IsWithinDistance(FMath::Square(2500.0), 2500.0f));
	TestFalse(TEXT("Past retention boundary"), IsWithinDistance(FMath::Square(2501.0), 2500.0f));
	const FVector2D A(0.35, 0.5), B(0.55, 0.5), C(0.80, 0.5);
	TestTrue(TEXT("Screen neighbour precedes farther screen target"), ScreenScore(A, B, 1.5f) < ScreenScore(A, C, 1.5f));
	TestTrue(TEXT("Right of current, even on left half of screen"), IsOnSide(FVector2D(0.2,0.5), FVector2D(0.3,0.5), 1, 0.015f));
	TestFalse(TEXT("Wrong direction excluded"), IsOnSide(B, A, 1, 0.015f));
	TestFalse(TEXT("Vertically overlapping target excluded"), IsOnSide(A, FVector2D(A.X, 0.8), 1, 0.015f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLockAssetPolicyTest, "Vowbound.LockOn.AuthoredInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBLockAssetPolicyTest::RunTest(const FString& Parameters)
{
	const UVBTargetLockConfig* Config = LoadObject<UVBTargetLockConfig>(nullptr, TEXT("/Game/Vowbound/Data/DA_TargetLock_Default.DA_TargetLock_Default"));
	if (!TestNotNull(TEXT("Lock config"), Config)) return false;
	TestFalse(TEXT("Ordinary hit does not create persistent lock"), Config->bAutoLockOnHit);
	TestTrue(TEXT("Hit recenter enabled independently"), Config->bRecenterCameraOnHit);
	TestTrue(TEXT("Recenter ends before next permitted burst"), Config->HitRecenterDuration > 0.0f && Config->HitRecenterDuration < Config->HitRecenterQuietSeconds);
	TestTrue(TEXT("Release threshold below press"), Config->StickSwitchReleaseThreshold < Config->StickSwitchPressThreshold);
	TestTrue(TEXT("Retention wider than acquisition"), Config->LockBreakDistance > Config->MaxLockDistance);
	const UInputAction* Action = LoadObject<UInputAction>(nullptr, TEXT("/Game/Vowbound/Input/Actions/IA_SwitchTarget.IA_SwitchTarget"));
	if (!TestNotNull(TEXT("Switch action"), Action)) return false;
	TestFalse(TEXT("Switch axis cannot consume free look"), Action->bConsumeInput);
	TestEqual(TEXT("Signed direction type"), Action->ValueType, EInputActionValueType::Axis1D);
	for (const TCHAR* ContextName : {TEXT("IMC_KeyboardMouse"), TEXT("IMC_Gamepad")})
	{
		const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr,
			*FString::Printf(TEXT("/Game/Vowbound/Input/Contexts/%s.%s"), ContextName, ContextName));
		if (!TestNotNull(ContextName, Context)) continue;
		const bool bPad = FString(ContextName) == TEXT("IMC_Gamepad");
		int32 Count = 0;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action != Action) continue;
			++Count;
			TestEqual(TEXT("No Pressed trigger hides analog neutral updates"), Mapping.Triggers.Num(), 0);
			if (bPad)
			{
				TestEqual(TEXT("Unscaled stick X"), Mapping.Key, EKeys::Gamepad_RightX);
				TestEqual(TEXT("No shared look sensitivity modifier"), Mapping.Modifiers.Num(), 0);
			}
			else
			{
				TestTrue(TEXT("Wheel only"), Mapping.Key == EKeys::MouseScrollUp || Mapping.Key == EKeys::MouseScrollDown);
				if (Mapping.Key == EKeys::MouseScrollDown)
					TestTrue(TEXT("Wheel down supplies negative direction"), Mapping.Modifiers.Num() == 1 && Mapping.Modifiers[0]->IsA<UInputModifierNegate>());
			}
		}
		TestEqual(TEXT("Exactly one pad axis or two wheel directions"), Count, bPad ? 1 : 2);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitRecenterWindowTest, "Vowbound.LockOn.HitRecenterWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitRecenterWindowTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	FHitRecenterWindow Window;
	TestTrue(TEXT("First hit after idle can assist"), Window.RegisterHit(10.0, 0.8f, 0.35f));
	Window.Start(10.0, 0.25f);
	TestEqual(TEXT("No initial snap"), Window.GetAlpha(10.0), 0.0);
	TestEqual(TEXT("Halfway easing"), Window.GetAlpha(10.125), 0.5);
	TestFalse(TEXT("Another hit cannot restart active assist"), Window.RegisterHit(10.1, 0.8f, 0.35f));
	TestEqual(TEXT("End time unchanged by hit"), Window.EndTime, 10.25);
	TestEqual(TEXT("Hitch cannot overshoot endpoint"), Window.GetAlpha(11.0), 1.0);
	Window.Cancel();
	for (int32 Hit = 1; Hit <= 8; ++Hit)
		TestFalse(TEXT("Continuous combo cannot reacquire camera"), Window.RegisterHit(10.1 + Hit * 0.3, 0.8f, 0.35f));
	TestTrue(TEXT("Quiet gap rearms a new burst"), Window.RegisterHit(13.4, 0.8f, 0.35f));
	Window.Start(13.4, 0.25f);
	Window.RecordInput(13.45);
	TestFalse(TEXT("Manual input immediately cancels"), Window.bActive);
	TestFalse(TEXT("Late hit during input grace cannot restart"), Window.RegisterHit(13.5, 0.0f, 0.35f));
	TestFalse(TEXT("Cancellation does not resume by time passing"), Window.bActive);
	TestTrue(TEXT("New burst after input and hit silence allowed"), Window.RegisterHit(14.4, 0.8f, 0.35f));
	Window.Start(14.4, 0.0f);
	TestFalse(TEXT("Zero duration never owns camera"), Window.bActive);
	TestEqual(TEXT("Zero duration has finite progress"), Window.GetAlpha(14.4), 1.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitRecenterVisibilityTest, "Vowbound.LockOn.HitRecenterBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitRecenterVisibilityTest::RunTest(const FString& Parameters)
{
	using namespace VBTargetLockPolicy;
	const FVector2D Half(0.25, 0.30);
	TestTrue(TEXT("Central target needs no recenter"), IsInRecenterSafeFrame(FVector2D(0.5,0.5), Half));
	TestTrue(TEXT("Safe frame edge included"), IsInRecenterSafeFrame(FVector2D(0.75,0.5), Half));
	TestFalse(TEXT("Side target may need correction"), IsInRecenterSafeFrame(FVector2D(0.9,0.5), Half));
	TestFalse(TEXT("Vertical edge target may need correction"), IsInRecenterSafeFrame(FVector2D(0.5,0.95), Half));
	TestFalse(TEXT("Offscreen target is not safe"), IsInRecenterSafeFrame(FVector2D(1.1,0.5), Half));
	TestTrue(TEXT("Recent server hit accepted"), IsHitNotificationFresh(10.1, 10.0, 0.35f));
	TestTrue(TEXT("Small server clock skew tolerated"), IsHitNotificationFresh(9.95, 10.0, 0.35f));
	TestFalse(TEXT("Old packet cannot turn camera"), IsHitNotificationFresh(11.0, 10.0, 0.35f));
	TestFalse(TEXT("Unsynchronised future packet ignored"), IsHitNotificationFresh(9.0, 10.0, 0.35f));
	TestFalse(TEXT("Invalid timestamp ignored"), IsHitNotificationFresh(10.0, std::numeric_limits<double>::quiet_NaN(), 0.35f));
	return true;
}
#endif
