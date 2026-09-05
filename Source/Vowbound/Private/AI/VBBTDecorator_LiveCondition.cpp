// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AI/VBBTDecorator_LiveCondition.h"

#include "BehaviorTree/BehaviorTreeComponent.h"

UVBBTDecorator_LiveCondition::UVBBTDecorator_LiveCondition()
{
	// 어느 알림을 받을지는 오버라이드한 함수로 정해진다. 이 매크로가 없으면 bNotifyTick 이 꺼진 채
	// 남아 TickNode 가 한 번도 불리지 않고, 그러면 이 노드는 중단을 걸지 못한다.
	//
	// 자식에서 다시 부를 필요가 없다: 매크로가 보는 것은 TickNode/OnBecomeRelevant 의 오버라이드
	// 여부이고 그 둘은 여기서 이미 오버라이드돼 있어, 자식이 상속만 해도 플래그가 그대로 유효하다.
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();
}

void UVBBTDecorator_LiveCondition::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	// 현재 값으로 씨를 뿌린다. 기본값 false 로 두면 이미 조건을 만족한 채 관련해진 적이
	// 첫 틱에 '변했다'로 읽혀 방금 시작한 분기를 스스로 끊는다.
	CastInstanceNodeMemory<FVBLiveConditionMemory>(NodeMemory)->bLastValue =
		CalculateRawConditionValue(OwnerComp, NodeMemory);
}

void UVBBTDecorator_LiveCondition::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FVBLiveConditionMemory* Memory = CastInstanceNodeMemory<FVBLiveConditionMemory>(NodeMemory);
	const bool              bValue = CalculateRawConditionValue(OwnerComp, NodeMemory);
	if (bValue == Memory->bLastValue)
	{
		return;
	}

	Memory->bLastValue = bValue;

	// 어느 방향이든 트리에 알린다. 참이 되면 그 분기로 들어가야 하고(격자 없이),
	// 거짓이 되면 하던 것을 그 자리에서 끊어야 한다.
	// 실제로 무엇을 끊고 무엇을 여는지는 이 노드의 FlowAbortMode 가 자산에서 정한다.
	OwnerComp.RequestExecution(this);
}

uint16 UVBBTDecorator_LiveCondition::GetInstanceMemorySize() const
{
	return sizeof(FVBLiveConditionMemory);
}

void UVBBTDecorator_LiveCondition::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
                                                    EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FVBLiveConditionMemory>(NodeMemory, InitType);
}

void UVBBTDecorator_LiveCondition::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
                                                 EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FVBLiveConditionMemory>(NodeMemory, CleanupType);
}
