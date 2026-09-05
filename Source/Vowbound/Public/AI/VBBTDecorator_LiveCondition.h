// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "VBBTDecorator_LiveCondition.generated.h"

/** 관찰형 데코레이터의 인스턴스별 메모리. 직전 프레임의 조건값 하나뿐이다. */
struct FVBLiveConditionMemory
{
	bool bLastValue = false;
};

/**
 * 매 프레임 원본에 다시 묻고, 답이 바뀌면 트리에 재탐색을 요청하는 데코레이터의 공통 뼈대.
 *
 * 무엇을 묻는지는 자식이 정한다(CalculateRawConditionValue). 이 클래스는 '언제 알리는가'만 소유한다.
 *
 * 왜 폴링인가: 이벤트로 알리려면 "값이 바뀌었다"를 원본이 쏴야 하는데, 그 이벤트를 만들면
 *   사실의 홈이 원본과 이벤트 둘이 된다. 폴링은 매 틱 원본을 다시 읽으므로 홈이 하나 그대로다.
 *   엔진 선례가 UBTDecorator_TimeLimit 이다(TickNode 에서 OwnerComp.RequestExecution).
 *
 * 왜 틱 간격을 두지 않는가: 간격을 두면 그 간격이 곧 반응 지연의 격자가 된다.
 *   이 뼈대가 존재하는 이유가 격자를 없애는 것이므로 매 프레임 본다.
 *   비용은 노드가 관련(relevant)한 동안 적 1기당 질의 1회다.
 *
 * 왜 베이스로 뽑았는가: 같은 기계가 두 곳(어택 토큰 / ASC 태그)에 필요해졌다. 규칙 12 의
 *   "3번째에 추출"보다 이르지만, 유사 코드가 아니라 동일 기계이고 세 번째를 기다리면
 *   그때는 세 곳을 함께 고쳐야 한다. 자식이 실수로 관찰을 빠뜨리는 것도 구조적으로 막힌다.
 *
 * 자산 저작 계약: 이 데코레이터를 쓰는 노드는 FlowAbortMode 를 설정해야 의미가 있다.
 *   설정하지 않으면 재탐색 요청이 아무 분기도 끊지 못한다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBBTDecorator_LiveCondition : public UBTDecorator
{
	GENERATED_BODY()

public:
	UVBBTDecorator_LiveCondition();

protected:
	// 조건이 바뀌는 순간을 잡아 트리에 재탐색을 요청한다. 판정 자체는 자식의
	//   CalculateRawConditionValue 가 한다 - 여기서 다시 쓰면 같은 판단이 두 곳에 생긴다.
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 트리 자산은 AI 여러 기가 공유하므로 직전 값은 노드 멤버가 아니라 인스턴스 메모리에 든다.
	//   멤버에 들면 적들이 서로의 직전 값을 덮어쓴다.
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
};
