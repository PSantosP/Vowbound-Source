#include "Misc/AutomationTest.h"
#include "Animation/VBLadderIKContact.h"

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderContactTest, "Vowbound.Unit.LadderIK.ContactLock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderContactTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40; Geometry.Drop = 10;
	FVBLadderIKContact State;
	const float Dt = 1.0f / 60.0f;
	FVector Raw(8, 20, 151);
	State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestFalse(TEXT("first sample cannot classify a moving contact"), State.bPlanted);
	State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestFalse(TEXT("one slow swing-apex frame must not plant"), State.bPlanted);
	for (int32 Frame = 0; Frame < 12; ++Frame) State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestTrue(TEXT("stationary contact plants"), State.bPlanted);
	TestEqual(TEXT("planted weight reaches one"), State.Weight, 1.0f);
	const FVector Anchor = State.Anchor;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Raw.Y += 0.02; Raw.Z += 0.05;
		State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	}
	TestTrue(TEXT("raw drift cannot move the planted rung or lateral anchor"), State.Anchor.Equals(Anchor));
	Raw.Z += 2;
	State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestFalse(TEXT("reach releases the old rung"), State.bPlanted);
	TestTrue(TEXT("release fades on the same target"), State.Anchor.Equals(Anchor));
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Raw.Z += 2;
		State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	}
	TestEqual(TEXT("swing becomes fully uncorrected"), State.Weight, 0.0f);
	for (int32 Frame = 0; Frame < 12; ++Frame) State.Update(Geometry, Raw, Dt, 0, 15, 40, 0.08f);
	TestTrue(TEXT("next hold plants a new rung"), State.Anchor.Z > Anchor.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderPoseDepthTest, "Vowbound.Unit.LadderIK.PoseDepthAndCapture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderPoseDepthTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40; Geometry.Drop = 10;
	const float Dt = 1.0f / 60.0f;
	const FVector Source(-25.9, 20, 72);
	FVBLadderIKContact FarContact;
	for (int32 Frame = 0; Frame < 60; ++Frame)
	{
		FarContact.Update(Geometry, Source, Dt, 0, 15, 40, 0.08f, 0.06f, 12);
	}
	TestFalse(TEXT("a slow foot 26cm behind the rung must not acquire contact"), FarContact.bPlanted);
	TestEqual(TEXT("time far from the rung does not count as contact dwell"), FarContact.QuietTime, 0.0f);

	const FVector PoseOffset = Geometry.GetPoseDepthOffset(63.9f, 38.0f);
	TestTrue(TEXT("pose adaptation aligns the source support plane"), FMath::IsNearlyZero((Source + PoseOffset).X, 0.001));
	for (int32 Frame = 0; Frame < 20; ++Frame)
	{
		FarContact.Update(Geometry, Source + PoseOffset, Dt, 0, 15, 40, 0.08f, 0.06f, 12);
	}
	TestTrue(TEXT("the adapted nearby support pose acquires contact"), FarContact.bPlanted);
	const FVector Anchor = FarContact.Anchor;
	for (int32 Frame = 1; Frame <= 12; ++Frame)
	{
		FarContact.Update(Geometry, Source + PoseOffset + FVector(0, 0, Frame * 2), Dt,
			0, 15, 40, 0.08f, 0.06f, 12);
	}
	TestFalse(TEXT("swing releases contact"), FarContact.bPlanted);
	TestEqual(TEXT("contact fade finishes during swing"), FarContact.Weight, 0.0f);
	TestTrue(TEXT("release does not move the old anchor"), FarContact.Anchor.Equals(Anchor));
	TestTrue(TEXT("pose depth adaptation survives release"), Geometry.GetPoseDepthOffset(63.9f, 38.0f).Equals(PoseOffset));

	Geometry.ToWorld = FTransform(FRotator(0, 90, 0), FVector(100, 200, 0), FVector(2, 1, 1));
	Geometry.Depth = 5;
	const FVector RotatedOffset = Geometry.GetPoseDepthOffset(63.9f, 38.0f);
	TestTrue(TEXT("world support distance accounts for rotated and scaled rung depth"),
		RotatedOffset.Equals(FVector(0, 15.9, 0), 0.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderHandContactTest, "Vowbound.Unit.LadderIK.HandContactTransition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderHandContactTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 40.255f; Geometry.Drop = 9.6f;
	FVBLadderIKContact Hand;
	const float Dt = 1.0f / 60.0f;
	const auto UpdateHand = [&](const FVector& Raw, float Step)
	{
		Hand.Update(Geometry, Raw, Step, 0, 30, 40, 0.08f, 0, 36, true, 6);
	};
	// 14:23 로그: 같은 봉 부근의 앞뒤 정렬을 3D 속도로 해제했던 재현 구간.
	const FVector Initial(-7.478, 28.138, 225.497);
	for (int32 Frame = 0; Frame < 10; ++Frame) UpdateHand(Initial, Dt);
	TestTrue(TEXT("initial hand support is planted"), Hand.bPlanted);
	const FVector Anchor = Hand.Anchor;
	const FVector Samples[] = {
		FVector(-7.650, 28.048, 226.279), FVector(-6.459, 28.338, 226.251),
		FVector(-3.529, 28.805, 227.084), FVector(3.004, 28.624, 230.547),
		FVector(0.145, 28.658, 230.568), FVector(-5.154, 28.476, 229.575),
		FVector(-7.405, 28.269, 229.140) };
	const float Steps[] = { 0.249f, 0.062f, 0.046f, 0.108f, 0.106f, 0.110f, 0.111f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Samples); ++Index)
	{
		UpdateHand(Samples[Index], Steps[Index]);
		TestTrue(TEXT("same-rung alignment keeps the hand planted"), Hand.bPlanted);
		TestTrue(TEXT("same-rung alignment preserves the anchor"), Hand.Anchor.Equals(Anchor));
		TestEqual(TEXT("same-rung alignment does not fade IK"), Hand.Weight, 1.0f);
	}
	// 축 이동이 6cm를 넘고 빠르게 다음 봉으로 이동하면 실제로 놓는다.
	UpdateHand(FVector(-7.405, 28.269, 235.140), Dt);
	TestFalse(TEXT("real upward reach releases the hand"), Hand.bPlanted);
	for (int32 Frame = 1; Frame <= 12; ++Frame)
	{
		UpdateHand(FVector(-7.405, 28.269, 235.140 + Frame * 3), Dt);
		TestFalse(TEXT("fast passage through an intermediate rung cannot plant"), Hand.bPlanted);
	}
	TestEqual(TEXT("swing releases all contact weight"), Hand.Weight, 0.0f);
	// 앞뒤 속도가 높아도 축 이동이 착지 속도로 줄면 정지 대기 없이 새 봉을 잡기 시작한다.
	UpdateHand(FVector(-5.405, 28.269, 271.140 + 20 * Dt), Dt);
	TestTrue(TEXT("arrival starts IK before waiting for full 3D stillness"), Hand.bPlanted);
	TestTrue(TEXT("arrival starts the blend immediately"), Hand.Weight > 0.0f);
	TestTrue(TEXT("arrival selects a higher rung"), Hand.Anchor.Z > Anchor.Z);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLadderGeometryTest, "Vowbound.Unit.LadderIK.VisibleRungs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLadderGeometryTest::RunTest(const FString& Parameters)
{
	FVBLadderIKGeometry Geometry;
	Geometry.Height = 400; Geometry.Width = 66; Geometry.Spacing = 20; Geometry.Drop = 9.6f;
	Geometry.Depth = 5;
	FVector Result;
	TestTrue(TEXT("lower boundary has an actual rung"), Geometry.FindContact(FVector(0, 100, -20), 0, Result));
	TestTrue(TEXT("never invent a rung below the base"), Result.Z >= 0);
	TestEqual(TEXT("contact stays within bar width"), Result.Y, 33.0);
	TestEqual(TEXT("bar depth matches mesh"), Result.X, -5.0);
	Geometry.FindContact(FVector(0, 0, 600), 0, Result);
	TestTrue(TEXT("never invent a rung above the top"), Result.Z <= 400);
	Geometry.FindContact(FVector(0, 0, 55.5), 5.14f, Result);
	TestTrue(TEXT("subdivision rung plus foot clearance"), FMath::IsNearlyEqual(Result.Z, 55.54, 0.001));
	Geometry.Height = 1;
	TestFalse(TEXT("no visual rung means no contact"), Geometry.FindContact(FVector::ZeroVector, 0, Result));
	return true;
}
#endif
