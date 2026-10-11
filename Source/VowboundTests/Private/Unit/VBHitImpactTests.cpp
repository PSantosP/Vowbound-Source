#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBHitImpact.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Abilities/GameplayAbilityTypes.h" // FGameplayEventData (밀린 방향 운반 시험)
#include <limits>

namespace
{
	// TestEqual 에 enum class 를 넘기면 템플릿 경로가 값을 문자열로 못 바꿀 수 있어 서수로 비교한다(실패 메시지도 숫자가 읽기 쉽다).
	int32 ImpactOrd(EVBHitImpact Impact) { return static_cast<int32>(Impact); }
}

// 반응 등급 해석(DEC-025). 허브가 이 함수 하나로 «얼마나 세게 맞았나» 를 정하므로, 경계가 틀리면 모든 피격 반응이 한 칸씩 어긋난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitImpactResolveTest, "Vowbound.Combat.HitImpact.Resolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitImpactResolveTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("저항 0 은 그대로"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::Heavy, 0.0f)), ImpactOrd(EVBHitImpact::Heavy));
	TestEqual(TEXT("저항 1 은 한 단계"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::Heavy, 1.0f)), ImpactOrd(EVBHitImpact::Light));
	TestEqual(TEXT("띄움이 한 단계 깎이면 넘어뜨림"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::Launch, 1.0f)), ImpactOrd(EVBHitImpact::Knockdown));
	TestEqual(TEXT("보스 저항 2 - 넘어뜨림은 가벼움"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::Knockdown, 2.0f)), ImpactOrd(EVBHitImpact::Light));
	TestEqual(TEXT("깎을 것이 없으면 없음"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::Light, 2.0f)), ImpactOrd(EVBHitImpact::None));
	TestEqual(TEXT("없음 아래는 없다"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::None, 5.0f)), ImpactOrd(EVBHitImpact::None));
	TestEqual(TEXT("속성 오차 1.9999 는 2 단계"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::Launch, 1.9999f)), ImpactOrd(EVBHitImpact::Heavy));
	TestEqual(TEXT("음수 저항은 올리지 않는다"), ImpactOrd(VBResolveHitImpact(EVBHitImpact::Light, -3.0f)), ImpactOrd(EVBHitImpact::Light));
	return true;
}

// SetByCaller 운반. 실리지 않은 스펙(비근접 피해)이 가벼움으로 읽혀야 현행이 보존된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitImpactMagnitudeTest, "Vowbound.Combat.HitImpact.Magnitude",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitImpactMagnitudeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("실리지 않음(0)은 가벼움"), ImpactOrd(VBHitImpactFromMagnitude(0.0f)), ImpactOrd(EVBHitImpact::Light));
	for (const EVBHitImpact Impact : {EVBHitImpact::Light, EVBHitImpact::Heavy, EVBHitImpact::Knockdown, EVBHitImpact::Launch})
	{
		TestEqual(TEXT("서수 왕복"), ImpactOrd(VBHitImpactFromMagnitude(static_cast<float>(Impact))), ImpactOrd(Impact));
	}
	TestEqual(TEXT("범위 위는 가벼움"), ImpactOrd(VBHitImpactFromMagnitude(99.0f)), ImpactOrd(EVBHitImpact::Light));
	TestEqual(TEXT("NaN 은 가벼움"), ImpactOrd(VBHitImpactFromMagnitude(std::numeric_limits<float>::quiet_NaN())), ImpactOrd(EVBHitImpact::Light));
	return true;
}

// 허브 -> 반응 GA 운반. «태그 없음(옛 경로) = 가벼움» 과 «없음 태그 = 반응 없음» 이 갈려야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitImpactTagTest, "Vowbound.Combat.HitImpact.Tags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitImpactTagTest::RunTest(const FString& Parameters)
{
	for (const EVBHitImpact Impact : {EVBHitImpact::None, EVBHitImpact::Light, EVBHitImpact::Heavy, EVBHitImpact::Knockdown, EVBHitImpact::Launch})
	{
		const FGameplayTag Tag = VBHitImpactToTag(Impact);
		TestTrue(TEXT("모든 등급은 자기 태그가 있다"), Tag.IsValid());
		FGameplayTagContainer Tags;
		Tags.AddTag(Tag);
		TestEqual(TEXT("태그 왕복"), ImpactOrd(VBHitImpactFromTags(Tags)), ImpactOrd(Impact));
	}
	TestEqual(TEXT("태그 없음은 가벼움"), ImpactOrd(VBHitImpactFromTags(FGameplayTagContainer())), ImpactOrd(EVBHitImpact::Light));

	// 잘못 둘이 실려도 센 쪽으로 읽는다 - 약한 쪽으로 떨어지면 «넘어뜨렸는데 플린치» 가 된다.
	FGameplayTagContainer Both;
	Both.AddTag(VBGameplayTags::Combat_Impact_Light);
	Both.AddTag(VBGameplayTags::Combat_Impact_Knockdown);
	TestEqual(TEXT("둘이면 센 쪽"), ImpactOrd(VBHitImpactFromTags(Both)), ImpactOrd(EVBHitImpact::Knockdown));
	return true;
}

// 허브 -> 반응 GA 의 «밀린 방향» 운반. 넉백·피격 클립 이동·넘어지는 쪽이 같은 값을 써야 한 방향이 된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitImpactPushYawTest, "Vowbound.Combat.HitImpact.PushYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBHitImpactPushYawTest::RunTest(const FString& Parameters)
{
	FGameplayEventData Empty;
	float Yaw = 123.0f;
	TestFalse(TEXT("실리지 않은 이벤트는 false"), VBGetPushYaw(Empty, Yaw));
	TestEqual(TEXT("false 일 때 출력은 건드리지 않는다"), Yaw, 123.0f);

	for (const float In : {0.0f, 90.0f, -135.0f, 179.0f})
	{
		FGameplayEventData Event;
		VBSetPushYaw(Event, In);
		float Out = 0.0f;
		TestTrue(TEXT("실은 이벤트는 true"), VBGetPushYaw(Event, Out));
		TestTrue(TEXT("요 왕복(각도 동치)"), FMath::IsNearlyZero(FMath::FindDeltaAngleDegrees(In, Out), 0.01f));
	}
	return true;
}
#endif
