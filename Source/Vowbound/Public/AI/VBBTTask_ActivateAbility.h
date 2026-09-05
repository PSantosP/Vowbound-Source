// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpecHandle.h"
#include "VBBTTask_ActivateAbility.generated.h"

class UAbilitySystemComponent;
struct FAbilityEndedData;

/**
 * BT Task: GAS Ability 발동
 * Pawn의 ASC에서 AbilityTag와 매칭되는 Ability를 TryActivateAbility
 *
 * 노드 인스턴싱: bWaitForAbilityEnd 가 ASC 델리게이트를 걸어야 하므로 생성자에서 bCreateNodeInstance=true 로 둔다.
 *  인스턴싱하지 않으면 이 노드는 트리 자산이 공유하는 템플릿이라 AI 여러 기가 같은 델리게이트 상태를 밟는다.
 *  엔진 선례: BTTask_PlayAnimation.cpp::UBTTask_PlayAnimation 생성자(타이머를 쓰기 위해 같은 이유로 인스턴싱).
 */
UCLASS()
class VOWBOUND_API UVBBTTask_ActivateAbility : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UVBBTTask_ActivateAbility();

	// 발동할 Ability 의 InputTag (BT 에디터에서 설정).
	// 비워 두면 이 폰의 아키타입 설정(UVBEnemyConfig::AttackAbilityInputTag)에서 읽는다.
	// 왜 2단 사다리인가: 하나뿐인 트리가 여러 아키타입을 태우므로 태그의 홈은 자산이어야 한다.
	//   그럼에도 이 필드를 남기는 이유는 "이 노드에서는 반드시 이 어빌리티"라는 스크립트 연출
	//   (보스 페이즈 전용 트리 등)이 실재하는 요구이기 때문이다. 둘 다 비면 Warning 후 Failed.
	UPROPERTY(EditAnywhere, Category="Vowbound|Ability")
	FGameplayTag AbilityTag;

	// 어빌리티가 끝날 때까지 이 태스크를 붙잡아 둘지 여부.
	// false(기본)면 종전대로 즉시 Succeeded/Failed - 기존 트리 동작이 그대로 보존된다.
	// true 면 ASC 의 OnAbilityEnded 를 기다린다. 왜 필요한가: 공격 길이를 BTTask_Wait 로 흉내내면
	//   몽타주 길이와 대기 시간이 이중 저작 상태가 된다. 이 플래그가 그 이중 저작을 없앤다(공격 길이의 홈=몽타주).
	UPROPERTY(EditAnywhere, Category="Vowbound|Ability")
	bool bWaitForAbilityEnd = false;

	// 취소로 끝난 어빌리티를 성공으로 볼지 여부. 기본 false - 경직으로 잘린 공격은 실패다.
	UPROPERTY(EditAnywhere, Category="Vowbound|Ability", meta=(EditCondition="bWaitForAbilityEnd"))
	bool bSucceedOnCancel = false;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 중단 경로. 델리게이트를 반드시 놓는다 - 놓지 않으면 다음 활성화의 종료가 이 노드로 잘못 들어온다.
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 모든 종료 경로의 수렴점(성공/실패/중단). 구독 해제를 여기에도 두어 누수 경로를 남기지 않는다.
	virtual void OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTNodeResult::Type TaskResult) override;

	// BT 에디터에서 노드 이름 표시
	virtual FString GetStaticDescription() const override;

private:
	// 이 노드가 발동할 InputTag. 노드 설정 우선, 비면 폰의 아키타입 설정.
	FGameplayTag ResolveAbilityTag(const APawn* Pawn) const;

	// ASC OnAbilityEnded 콜백. 우리가 켠 그 스펙일 때만 반응한다.
	void OnAbilityEnded(const FAbilityEndedData& EndedData);

	// 구독 해제. 두 번 불러도 안전하다(핸들 무효 검사).
	void UnbindAbilityEnded();

	// 대기 중인 활성화의 스펙. 콜백이 다른 어빌리티의 종료인지 가리는 데 쓴다.
	FGameplayAbilitySpecHandle WaitingSpecHandle;

	// 구독 대상 ASC. 약참조인 이유: 폰이 먼저 파괴되면 해제 시점에 이미 없다.
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;

	// FinishLatentTask 대상. 콜백이 비동기로 오므로 실행 시점의 트리 컴포넌트를 들고 있어야 한다.
	TWeakObjectPtr<UBehaviorTreeComponent> CachedOwnerComp;

	FDelegateHandle AbilityEndedHandle;

	// 종료 통지를 기다리는 중인가. 콜백이 중복/지연 도착해도 한 번만 처리하게 하는 래치.
	bool bWaitingForAbilityEnd = false;

	// ExecuteTask 안에서 TryActivateAbility 를 부르는 동안인가.
	// 어빌리티가 활성화와 같은 스택에서 즉시 끝나는 경로(몽타주 미설정 폴백 등)가 실재하는데,
	// 그때 FinishLatentTask 를 부르면 아직 InProgress 를 반환하지도 않은 태스크를 끝내려 든다.
	bool bInsideExecute = false;

	// 위 동기 종료가 실제로 일어났는가. ExecuteTask 가 반환값으로 결과를 돌려주는 데 쓴다.
	bool bEndedDuringExecute = false;

	// 마지막 종료가 취소였는가. bSucceedOnCancel 과 함께 최종 결과를 정한다.
	bool bLastEndWasCancelled = false;
};
