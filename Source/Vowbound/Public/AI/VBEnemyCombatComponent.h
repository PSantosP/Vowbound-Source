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

	// 구독 해제용 ASC 재획득. BeginPlay/EndPlay 가 같은 방법으로 얻어야 핸들이 어긋나지 않는다.
	UAbilitySystemComponent* GetOwnerASC() const;

	// 내가 지금 노리는 대상. 이 컴포넌트가 소유하는 유일한 전투 사실이다.
	TWeakObjectPtr<AActor> CombatTarget;

	// ASC 태그 이벤트 구독 핸들(서버 전용). 무효면 구독하지 않았다는 뜻.
	FDelegateHandle AttackingTagHandle;

	// 태그 하강 에지 검출용 래치. 상승 없이 내려오는 첫 콜백(카운트 0 유지)을 무시한다.
	bool bWasAttacking = false;
};
