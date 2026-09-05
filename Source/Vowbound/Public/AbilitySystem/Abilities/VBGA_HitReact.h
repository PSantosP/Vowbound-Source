// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_HitReact.generated.h"

class UVBHitReactConfig;

/**
 * UVBGA_HitReact
 *
 * 피격 반응(히트리액트) 어빌리티. 피격자 본인의 ASC에서 활성화되어 히트리액트 몽타주를 재생한다.
 * 그 몽타주가 bStopAllMontages 로 진행 중인 공격 몽타주를 끊는 것이 "맞으면 공격이 끊긴다"의 실체다.
 *
 * 왜 GA 인가 (DEBT-009 2단계 — 히트리액트가 코스메틱 큐에 살던 구조의 최종 해소):
 *  1. 데디케이티드 서버 — GameplayCue 는 서버에서 실행되지 않는다(GameplayCueRunOnDedicatedServer=0 기본
 *     → ShouldSuppressGameplayCues). 히트리액트가 큐에 있으면 서버에선 공격이 안 끊기고 계속 스윙하며 데미지를 낸다
 *     = "맞으면 끊긴다"가 클라 코스메틱 전용이 되고 뚝심은 서버측 no-op. PIE ListenServer 가 이 결함을 은폐한다(FIND-038).
 *     GA 는 서버에서 실행되므로 이 계층 오류가 근본 소멸한다.
 *  2. 피격자별 데이터 — 큐는 '공격자의 큐 태그'로 갈려 config 가 게임 전체에 최대 2개뿐이었다.
 *     GA 는 DefaultAbilities 로 피격자 아키타입 단위 부여되므로 config 가 피격자를 따라온다(FIND-039).
 *  3. 뚝심 게이트가 GAS 네이티브 — ActivationBlockedTags. "연출을 스킵"이 아니라 "능력이 활성화되지 않음"이라
 *     의미가 정확해지고, 커스텀 판정 코드가 0이 된다.
 *
 * 활성화 경로: 피격자 VBHealthAttributeSet::PostGameplayEffectExecute 가 데미지 확정 후
 *              Event.Combat.Hit 를 HandleGameplayEvent 로 송신 → 아래 AbilityTriggers 가 수신해 활성화.
 *              (공격자가 아니라 피격자 쪽에서 쏘는 이유: 도조 i-frame 판정이 그 함수 안에 있어
 *               DamageValue==0 이면 이벤트가 아예 나가지 않는다 → 무적 중 히트리액트 재생 버그가 구조적으로 소멸)
 */
UCLASS()
class VOWBOUND_API UVBGA_HitReact : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_HitReact();

protected:
	// 히트리액트 데이터(피격자 소유). BP_GA_HitReact 클래스 디폴트에서 DA_HitReact_Default 를 할당한다.
	// 왜 GA 가 데이터를 드는가: GA 는 DefaultAbilities(VBPlayerState / VBEnemyBase)로 아키타입 단위 부여되므로
	//   config 참조가 피격자를 자동으로 따라온다 — C++ 부여 코드 0줄.
	//   VBGA_MeleeAttackBase 가 MovesetConfig 를 드는 것과 동일한 확립된 관례.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBHitReactConfig> HitReactConfig;

	// 베이스가 ActivateAbility 를 final 로 잠그고 CommitAbility 를 강제하므로 여기를 오버라이드한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
	                            const FGameplayAbilityActorInfo*     ActorInfo,
	                            const FGameplayAbilityActivationInfo ActivationInfo,
	                            const FGameplayEventData*            TriggerEventData) override;

private:
	// 몽타주 종료 4경로(완료/블렌드아웃/중단/취소) 공통 종료 — 히트리액트는 경로별 분기가 필요 없다.
	UFUNCTION()
	void OnHitReactEnded();
};
