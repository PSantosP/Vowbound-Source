// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AI/VBBTDecorator_HasAttackToken.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"
#include "AI/VBEnemyCombatComponent.h"

UVBBTDecorator_HasAttackToken::UVBBTDecorator_HasAttackToken()
{
	NodeName = "Has Attack Token";
}

bool UVBBTDecorator_HasAttackToken::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
                                                               uint8*                  NodeMemory) const
{
	const AAIController* Controller = OwnerComp.GetAIOwner();
	const APawn*         Pawn       = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	// 컴포넌트가 없으면 이 폰은 토큰 체계 밖이다. false 가 옳다 - 부재를 '허가'로 읽으면
	// 조율을 받지 않는 폰이 무제한으로 때린다.
	const UVBEnemyCombatComponent* CombatComponent = Pawn->FindComponentByClass<UVBEnemyCombatComponent>();
	return CombatComponent && CombatComponent->HasAttackToken();
}

FString UVBBTDecorator_HasAttackToken::GetStaticDescription() const
{
	// 에디터에서 이 노드가 무엇을 묻는지 한 줄로 보이게 한다. 기반 클래스가 중단 모드를 덧붙인다.
	return FString::Printf(TEXT("%s: 어택 토큰 보유"), *Super::GetStaticDescription());
}
