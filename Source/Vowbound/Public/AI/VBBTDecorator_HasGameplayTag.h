// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AI/VBBTDecorator_LiveCondition.h"
#include "VBBTDecorator_HasGameplayTag.generated.h"

/**
 * 이 폰의 ASC 가 지정한 태그를 갖고 있는가.
 *
 * 무엇: 태그가 프로퍼티다. 게이트할 상태가 하나 늘어도 자산 저작만 하면 된다.
 *
 * 왜 태그별 클래스를 만들지 않는가: 상태 하나마다 C++ 클래스가 늘면 "적이 언제 멈추는가"를
 *   바꾸는 데 빌드가 필요해진다. 게이트 대상은 저작 결정이지 코드 결정이 아니다.
 *
 * 왜 UVBBTDecorator_HasAttackToken 을 확장하지 않는가: 질문의 출처가 다르다.
 *   그쪽은 코디네이터(그룹 결정)에 묻고 이쪽은 ASC(개체 상태)에 묻는다. 합치면 두 진실이 섞인다.
 *
 * 왜 관찰형인가: 경직처럼 분기 도중에 켜지고 꺼지는 상태를 게이트하기 때문이다.
 *   진입 시점에만 보면 맞는 도중에도 하던 이동을 끝까지 계속한다 - 그것이 이 노드를 만든 이유다.
 *
 * 첫 사용처: BT_Infected 의 Engage / Move To Ring Slot 두 분기에 State.Combat.Staggered 를
 *   반전(bInverseCondition)으로 얹어, 맞는 동안은 아무 명령도 내지 않고 Idle 로 떨어지게 한다.
 *   이동을 GA 나 컨트롤러에서 멈추지 않는 이유: 명령을 내는 주체가 BT 라, 속도만 지우면
 *   BT 가 다음 틱에 다시 명령해 두 시스템이 싸운다.
 */
UCLASS()
class VOWBOUND_API UVBBTDecorator_HasGameplayTag : public UVBBTDecorator_LiveCondition
{
	GENERATED_BODY()

public:
	UVBBTDecorator_HasGameplayTag();

protected:
	// 물어볼 태그. 비어 있으면 항상 false 다 - 부재를 참으로 읽으면 저작을 빠뜨린 노드가
	//   조용히 모든 분기를 막아 적이 아무것도 하지 않게 된다.
	// 자식 태그도 함께 매칭된다(GAS 표준 의미). State.Combat 하나로 그 아래 전부를 게이트할 수 있다.
	UPROPERTY(EditAnywhere, Category="Vowbound|AI")
	FGameplayTag QueryTag;

	virtual bool    CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;
};
