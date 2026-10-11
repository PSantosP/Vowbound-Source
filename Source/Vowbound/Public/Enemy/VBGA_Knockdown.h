// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_Stagger.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayAbilitySpecHandle.h"
#include "VBGA_Knockdown.generated.h"

class UAbilityTask_PlayMontageAndWait;
class UAnimMontage;
struct FAbilityEndedData;
struct FGameplayEventData;
struct FVBKnockdownSet;
enum class EVBHitDirection : uint8;

/**
 * UVBGA_Knockdown
 *
 * 그로기(다운) 어빌리티 (design .sw/boss/design.md §14). 경직(UVBGA_Stagger)의 파생이다.
 * 물려받는 것: 트리거(Event.Combat.PoiseBroken)·게이트(사망/회피/가드/처형)·State.Combat.Staggered 소유·HitReactConfig.
 * 대신 하는 것: 넘어질 수 있으면(KnockdownSet 저작 + 쿨다운 아님) 서 있는 경직 대신 «넘어짐 -> 엎드린 대기 -> 기상» 을 돈다.
 *   못 넘어지면 Super 의 서 있는 방향별 경직 그대로다 - 한 이벤트에 GA 하나라 둘이 겹쳐 도는 일이 없다.
 * 진입로 둘(2026-09-29, DEC-025 design .sw/hit-reaction-tiers/design.md): 기절 게이지 소진(PoiseBroken)과 넘어뜨리는 타격(KnockedDown).
 *   기절 게이지 소진은 쿨다운·bFallOnPoiseBreak 를 보고, 넘어뜨리는 타격은 안 본다(공격 등급이 이미 고른 반응이다).
 *   넘어뜨리는 타격이면 공격자를 향해 돌려 세운 뒤 넘어뜨린다 - 넘어짐 클립이 한 방향뿐이라서다.
 *   병사·감염체는 bFallOnPoiseBreak=false 로 이 GA 를 쓴다: 기절 게이지 소진은 서 있는 경직, 넘어짐은 강타로만.
 *
 * 왜 파생인가: 그로기는 «더 나아간 경직» 이다. 같은 이벤트·같은 게이트·같은 방향 계산을 두 클래스에 적으면 한쪽이 낡는다.
 * 왜 Staggered 를 그대로 드는가: BT_Infected 의 이동·공격 분기가 NOT Staggered 로 닫혀 있다. 이 태그 하나로 넘어진 보스가
 *   걸어 나가거나 공격을 시동하지 않는다 - BT 편집 0.
 * 왜 Ability.Attack 차단을 뺐는가: 엎드린 채 휘두르는 반격이 그 태그다. 공격의 유일한 시동자(BT)가 닫혀 있으니
 *   «경직 중 신규 공격 없음» 은 그대로다.
 *
 * 흐름(서버 - 경직과 같은 ServerInitiated. 몽타주는 ASC 복제로 클라에 간다):
 *   넘어짐: Downed 루즈태그 + 취약 GE -> FallMontage -> 끝나면 대기로
 *   대기:   DownIdleMontage(끝나면 다시) · DownHoldSeconds 타이머 · 반격 감시 타이머(0.2초)
 *           - Event.Combat.Hit: 반격 중이 아니면 DownHitSet 방향 클립 - 앞 반응이 재생 중이어도 새로 튼다 -> 끝나면 대기로
 *           - 감시: 표적이 DownAttackTriggerRadius 안 + 간격 경과 -> InputTag 로 반격 GA 켬(다운 피격 재생 중에도) -> 그 GA 가 끝나면 대기(타이머 끝났으면 기상)
 *           - 타이머 끝 + 끼어든 것 없음 -> 기상
 *   기상:   GetUpMontage -> 끝나면 EndAbility
 *   EndAbility(모든 경로): Downed 제거 · 취약 GE 제거 · 쿨다운 시각 기록 · 타이머·구독 정리
 *
 * 다운 중 두 번째 기절 게이지 소진은 ActivationBlockedTags(Downed) 가 막는다 - 그 파탄은 버려진다(Poise 는 파탄 때 만충된다).
 * 피격 규칙(2026-09-29 user 「공격할 때 빼고 늘 반응」, design §14.13): 반격 중(예고 포함)만 반응 없음.
 *   몸통 반응(방향별 다운 피격) = 대기 시간 안의 대기, 넘어짐이 엎드린 자세에 닿은 뒤(FVBKnockdownSet::FallHitAcceptSeconds, 트롤 2.8초 - 넘어짐을 끊는다).
 *   가산 플린치(UVBHitReactConfig::FlinchSet) = 그 밖(넘어짐 앞부분·기상 중·대기 시간이 끝난 뒤). 클립이 스스로 자세를 바꾸는 구간을
 *   몸통 반응으로 끊으면 튀고, 대기가 끝난 뒤에도 몸통 반응을 새로 틀면 계속 때려 영원히 눕힐 수 있다 - 그 뚜껑이 «대기 시간» 이다.
 * 엎드린 동안은 넉백도 없다(VBHealthAttributeSet.cpp - IsDowned 게이트). 루트모션 아래 넉백은 수직으로만 튀기 때문이다.
 * 몽타주 태스크의 종료 배선은 BlendOut·Interrupted·Cancelled 셋이다(Completed 는 안 건다) - 한 태스크에서 BlendOut 과 Interrupted 는
 *   둘 중 하나만 오고, Completed 는 BlendOut 뒤에 또 와서 다음 몽타주를 두 번 틀게 한다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_Knockdown : public UVBGA_Stagger
{
	GENERATED_BODY()

public:
	UVBGA_Knockdown();

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
	                            const FGameplayAbilityActorInfo*     ActorInfo,
	                            const FGameplayAbilityActivationInfo ActivationInfo,
	                            const FGameplayEventData*            TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle     Handle,
	                        const FGameplayAbilityActorInfo*     ActorInfo,
	                        const FGameplayAbilityActivationInfo ActivationInfo,
	                        bool                                 bReplicateEndAbility,
	                        bool                                 bWasCancelled) override;

	// 넘어뜨리는 타격(Event.Combat.KnockedDown)은 KnockdownSet 이 저작돼 있을 때만 받는다(DEC-025).
	//   거절하면 허브가 켜진 수 0 을 보고 무거운 피격으로 내린다. 기절 게이지 소진은 종전처럼 늘 받는다.
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
	                                         const FGameplayEventData*        Payload) const override;

private:
	enum class EKnockdownPhase : uint8
	{
		None,
		Falling,
		Hold,
		GettingUp
	};

	// 다운 세트. HitReactConfig 가 없으면 null.
	const FVBKnockdownSet* GetKnockdownSet() const;

	// 몽타주 태스크 하나를 만들어 돌려준다(아직 켜지 않음). 종료 배선은 호출자가 하고 ReadyForActivation 도 호출자가 부른다.
	UAbilityTask_PlayMontageAndWait* MakeMontageTask(UAnimMontage* Montage);

	void BeginHold();
	void PlayDownIdle();
	void BeginGetUp();
	// 대기가 끝났고 끼어든 것(다운 피격·반격)이 없으면 기상으로. 끼어든 것이 있으면 그것이 끝날 때 다시 묻는다.
	void ResumeAfterInterlude();

	// 몸통 반응을 못 트는 자리의 피격 반응. 지금 도는 몽타주를 멈추지 않고 가산 슬롯에 얹는다. 못 틀면 이유를 로그로 남긴다.
	void PlayFlinch(EVBHitDirection Direction, const TCHAR* Situation);

	void TickDownAttackWatch();
	bool TryStartDownAttack(const FVBKnockdownSet& Set, double Now);
	void OnAnyAbilityEnded(const FAbilityEndedData& EndedData);
	void OnHoldExpired();

	UFUNCTION()
	void OnFallEnded();
	UFUNCTION()
	void OnDownIdleEnded();
	// 다운 피격이 제 끝에서 빠질 때(BlendOut)와 남에게 밀려날 때(Interrupted·Cancelled)를 가른다 - 앞은 뒤를 우리가 덮어야 하고, 뒤는 민 쪽이 덮는다.
	UFUNCTION()
	void OnDownHitEnded();
	UFUNCTION()
	void OnDownHitInterrupted();
	UFUNCTION()
	void OnGetUpEnded();
	UFUNCTION()
	void OnHitEventReceived(FGameplayEventData Payload);

	// 반격 몽타주가 제 끝에서 빠지기 시작한 순간(UAnimInstance::OnMontageBlendingOut). 반격 능력의 종료 알림은 블렌드아웃이 «끝난 뒤» 라
	//   그 0.25초 동안 몸을 덮는 몽타주가 없어 서 있는 자세가 드러났다(2026-09-29). 여기서 대기를 먼저 건다.
	UFUNCTION()
	void OnAnyMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	EKnockdownPhase Phase = EKnockdownPhase::None;

	// 서버 전용 래치들. 태그·GE 는 «걸었을 때만» 떼기 위해 둔다(공격 GA 의 bGrantedSuperArmor 와 같은 이유).
	bool bDownedTagGranted = false;
	bool bHoldExpired      = false;
	bool bDownHitPlaying   = false;
	bool bDownAttackActive = false;

	FActiveGameplayEffectHandle VulnerabilityHandle;
	FGameplayAbilitySpecHandle  DownAttackSpecHandle;
	FDelegateHandle             AbilityEndedHandle;
	FTimerHandle                HoldTimer;
	FTimerHandle                WatchTimer;

	// 재생 중인 다운 피격의 태스크. 새 타격이 반응을 갈아 끼울 때 앞 태스크의 종료 알림을 떼어 낸다 - 안 떼면 앞 클립이 밀려나며 부른
	//   OnDownHitEnded 가 대기를 틀어 방금 튼 반응을 덮는다. 태스크는 능력이 소유하므로 약한 참조로 든다.
	TWeakObjectPtr<UAbilityTask_PlayMontageAndWait> DownHitTask;

	double LastDownAttackTime = -1.0e9;

	// 기상이 끝난 시각 + CooldownSeconds. 이 전의 파탄은 서 있는 경직. InstancedPerActor 라 활성화 사이에 남는다.
	double CooldownUntil = 0.0;

	// 넘어짐을 다운 피격으로 끊었을 때 남아 있던 클립 시간(초). BeginHold 가 대기 시간에 더한다 - 전체 다운 길이를 지키려고.
	float FallTimeCutShort = 0.0f;
};
