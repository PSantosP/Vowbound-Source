#include "Misc/AutomationTest.h"
#include "Animation/VBGASPSlideLibrary.h"
#include "Data/VBMovementConfig.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"

// 슬라이드 경계 안전(f3 verify 47c1e578 의 슬라이드 몫). 설계 .sw/slide-sprint-resume/fix-design-claude.md 3차.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSlideSafetyTest, "Vowbound.Unit.Slide.Safety",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSlideSafetyTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no slide graph instance has not entered a slide"), UVBGASPSlideLibrary::HasEnteredSlide(nullptr));
	TestFalse(TEXT("no slide graph instance has not finished an exit"), UVBGASPSlideLibrary::HasCompletedSlideExit(nullptr));

#if WITH_EDITOR
	UVBMovementConfig* Config = NewObject<UVBMovementConfig>(GetTransientPackage());
	{
		FDataValidationContext Context;
		TestEqual(TEXT("default slide speeds (start 380 > end 200) are valid"), Config->IsDataValid(Context), EDataValidationResult::Valid);
	}
	// 끝 = 시작: 시작 프레임에 끝나는 설정 - 저작 때 막는다.
	for (const float End : { Config->SlideStartSpeed, Config->SlideStartSpeed + 50.f })
	{
		Config->SlideEndSpeed = End;
		FDataValidationContext Context;
		TestEqual(FString::Printf(TEXT("end speed %.0f >= start %.0f is invalid"), End, Config->SlideStartSpeed),
			Config->IsDataValid(Context), EDataValidationResult::Invalid);
	}
#endif
	return true;
}
#endif
