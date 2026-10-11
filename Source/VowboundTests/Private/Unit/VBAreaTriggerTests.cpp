#include "Misc/AutomationTest.h"
#include "Narrative/Story/VBAreaTrigger.h"

// 영역 진입의 발화 판정. 설계 .sw/prologue-opening 3.3·8.3절.
// 겹침·사실·복원 순서는 PIE 가 보고, 여기는 «만족이 거짓 -> 참일 때만, 한 번만이면 처음만» 이라는 판정 자체를 잰다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAreaTriggerShouldFireTest, "Vowbound.Unit.Narrative.AreaTriggerShouldFire",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAreaTriggerShouldFireTest::RunTest(const FString& Parameters)
{
	using VBAreaTrigger::ShouldFire;

	// 반복 영역: 들어올 때마다.
	TestTrue(TEXT("enter fires"), ShouldFire(false, true, false, false));
	TestFalse(TEXT("staying inside does not fire again"), ShouldFire(true, true, false, false));
	TestFalse(TEXT("leaving does not fire"), ShouldFire(true, false, false, false));
	TestFalse(TEXT("outside stays quiet"), ShouldFire(false, false, false, false));

	// 한 번만 영역: 사실이 켜져 있으면 다시 안 한다(이어하기 뒤 포함).
	TestTrue(TEXT("once fires the first time"), ShouldFire(false, true, true, false));
	TestFalse(TEXT("once does not fire after the fact is on"), ShouldFire(false, true, true, true));

	// 안에서 조건이 참이 되는 경우도 거짓 -> 참이므로 발화한다(무기를 영역 안에서 주운 경우).
	TestTrue(TEXT("conditions turning true while inside fire"), ShouldFire(false, true, false, false));
	return true;
}
#endif
