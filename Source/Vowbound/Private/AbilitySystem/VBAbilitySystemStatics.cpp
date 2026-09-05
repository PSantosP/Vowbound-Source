// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayAbilitySpec.h"          // FScopedAbilityListLock / FGameplayAbilitySpec
#include "GenericTeamAgentInterface.h"    // FGenericTeamId::GetAttitude
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h" // InputTag 의 홈

bool UVBAbilitySystemStatics::IsDead(const UAbilitySystemComponent* ASC)
{
	// State.Dead 태그 문자열이 코드에 등장하는 유일한 지점이다. 사망 의미가 바뀌면 여기만 고친다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead);
}

bool UVBAbilitySystemStatics::IsAttacking(const UAbilitySystemComponent* ASC)
{
	// State.Combat.Attacking 질의가 코드에 등장하는 유일한 지점이다.
	// 태그 자체는 GA 들이 ActivationOwnedTags 로 부여하고, 읽는 쪽은 전부 여기를 거친다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
}

bool UVBAbilitySystemStatics::IsTelegraphing(const UAbilitySystemComponent* ASC)
{
	// 태그를 붙이고 떼는 곳은 UVBGA_EnemyMeleeAttack 하나다. 예고 시작에 붙고, 떼는 지점은 둘이다 -
	//   정상 진행의 BeginSwing 과, 취소·경직으로 예고 도중 끝날 때의 EndAbility.
	// 읽는 쪽은 여기를 거친다.
	return ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Telegraphing);
}

bool UVBAbilitySystemStatics::IsActorDead(const AActor* Actor)
{
	// 획득과 판정을 한 줄로 잇는다. 획득만 여기서 하고 판정은 위 함수에 위임 - 판정은 한 벌뿐이어야 한다.
	return IsDead(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor));
}

bool UVBAbilitySystemStatics::IsFriendlyFire(const AActor* Attacker, const AActor* Target)
{
	if (!Attacker || !Target || Attacker == Target)
	{
		// 자기 자신은 트레이스가 이미 제외한다(QueryParams.AddIgnoredActor) - 여기의 답이 아니다.
		return false;
	}
	return FGenericTeamId::GetAttitude(Attacker, Target) == ETeamAttitude::Friendly;
}

const FGameplayAbilitySpec* UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(
		UAbilitySystemComponent* ASC, FGameplayTag InputTag)
{
	if (!ASC || !InputTag.IsValid())
	{
		return nullptr;
	}

	// 순회 중 어빌리티가 부여/제거되면 배열이 재할당되어 이터레이터가 무효가 된다.
	// 잠금을 호출부가 아니라 여기 한 곳에만 거는 것이 이 함수를 공용 홈으로 둔 이유의 절반이다.
	FScopedAbilityListLock ActiveScopeLock(*ASC);
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UVBGameplayAbility* VBAbility = Cast<UVBGameplayAbility>(Spec.Ability);
		if (VBAbility && VBAbility->InputTag.MatchesTagExact(InputTag))
		{
			return &Spec;
		}
	}
	return nullptr;
}
