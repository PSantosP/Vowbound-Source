#include "Misc/AutomationTest.h"
#include "Animation/AnimNode_VBMuteEffects.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/AnimationInstanceScope.h"

// 숨은 갈래 노티파이 거르기(설계 .sw/slide-sprint-resume/fix-design-claude.md 5차). 그래프 안 전달(링크 그래프·캐시 포즈를 건너는지)은
// 엔진 경로라 여기서 못 본다 - 실제 슬라이드 표본(01)이 그 몫이다. 여기서는 소비처가 보는 판정과 메시지가 만드는 표시가 맞물리는지만 본다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMuteEffectsTest, "Vowbound.Unit.Animation.MuteEffects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMuteEffectsTest::RunTest(const FString& Parameters)
{
	const FAnimNotifyEventReference Plain;
	TestFalse(TEXT("a notify without context data plays"), VBAnimEffects::IsMuted(Plain));

	FAnimNotifyEventReference Other;
	Other.AddContextData<UE::Anim::FAnimNotifyAssetPlayerInstanceContext>(7u);
	TestFalse(TEXT("an unrelated engine context does not mute"), VBAnimEffects::IsMuted(Other));

	const TUniquePtr<const UE::Anim::IAnimNotifyEventContextDataInterface> Made = FVBMuteEffectsScope().MakeUniqueEventContextData();
	TestTrue(TEXT("the hidden-branch message makes the context the consumers check"), Made && Made->Is<FVBMutedEffectsNotifyContext>());

	FAnimNotifyEventReference Hidden;
	Hidden.AddContextData<UE::Anim::FAnimNotifyAssetPlayerInstanceContext>(7u);
	Hidden.AddContextData<FVBMutedEffectsNotifyContext>();
	TestTrue(TEXT("a notify carrying the hidden-branch context is muted among other contexts"), VBAnimEffects::IsMuted(Hidden));
	return true;
}
#endif
