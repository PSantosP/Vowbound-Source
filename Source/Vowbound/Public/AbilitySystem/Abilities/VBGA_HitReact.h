// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/VBHitImpact.h" // EVBHitImpact (PlayingImpact)
#include "VBGA_HitReact.generated.h"

class UVBHitReactConfig;
class UAnimMontage;
class UCharacterMovementComponent;

/**
 * UVBGA_HitReact
 *
 * 피격 반응(히트리액트) 어빌리티. 피격자 본인의 ASC에서 활성화되어 히트리액트 몽타주를 재생한다.
 * 그 몽타주가 bStopAllMontages 로 진행 중인 공격 몽타주를 끊는 것이 "맞으면 공격이 끊긴다"의 실체다.
 *
 * 왜 GA 인가 (DEBT-009 2단계 — 히트리액트가 코스메틱 큐에 살던 구조의 최종 해소):
 *  1. 데디케이티드 서버 — GameplayCue 는 서버에서 실행되지 않는다(GameplayCueRunOnDedicatedServer=0 기본
 *     → ShouldSuppressGameplayCues). 히트리액트가 큐에 있으면 서버에선 공격이 안 끊기고 계속 스윙하며 데미지를 낸다
 *     = "맞으면 끊긴다"가 클라 코스메틱 전용이 되고 슈퍼아머는 서버측 no-op. PIE ListenServer 가 이 결함을 은폐한다(FIND-038).
 *     GA 는 서버에서 실행되므로 이 계층 오류가 근본 소멸한다.
 *  2. 피격자별 데이터 — 큐는 '공격자의 큐 태그'로 갈려 config 가 게임 전체에 최대 2개뿐이었다.
 *     GA 는 DefaultAbilities 로 피격자 아키타입 단위 부여되므로 config 가 피격자를 따라온다(FIND-039).
 *  3. 슈퍼아머 게이트가 GAS 네이티브 — ActivationBlockedTags. "연출을 스킵"이 아니라 "능력이 활성화되지 않음"이라
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

	// 실효 등급이 «없음»(저항이 다 깎았다)이면 켜지지 않는다(DEC-025). 엔진이 활성화 시도의 맨 앞에서 부른다 -
	//   켜진 뒤 바로 끝내면 HitReacting 태그가 한 프레임 서고 CancelAbilitiesWithTag(상호작용)가 돌아 버린다.
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
	                                         const FGameplayEventData*        Payload) const override;

	// 밀림 방향 보정을 푸는 유일한 자리. 끝나는 길(몽타주 4종·재트리거·취소)이 전부 여기를 지난다.
	virtual void EndAbility(const FGameplayAbilitySpecHandle     Handle,
	                        const FGameplayAbilityActorInfo*     ActorInfo,
	                        const FGameplayAbilityActivationInfo ActivationInfo,
	                        bool                                 bReplicateEndAbility,
	                        bool                                 bWasCancelled) override;

private:
	// 몽타주 종료 4경로(완료/블렌드아웃/중단/취소) 공통 종료 — 히트리액트는 경로별 분기가 필요 없다.
	UFUNCTION()
	void OnHitReactEnded();

	// 반응 클립의 루트모션 수평 이동을 «밀린 방향» 으로 돌린다(design .sw/hit-reaction-tiers/design.md 1-B 네 번째 PIE 5번).
	//   클립은 피격자 몸 기준 축으로 밀어서, 비스듬히 선 적은 공격 방향과 최대 45도 어긋나게 밀렸다(user 「정면을 때렸는데 왼쪽으로」).
	//   몸은 돌리지 않는다 - 이동만 돌린다. 걸었으면 true(각은 RootMotionRedirectYaw), 안 걸 이유가 있으면 false.
	bool BeginRootMotionRedirect(UAnimMontage* Montage, float PushYaw);
	void  EndRootMotionRedirect();
	// CMC ProcessRootMotionPostConvertToWorld 에 묶인다. 그 반응 몽타주가 루트모션 주인일 때만 돌리고, 아니면 그대로 넘긴다.
	FTransform RedirectRootMotion(const FTransform& WorldRootMotion, UCharacterMovementComponent* Movement, float DeltaSeconds);

	// 활성화 때 한 번 정한 보정 각. 매 프레임 다시 재면 몸이 움직이며 값이 흔들린다.
	float                                        RootMotionRedirectYaw = 0.0f;
	TWeakObjectPtr<UAnimMontage>                 RedirectedMontage;
	TWeakObjectPtr<UCharacterMovementComponent>  RedirectedMovement;

	// 클립 전 구간 루트모션(몽타주 로컬). 몽타주에만 달린 값이라 액터마다 몽타주당 한 번만 잰다 - 연타마다 트랙을 다시 훑지 않게(2026-09-30 /sw:verify).
	//   InstancedPerActor 라 PIE 한 판 동안 산다. 에디터에서 몽타주를 고쳤으면 PIE 를 다시 켜야 새 값이 든다.
	TMap<TObjectKey<UAnimMontage>, FVector> ClipTranslationCache;

	// HeavySet 미저작 경고를 액터당 한 번만 낸다. 매 강타마다 내면 로그가 그 줄로 덮여 다른 경고가 안 보인다.
	//   InstancedPerActor 라 이 값은 액터 수명 동안 남는다.
	bool bWarnedMissingHeavy = false;

	// 지금 틀고 있는 반응의 등급. 이보다 약한 타격은 ShouldAbilityRespondToEvent 가 거절한다 - 약한 반응이 강한 반응을 끊지 않게.
	//   2026-09-29 PIE: 카타나 5타(무거움)와 같은 순간 들어온 출혈 폭발(등급 없음 = 가벼움)이 재트리거로 무거운 반응을 가벼운 움찔로 덮었고,
	//   그 움찔 클립의 루트모션이 방금 준 넉백까지 지웠다(로그 12:15:38.911).
	//   엔진은 인스턴스가 있으면 판정을 인스턴스에서 부른다(AbilitySystemComponent_Abilities.cpp::InternalTryActivateAbility - AbilitySource).
	EVBHitImpact PlayingImpact = EVBHitImpact::None;
};
