// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_Stagger.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBHitReactConfig.h"
#include "Vowbound/Vowbound.h"

UVBGA_Stagger::UVBGA_Stagger()
{
	// 입력 GA 가 아니다 → InputTag 없음. ActivationPolicy 는 베이스 기본값 OnInputTriggered 를 그대로 둔다
	// (OnSpawn 으로 두면 UVBGameplayAbility::OnGiveAbility 가 부여 즉시 활성화해버린다 - 트리거 기반과 충돌).

	// 활성화 배선은 ASC 가 자동 수행한다: UAbilitySystemComponent::OnGiveAbility 가 AbilityTriggers 를 순회해
	// GameplayEventTriggeredAbilities 맵에 등록 → GiveAbility 만 하면 끝. 별도 구독 코드 불필요.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_PoiseBroken;
	AbilityTriggers.Add(Trigger);

	// 게이트는 전부 GAS 네이티브다 - "연출을 스킵"이 아니라 "능력이 활성화되지 않음"이라 의미가 정확하고
	// 커스텀 판정 코드가 0줄이 된다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
	// 회피 중엔 안 비틀린다. 회피 i-frame 은 데미지를 0 으로 만들어 poise 파이프라인 자체를 막지만,
	// 감쇠가 아닌 다른 경로로 poise 가 들어올 가능성이 있으므로 게이트를 명시해 둔다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Dodging);
	// 가드 중엔 안 비틀린다 - 받아넘김(Accept)이 이미 그 상황의 반응이다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Guarding);

	// State.Combat.SuperArmor 는 일부러 넣지 않는다. 넣으면 뚝심과 Poise 가 같은 일
	// ("한 대로는 안 비틀린다")을 두 메커니즘으로 하게 되고, 어느 쪽을 튜닝해도 다른 쪽이 조용히 상쇄한다.
	// 적의 뚝심은 태그가 아니라 MaxPoise 값의 크기로 표현한다.

	// Poise 파탄 경직 재생 중 = Staggered. 이 GA 가 태그의 단독 생산자이며 엔진이 EndAbility 에서 제거한다.
	// 일반 히트리액트에 같은 태그를 부여하면 Poise 판단과 무관한 피격이 공격을 취소하므로 생산자를 늘리지 않는다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Staggered);

	// 경직 중 신규 공격 차단. 엔진 UAbilitySystemComponent::AreAbilityTagsBlocked 가 후보 GA 의 AssetTags 를
	// 부모까지 확장해 비교하므로, 부모 하나로 Ability.Attack.* 전부가 걸린다.
	// 주의: 이것은 '신규 활성화'만 막는다. 진행 중인 스윙의 중단은 경직 몽타주가 같은 슬롯을 덮어써
	//   공격 몽타주에 Interrupted 를 내는 경로에 의존한다.
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Attack);

	// 연타 FEEL 보존. false 면 InstancedPerActor 가 활성 중일 때 재활성화가 조용히 거부되어
	// 연속 경직이 첫 발에만 나온다(UVBGA_HitReact 와 같은 이유).
	bRetriggerInstancedAbility = true;
}

void UVBGA_Stagger::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                   const FGameplayAbilityActorInfo*     ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo,
                                   const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 데이터 홈 미할당은 설정 오류다 - 조용히 넘기면 경직이 흔적 없이 사라진다(BP 클래스 디폴트 누락).
	if (!HitReactConfig)
	{
		VB_LOG(Warning, "%s: HitReactConfig 미할당 -> 경직 스킵. BP_GA_Stagger 에 DA_HitReact_Default 를 할당하라",
		       *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 피격자 = 이 GA 의 아바타(이벤트가 피격자 ASC 로 송신되므로).
	UAnimMontage* StaggerMontage = HitReactConfig->ResolveStaggerMontage();
	if (!StaggerMontage)
	{
		VB_LOG(Warning, "%s: 경직 몽타주 해석 실패 -> 즉시 종료. DA_HitReact_Default 의 StaggerSet 을 저작하라",
		       *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ServerInitiated → 서버 + 피격자 소유클라 양쪽에서 실행되어 각자 로컬 재생한다(히트리액트와 동일 패턴).
	// bStopAllMontages=true 가 요점이다 - 진행 중인 공격 몽타주를 덮어써 그 GA 에 Interrupted 를 낸다.
	// bAllowInterruptAfterBlendOut(마지막 인자)는 엔진 기본값 false 를 유지한다(UVBGA_HitReact 주석 참조 -
	//   true 면 자연 블렌드아웃이 잘리고, 기대하던 OnInterrupted 는 영영 오지 않는다).
	UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			                                                               this,
			                                                               NAME_None,
			                                                               StaggerMontage,
			                                                               HitReactConfig->MontagePlayRate,
			                                                               NAME_None,
			                                                               true,
			                                                               1.0f,
			                                                               0.0f,
			                                                               false);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnStaggerEnded);
	MontageTask->ReadyForActivation();
}

void UVBGA_Stagger::OnStaggerEnded()
{
	// 몽타주 4개 델리게이트 공통 종료. bWasCancelled=false 로 통일 - 경직은 중단되든 완료되든
	// 정리할 상태가 없다(ActivationOwnedTags 의 Staggered 와 BlockAbilitiesWithTag 는 엔진이 EndAbility 에서 푼다).
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
