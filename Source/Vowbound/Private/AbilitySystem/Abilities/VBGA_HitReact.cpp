// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_HitReact.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBHitReactConfig.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 피격자의 현재 스탠스. 히트리액트는 피격자 소유 데이터이므로 "맞은 쪽"의 무기 타입을 읽는다.
	// 적(AVBEnemyBase)은 UVBWeaponStateComponent 를 갖지 않는다(플레이어 전용) → None → DefaultSet 폴백.
	EVBWeaponType ResolveVictimStance(const AActor* Victim)
	{
		if (const AVBCharacter* VBChar = Cast<AVBCharacter>(Victim))
		{
			if (const UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent())
			{
				return WSC->GetCurrentWeaponType();
			}
		}
		return EVBWeaponType::None;
	}
}

UVBGA_HitReact::UVBGA_HitReact()
{
	// 입력 GA 가 아니다 → InputTag 없음. ActivationPolicy 는 베이스 기본값 OnInputTriggered 를 그대로 둔다.
	// (OnSpawn 으로 두면 VBGameplayAbility::OnGiveAbility 가 부여 즉시 활성화해버린다 — 트리거 기반과 충돌)
	// NetExecutionPolicy(ServerInitiated) / InstancingPolicy(InstancedPerActor) 는 베이스 생성자 상속.

	// 프로젝트 최초의 AbilityTriggers 사용.
	// 배선은 ASC 가 자동 수행한다: UAbilitySystemComponent::OnGiveAbility 가 AbilityTriggers 를 순회해
	// GameplayEventTriggeredAbilities 맵에 등록 → GiveAbility 만 하면 끝. 별도 구독 코드 불필요.
	// TriggerSource 는 FAbilityTriggerData 기본 생성자가 이미 GameplayEvent 로 설정하므로 명시 대입 생략.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_Hit;   // 선언만 있고 사용처가 없던 예약 태그를 여기서 살린다
	AbilityTriggers.Add(Trigger);

	// 뚝심/사망 게이트 = GAS 네이티브("이 어빌리티는 활성화 액터가 이 태그 중 하나라도 가지면 차단됨").
	// 왜 로스태그 비복제(VBGA_MeleeAttackBase.cpp AddLooseGameplayTag)가 문제되지 않는가:
	//   트리거 활성화는 서버 권위에서만 시도된다(HasNetworkAuthorityToActivateTriggeredAbility →
	//   ServerInitiated 는 bIsAuthority 반환) → 판정이 읽는 ASC 는 항상 서버 ASC 이고, 서버엔 뚝심 태그가 확실히 있다.
	//   → 큐 시절 sim proxy 에서 게이트가 반대로 평가되던 발산이 자동 소멸한다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_SuperArmor);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);

	// 히트리액트는 일반 유효 피격 연출만 담당하며 State.Combat.Staggered 를 소유하지 않는다.
	// 해당 태그의 단독 생산자는 UVBGA_Stagger 이며, Poise 파탄만 뜻한다.

	// 연타 FEEL 보존을 위해 반드시 true. 기본값 false 면 InstancedPerActor 가 활성 중일 때 재활성화가 조용히 거부된다
	// → 연타로 맞아도 히트리액트가 첫 발에만 나온다(구 동작은 bStopAllMontages 로 매번 재생 = 회귀).
	// 엔진 주석: "if true, and trying to activate an already active instanced ability, end it and re-trigger it."
	bRetriggerInstancedAbility = true;
}

void UVBGA_HitReact::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                    const FGameplayAbilityActorInfo*     ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                    const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 데이터 홈 미할당은 설정 오류다 — 조용히 넘기면 히트리액트가 흔적 없이 사라진다(BP 클래스 디폴트 누락).
	if (!HitReactConfig)
	{
		VB_LOG(Warning, "%s: HitReactConfig 미할당 -> 히트리액트 스킵. BP_GA_HitReact 에 DA_HitReact_Default 를 할당하라", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 피격자 = 이 GA 의 아바타(이벤트가 피격자 ASC 로 송신되므로). 공격자 기준 역추출이 필요 없다 —
	// 큐 시절 EffectContext 의 HitResult 에서 피격자를 되짚던 역전 구조가 여기서 사라진다.
	UAnimMontage* HitReactMontage = HitReactConfig->ResolveMontage(ResolveVictimStance(GetAvatarActorFromActorInfo()));
	if (!HitReactMontage)
	{
		VB_LOG(Warning, "%s: 히트리액트 몽타주 해석 실패 -> 즉시 종료", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ServerInitiated → 서버 + 피격자 소유클라 양쪽에서 실행되어 각자 로컬 재생한다(공격 GA 와 동일 패턴).
	// sim proxy 는 서버 ASC->PlayMontage 가 갱신하는 RepAnimMontageInfo 로 수신 — 히트리액트는 섹션 점프가 없는
	// 단발 Play 라, 3+ 연속 jump 에서 불안정하던 기지 사례(VBGA_MeleeAttackBase 콤보 점프)와는 다른 경로다.
	// 서버 전용 부작용이 없으므로 IsNetAuthority 분기 불필요.
	// TriggerEventData 는 현재 미사용 — 무방향 단일 몽타주라 방향 정보가 필요 없다(방향별 확장 시 payload 활용).
	// bAllowInterruptAfterBlendOut(마지막 인자)는 엔진 기본값 false 를 유지한다.
	// true 로 두면: 정상 블렌드아웃 시 Task 가 ClearAnimatingAbility 를 스킵 → OnBlendOut → EndAbility →
	//   bStopWhenAbilityEnds 의 StopPlayingMontage 가드가 여전히 통과 → 이미 블렌드아웃 중인 몽타주에 재차 stop
	//   = 자연 블렌드아웃이 잘린다. 게다가 그 플래그가 켜주려던 "블렌드아웃 후 OnInterrupted"는 영원히 오지 않는다
	//   (OnBlendOut 에서 이미 어빌리티가 끝나 ShouldBroadcastAbilityTaskDelegates()가 false).
	UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			                                                               this,
			                                                               NAME_None,
			                                                               HitReactMontage,
			                                                               HitReactConfig->MontagePlayRate,
			                                                               NAME_None,
			                                                               true,
			                                                               1.0f,
			                                                               0.0f,
			                                                               false);

	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnHitReactEnded);
	MontageTask->ReadyForActivation();
}

void UVBGA_HitReact::OnHitReactEnded()
{
	// 몽타주 4개 델리게이트(Completed/BlendOut/Interrupted/Cancelled) 공통 종료. bWasCancelled=false 로 통일 —
	// 히트리액트는 중단되든 완료되든 정리할 자체 상태가 없다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
