// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AI/VBBTDecorator_HasGameplayTag.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"

UVBBTDecorator_HasGameplayTag::UVBBTDecorator_HasGameplayTag()
{
	NodeName = "Has Gameplay Tag";
}

bool UVBBTDecorator_HasGameplayTag::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
                                                               uint8*                  NodeMemory) const
{
	if (!QueryTag.IsValid())
	{
		return false;
	}

	const AAIController* Controller = OwnerComp.GetAIOwner();
	APawn*               Pawn       = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	// 액터에서 ASC 를 얻는 정본 경로. 폰이 직접 들든 PlayerState 가 들든 여기서 흡수된다.
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	if (!ASC)
	{
		return false;
	}

	// 자식 태그까지 매칭한다(GAS 표준). 정확 일치가 필요해지면 그때 옵션을 만든다 -
	// 지금 만들면 쓰지 않는 분기를 검증 없이 들고 가게 된다.
	return ASC->HasMatchingGameplayTag(QueryTag);
}

FString UVBBTDecorator_HasGameplayTag::GetStaticDescription() const
{
	// 태그를 안 고른 노드가 에디터에서 바로 보이게 한다. 부재는 런타임에 조용한 false 라
	// 저작 시점에 드러나야 한다.
	const FString TagText = QueryTag.IsValid() ? QueryTag.ToString() : TEXT("(태그 미지정 - 항상 false)");
	return FString::Printf(TEXT("%s: ASC 태그 %s"), *Super::GetStaticDescription(), *TagText);
}
