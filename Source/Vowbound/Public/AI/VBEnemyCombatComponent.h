// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h" // OnAttackingTagChanged 가 FGameplayTag 를 값으로 받는다
#include "AI/VBCombatTypes.h"
#include "VBEnemyCombatComponent.generated.h"

class UVBEnemyConfig;
class UVBCombatCoordinatorSubsystem;
class UAbilitySystemComponent;

/**
 * 적 하나의 전투 조율 상태.
 *
 * 무엇: "내 전투 타겟"만 소유한다. 역할/토큰/슬롯은 소유하지 않고 코디네이터에 위임 조회한다 -
 *       캐시하면 같은 사실의 홈이 둘이 되고 재계산 사이에 두 값이 갈린다.
 * 왜 컴포넌트인가: AVBEnemyBase 는 ASC/사망/타겟어블만 책임진다. 전투 조율 상태를 액터에 얹으면
 *       SRP 가 깨지고, 적 아키타입이 늘 때 액터 클래스가 비대해진다(UVBHitFlashComponent 와 같은 판단).
 * Tick: 없다(bCanEverTick=false). 관측은 BTService 주기, 조율은 코디네이터 타이머가 한다.
 * 복제: 없다. 역할/토큰/슬롯은 순수 서버 결정 상태이고 적 ASC 는 ReplicationMode=Minimal 이다.
 *       SetIsReplicated 를 호출하지 않는 것이 이 클래스의 계약이다.
 * 부작용: BeginPlay 에서 서버 한정으로 ASC 태그 이벤트를 구독한다(토큰 반납의 유일한 정상 경로).
 */
UCLASS()
class VOWBOUND_API UVBEnemyCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBEnemyCombatComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 블랙보드의 TargetActor 를 코디네이터 등록 상태로 반영하는 유일한 경로. null 이면 전투 이탈.
	// 왜 EnterCombat 이 아니라 Set 인가: 호출자(BTService)가 매 주기 현재 진실을 그대로 넘기는
	//   멱등 함수라야 "진입/이탈 이벤트를 놓쳤다"가 구조적으로 불가능해진다.
	//   같은 타겟을 다시 넘기는 것도 무해하다 - 등록이 멱등이라 기존 결정이 보존된다.
	void SetCombatTarget(AActor* NewTarget);

	// 전투 이탈. SetCombatTarget(nullptr) 과 같다 - 경로를 하나로 두기 위해 위임한다.
	// AVBEnemyBase::HandleDeath 와 EndPlay 가 호출한다.
	void LeaveCombat();

	// 코디네이터 위임 조회. 이 값들을 캐시하지 않는다 - 캐시하면 홈이 둘이 된다.
	EVBCombatRole GetRole() const;
	bool          HasAttackToken() const;
	FVector       GetSlotLocation() const;
	AActor*       GetCombatTarget() const { return CombatTarget.Get(); }

	// === 계획된 다음 공격 (design .sw/boss/design.md §12.7) - «공격을 먼저 고르고 그 거리까지 간다» ===
	// 토큰을 받는 순간 코디네이터가 PlanNextAttack 을 부른다. 공격 GA 의 설정 자산에서 변형마다 클립 전진을 재고, 시작 거리 = 전진 + 멈추는 거리.
	//   후보는 «지금 거리에서 대역 안으로 닿을 수 있는 변형», 그중 순환 커서. 그 뒤 BT 서비스·역할 판정·걸음이 GetAttackStartDistance 를 읽고,
	//   공격 GA 가 ConsumePlannedVariantIndex 로 같은 변형을 튼다. 계획이 없으면 전부 종전 값(AttackCommitDistance)이라 잡몹 흐름은 그대로다.
	// 왜 여기가 홈인가: 거리·타겟·설정·GA 스펙이 전부 이 컴포넌트에서 이미 조회된다. 소울 계열 AI 의 «Approach_Act(거리) 뒤 Attack» 과 같은 구조.
	void  PlanNextAttack(const FVector& TargetLocation);
	void  ClearPlannedAttack();
	int32 ConsumePlannedVariantIndex();

	// 시작 거리를 블랙보드에 지금 쓴다(AVBAIController::PushAttackStartDistance). 계획을 세우거나 비운 자리에서만 부른다 -
	//   서비스의 주기 쓰기는 그대로 남고(폰 교체·자산 폴백), 이것은 «값이 바뀐 순간» 을 MoveTo 시작보다 앞세우는 것이다.
	void  PushStartDistanceToBlackboard() const;
	float GetAttackStartDistance() const;
	float GetAttackReleaseDistance() const;

	// 걸음을 정해 소유 적에게 적용한다 (design .sw/enemy-gait/design.md §2·§4).
	// 규칙 하나: 「그 분기의 목표까지 남은 거리가 문턱을 넘으면 뛴다」. 문턱은 쌍이다 -
	//   걷는 중이면 RunEnterDistance, 뛰는 중이면 (더 짧은) RunExitDistance 를 본다(히스테리시스).
	//   목표는 역할이 고른다 - 링 슬롯 대기면 슬롯까지, 그 밖이면 대상 사거리까지.
	// 왜 서비스가 아니라 여기인가: 판단에 필요한 것(역할·슬롯·설정·대상)이 전부 이 컴포넌트에 이미 있다.
	//   서비스에 두면 그 넷을 다시 모아야 하고, BTService 가 적 클래스에 직접 묶인다.
	// 부작용: 소유 적의 CMC MaxWalkSpeed 변경(AVBEnemyBase::ApplyGait 경유).
	void UpdateGait();

	// 이 적이 지금 공격 GA 를 실제로 켤 수 있는가(쿨다운/코스트/차단태그 포함).
	// 판정을 재구현하지 않고 엔진 정본 UGameplayAbility::CanActivateAbility 에 위임한다 -
	//   코디네이터가 "쿨다운 중인 적에게 토큰을 주고 교착시키는" 결함을 구조적으로 없앤다.
	bool IsReadyToAttack() const;

	// 이 적이 쓰는 공격 GA 의 InputTag. 해석의 홈은 이 함수 하나다 - BT 태스크도 이것을 부른다.
	// 왜 홈을 모으는가: 두 곳이 각자 자산을 읽으면 '누구에게 준비를 묻는가'(코디네이터)와
	//   '무엇을 발동하는가'(BT)가 조용히 갈릴 수 있다. 그 어긋남은 로그를 한 줄도 안 남긴다.
	FGameplayTag GetAttackInputTag() const;

	// 지금 공격 GA 가 활성인가.
	// 왜 bWasAttacking 래치를 쓰지 않는가: 그 래치는 태그 구독이 만들어 내는 값이다. 이 질문이 필요한
	//   자리가 바로 '그 구독이 실패했는가'를 가리는 안전망이라, 래치로 답하면 자기 기록으로 자기 오류를
	//   검사하는 꼴이 되어 두 경우가 구분되지 않는다. 판정의 홈은 ASC 태그다.
	bool IsAttacking() const;

	// 지금 예고(텔레그래프) 중인가. 코디네이터가 토큰 배분을 잠글 때 쓴다.
	bool IsTelegraphing() const;

	// 슈퍼아머 반격을 원하는가(design .sw/enemy-soldier/design.md §3): 아키타입에 반격 태그가 있고, 창 안에 임계만큼 맞았고,
	//   그 GA 를 지금 켤 수 있다. 참이면 GetAttackInputTag 가 반격 태그를 돌려주고 코디네이터가 배분 1순위·숨 면제로 본다.
	bool WantsArmoredCounter() const;

	// 부팅 1회 계약 검증(서버 전용). 호출자는 AVBEnemyBase::BeginPlay 의 GiveAbility 루프 직후다.
	// 왜 컴포넌트 BeginPlay 가 아닌가: AActor::BeginPlay 가 컴포넌트들의 BeginPlay 를 먼저 돌리고
	//   액터 본문은 그 뒤다. 컴포넌트에서 물으면 부여 루프가 아직 안 돌아 항상 '없다'로 나온다.
	//   판단 자체는 컴포넌트가 소유하고(전투 준비 계약은 그쪽 관심사) 액터는 시점만 정한다.
	void ValidateAttackAbilityContract() const;

	// 이 적 아키타입의 튜닝. 소유자(AVBEnemyBase)의 EnemyConfig 를 위임 조회한다 - 절대 null 아님.
	// 왜 컴포넌트가 자기 config 포인터를 갖지 않는가: 그러면 BP 에서 액터와 컴포넌트에 서로 다른
	//   자산을 꽂을 수 있고, 그 어긋남은 로그 없이 "이 적만 이상하게 행동한다"로만 나타난다.
	const UVBEnemyConfig* GetCombatConfig() const;

private:
	// 코디네이터가 CombatTarget 을 그룹 키로 직접 읽는다. getter(CombatTarget.Get())로는 타겟이 파괴된 뒤
	//   nullptr 가 되어 등록 해제가 자기 그룹을 못 찾는다 - 약참조 원본이 필요하다.
	friend class UVBCombatCoordinatorSubsystem;

	// State.Combat.Attacking 태그 카운트 변화 콜백. 하강 에지가 토큰 반납의 유일한 정상 지점이다.
	void OnAttackingTagChanged(const FGameplayTag Tag, int32 NewCount);

	// State.Combat.HitReacting 상승 에지 = «히트리액트가 실제로 켜진 피격» 한 대. 슈퍼아머 반격의 근거를 여기서 센다.
	//   재트리거는 0→1 이 한 번 더 오므로 한 대 = 한 에지. 슈퍼아머로 히트리액트가 안 켜진 피격은 안 센다 - 슈퍼아머 중 피격은 근거가 아니다.
	void OnHitReactingTagChanged(const FGameplayTag Tag, int32 NewCount);

	// 창 밖 기록을 버리고 창 안 횟수를 돌려준다.
	int32 CountRecentHits(double Now) const;

	// 이 InputTag 의 스펙이 지금 켤 수 있는가(쿨다운·코스트·차단 태그). IsReadyToAttack 과 WantsArmoredCounter 가 같은 판정을 쓴다.
	bool CanActivateAbilityByInputTag(const FGameplayTag& InputTag) const;

	// 구독 해제용 ASC 재획득. BeginPlay/EndPlay 가 같은 방법으로 얻어야 핸들이 어긋나지 않는다.
	UAbilitySystemComponent* GetOwnerASC() const;

	// 내가 지금 노리는 대상. 이 컴포넌트가 소유하는 유일한 전투 사실이다.
	TWeakObjectPtr<AActor> CombatTarget;

	// 계획된 공격. INDEX_NONE / 음수 = 계획 없음. 서버 전용(토큰이 서버 사실이다).
	int32 PlannedVariantIndex  = INDEX_NONE;
	float PlannedStartDistance = -1.0f;
	// 순환 커서 - 후보가 여럿일 때 같은 공격이 연달아 나오지 않게. 리셋하지 않는다(의미가 «지난번에 무엇을 썼나» 다).
	int32 VariantPlanCursor = 0;

	// ASC 태그 이벤트 구독 핸들(서버 전용). 무효면 구독하지 않았다는 뜻.
	FDelegateHandle AttackingTagHandle;
	FDelegateHandle HitReactingTagHandle;

	// 최근 피격 시각(월드 게임시간). 창(ArmoredCounterWindowSeconds) 밖은 CountRecentHits 가 걷어낸다. 공격 시작에 비운다.
	TArray<double> RecentHitTimes;

	// HitReacting 상승 에지 판정용 래치(bWasAttacking 과 같은 이유).
	bool bWasHitReacting = false;

	// 태그 하강 에지 검출용 래치. 상승 없이 내려오는 첫 콜백(카운트 0 유지)을 무시한다.
	bool bWasAttacking = false;
};
