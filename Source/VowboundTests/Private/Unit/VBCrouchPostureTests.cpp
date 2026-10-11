#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGA_Crouch.h"
#include "AbilitySystem/Abilities/VBGA_Dodge.h"
#include "Abilities/GameplayAbility.h"

// 자세 관문의 단위 시험. 설계 .sw/crouch-montage 2.1·2.3.
// 관문은 태그 묶음 하나가 홈이고, 앉기 능력의 막기 태그와 입력 관문(AVBCharacter::HandleCrouchPressed)이 같은 묶음을 쓴다.
// 묶음에서 태그 하나가 빠지면 그 동작 도중 C 가 «서기 몽타주 + 앉은 캡슐» 을 다시 만든다 - 그것이 조용히 일어나지 않게 여기서 센다.
// 실제 전환(캡슐·몽타주)은 PIE 가 본다.

#if WITH_AUTOMATION_TESTS
namespace
{
	// 능력의 태그 칸은 보호 칸이라 시험에서만 리플렉션으로 읽는다(VBKeyBindingTests 의 입력 액션 칸과 같은 방법).
	const FGameplayTagContainer* AbilityTags(const UGameplayAbility* Ability, const TCHAR* PropertyName)
	{
		const FStructProperty* Property = FindFProperty<FStructProperty>(UGameplayAbility::StaticClass(), PropertyName);
		return Property && Ability ? Property->ContainerPtrToValuePtr<FGameplayTagContainer>(Ability) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCrouchPostureTagsTest, "Vowbound.Unit.Crouch.PostureTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCrouchPostureTagsTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer& Posture = UVBGA_Crouch::GetPostureBlockingTags();
	// 몸통을 쓰는 동작 전부. 하나라도 빠지면 그 동작 도중의 C 가 다시 깨진다(감사 2026-10-03).
	const TArray<FGameplayTag> Required = {
		VBGameplayTags::State_Combat_Attacking, VBGameplayTags::State_Combat_Finishing, VBGameplayTags::State_Combat_Guarding,
		VBGameplayTags::State_Combat_HitReacting, VBGameplayTags::State_Combat_Dodging, VBGameplayTags::State_Combat_Staggered,
		VBGameplayTags::State_Combat_Downed, VBGameplayTags::State_Combat_GettingUp, VBGameplayTags::State_Weapon_Summoning,
		VBGameplayTags::State_Weapon_Sheathing, VBGameplayTags::State_Dead };
	for (const FGameplayTag& Tag : Required)
	{
		TestTrue(FString::Printf(TEXT("posture set has %s"), *Tag.ToString()), Tag.IsValid() && Posture.HasTagExact(Tag));
	}
	TestEqual(TEXT("posture set is exactly the required tags"), Posture.Num(), Required.Num());
	// 질주는 막는 것이 아니라 미끄러지기다 - 묶음에 들어가면 질주 + C 의 미끄러지기 길이 입력 관문에서 막힌다.
	TestFalse(TEXT("sprinting is not a posture block (sprint + C = slide)"), Posture.HasTagExact(VBGameplayTags::State_Locomotion_Sprinting));

	// 앉기 능력은 묶음 전부 + 질주를 막는다. 대조: 묶음이 비어 있으면 아래 HasAllExact 는 거짓 통과하므로 위의 수 검사가 먼저다.
	const FGameplayTagContainer* CrouchBlocked = AbilityTags(GetDefault<UVBGA_Crouch>(), TEXT("ActivationBlockedTags"));
	if (!TestNotNull(TEXT("crouch ActivationBlockedTags readable"), CrouchBlocked))
	{
		return false;
	}
	TestTrue(TEXT("crouch ability blocks every posture tag"), CrouchBlocked->HasAllExact(Posture));
	TestTrue(TEXT("crouch ability still blocks sprinting"), CrouchBlocked->HasTagExact(VBGameplayTags::State_Locomotion_Sprinting));

	// 회피는 앉기를 끝내고 시작한다(설계 2.3).
	const FGameplayTagContainer* DodgeCancels = AbilityTags(GetDefault<UVBGA_Dodge>(), TEXT("CancelAbilitiesWithTag"));
	if (!TestNotNull(TEXT("dodge CancelAbilitiesWithTag readable"), DodgeCancels))
	{
		return false;
	}
	TestTrue(TEXT("dodge cancels the crouch ability"), DodgeCancels->HasTagExact(VBGameplayTags::Ability_Locomotion_Crouch));
	return true;
}
#endif
