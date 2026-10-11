// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_FinisherWindow.generated.h"

class UVBFinisherWindowConfig;

/**
 * UVBGA_FinisherWindow — 처형 창(DEC-019, 2026-09-11)
 *
 * 무엇: 체력이 문턱 아래로 떨어진 적이 잠시 무력해지고, 그동안에만 처형이 걸린다.
 *       창이 닫히면 적은 풀려나 다시 싸운다.
 *
 * 왜 있나: 종전에는 «체력 30% 이하» 가 곧 처형 가능이었다. 창이 없으니 프롬프트를 띄울 시점도 없고,
 *   적이 때리러 오는 도중에도 처형이 걸려 두 애니가 겹쳤다(2026-09-11 user PIE).
 *   창을 상태로 만들면 그 셋이 한꺼번에 풀린다 - 프롬프트가 붙을 곳이 생기고, 창 동안 적이 멈추고,
 *   창 밖에서는 처형이 아예 활성화되지 않는다.
 *
 * 활성화 경로: UVBHealthAttributeSet::PostGameplayEffectExecute 가 피해 적용 뒤
 *   Event.Combat.FinisherWindow 를 자기 ASC 로 송신하며 EventMagnitude 에 «남은 체력 비율» 을 싣는다
 *   -> 아래 AbilityTriggers 가 수신 -> ShouldAbilityRespondToEvent 가 문턱과 비교해 가른다.
 *   (UVBGA_Stagger 가 Event.Combat.PoiseBroken 으로 켜지는 것과 같은 구조에 판정 훅 하나가 더 붙은 모양이다)
 *
 * 왜 문턱 판정이 여기인가: 어트리뷰트 셋이 아는 사실은 「체력이 이만큼 남았다」 하나다. 「그 정도면
 *   창을 열 만한가」는 설정 자산의 판단이고 그 자산은 이 GA 가 든다. 문턱을 어트리뷰트 셋에서 읽으면
 *   같은 숫자가 두 곳에 산다. 그리고 엔진이 ShouldAbilityRespondToEvent 를 CanActivateAbility 보다도
 *   먼저 부르므로, 문턱 위의 타격은 재무장 쿨다운을 태우지 않고 되돌아 나간다.
 *
 * 왜 Poise 파탄 이벤트를 같이 쓰지 않는가: 그 이벤트에는 이미 UVBGA_Stagger 가 걸려 있다. 트리거 하나에
 *   GA 둘이 붙으면 어느 쪽이 먼저 활성화되느냐가 ASC 순회 순서에 달리고, 먼저 선 쪽의 ActivationOwnedTags 가
 *   뒤쪽의 ActivationBlockedTags 를 때리느냐까지 그 순서로 갈린다. 순서에 기대는 게이트는 조용히 뒤집힌다.
 *
 * 재무장: 이 GA 자신의 CooldownGameplayEffectClass 가 맡는다. 베이스가 CommitAbility 를 강제하므로
 *   커스텀 코드 0줄이고 값의 홈은 GE 자산 하나다. 쿨다운 중에는 이벤트가 와도 창이 안 열린다 -
 *   그것이 없으면 약한 연타가 적을 사실상 영구 무력화한다.
 *
 * 창 중의 추가 타격은 창을 연장하지 않는다: bRetriggerInstancedAbility 를 켜지 않아 엔진이 재활성화를 거부한다.
 *
 * 반응이 끝난 뒤에 연다 (2026-09-29, DEC-025, user 「5타 때리면 바로 처형준비상태가 되어버려」):
 *   문턱을 넘긴 그 타격의 피격 반응(플린치·무거운 피격·넘어짐·기상)이 도는 동안에는 «대기» 로 켜져만 있고,
 *   반응 태그(HitReacting·Downed·GettingUp·SuperArmor)가 다 걷히면 그때 창을 연다(최대 MaxReactionWaitSeconds). 종전에는 같은 프레임에 열려
 *   대기 몽타주가 방금 시작한 반응을 덮었고, 넘어뜨린 타격이면 누운 몸이 벌떡 섰다.
 *   경직(기절 게이지 소진으로 선 기절)은 기다리지 않는다 - 기절 자체가 무력한 때라 기다리면 창이 3.7초 늦는다(09-29 user PIE).
 *   그래서 창 태그는 ActivationOwnedTags 가 아니라 창이 열리는 순간 손으로 붙인다 - 태그를 읽는 쪽(처형 대상 선정·BT·경직 차단)은
 *   전부 «창이 열려 있다» 로 읽기 때문에, 대기 중에 태그가 서 있으면 누운 몸에 처형이 걸린다.
 *   창 시간(WindowDuration)·발광·대기 자세·공격 취소도 여는 순간부터다. 재무장 쿨다운만 활성화(대기 시작) 때 걸린다.
 *
 * 전제: BP 파생에서 FinisherWindowConfig 와 CooldownGameplayEffectClass 를 할당해야 한다.
 * 부작용: 소유자의 진행 중인 공격 GA 취소, 신규 공격 차단, 속도·가속 제거, 대기 몽타주 재생.
 *   해제에 우리 코드는 하나뿐이다 - 손으로 붙인 창 태그를 EndAbility 에서 뗀다. 나머지(능력 차단·몽타주·태스크·큐)는
 *   종전대로 엔진이 거둔다. 창이 닫히는 경로가 셋이어도(시간 만료 / 사망 / 외부 취소) 전부 EndAbility 를 지나므로
 *   풀다 만 상태가 남을 자리가 없다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_FinisherWindow : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_FinisherWindow();

protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	// 문턱 게이트. 엔진이 활성화 시도의 맨 앞에서 부른다 - 여기서 거부하면 CommitAbility 가 안 돌아
	// 재무장 쿨다운이 그대로 남는다. 부작용을 두지 않는다(판정만 한다).
	virtual bool ShouldAbilityRespondToEvent(
		const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const override;

	// 손으로 붙인 창 태그를 뗀다(열린 적이 있을 때만). 대기 중에 끝나면 붙인 것이 없다.
	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 창의 튜닝값. BP_GA_FinisherWindow 클래스 디폴트에서 할당한다.
	// 미할당이면 창을 열지 않는다 - 기본값으로 대충 여는 것은 「문턱이 어디인지 아무도 모르는 창」을 만든다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher")
	TObjectPtr<UVBFinisherWindowConfig> FinisherWindowConfig;

private:
	// 제한 시간 만료. 처형당하지 않고 창이 닫히는 정상 경로다.
	UFUNCTION()
	void OnWindowExpired();

	// 소유자가 죽었다. 처형이 성사된 경우와 다른 사인으로 죽은 경우를 함께 받는다.
	// 창을 여기서 닫지 않으면 시체가 처형 가능 태그를 든 채 남아 두 번째 처형이 활성화된다.
	UFUNCTION()
	void OnOwnerDied();

	// 장면 때문에 만료를 미룬 적이 있는가. 로그를 한 번만 남기기 위한 기록이다 -
	// 0.25초마다 찍으면 다른 계측을 덮는다. 조용히 미루기만 하면 「창이 영영 안 닫힌다」가 무증상이 된다.
	bool bDeferredForScene = false;

	// 반응이 걷혔는지 다시 본다. 걷혔으면 OpenWindow, 아니면 조금 뒤 다시.
	UFUNCTION()
	void OnReactionRecheck();

	// 소유자가 지금 창을 열면 안 되는 중인가(HitReacting·Downed·GettingUp·SuperArmor 중 하나라도). 경직(Staggered)은 기다리지 않는다.
	bool IsOwnerReacting() const;

	// 반응 대기를 시작한 시각(월드 초). OnReactionRecheck 가 설정의 MaxReactionWaitSeconds 를 넘으면 창을 포기한다.
	double ReactionWaitStartTime = 0.0;

	// 창을 실제로 연다 - 창 태그·발광·공격 취소·정지·대기 자세·창 시간. 한 활성화에 한 번.
	void OpenWindow();

	// 창 태그를 붙였는가. EndAbility 가 이것만 보고 뗀다 - 대기 중에 끝났으면 뗄 것이 없다.
	bool bWindowOpen = false;
};
