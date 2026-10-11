// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "VBEconomyConfig.generated.h"

class UVBItemDefinition;

/**
 * 경제의 전역 데이터: 무엇이 돈인가, 모든 상점에 걸리는 값 규칙. 설계 .sw/narrative 13.2절.
 * 프로젝트에 하나뿐이라 소비자가 가리키지 않는다 - UVBEconomySettings 가 이 자산 하나를 가리키고, 쓰는 쪽은 그 설정을 거친다
 *   (사망 디졸브 설정 -> 기본 디졸브 자산과 같은 모양). 설정에 바로 두지 않은 이유: 값 규칙이 인스턴스 구조체 배열이라
 *   자산 저작은 대화·퀘스트에서 검증했지만 ini 왕복은 재지 않았다.
 */
UCLASS(BlueprintType, Const)
class VOWBOUND_API UVBEconomyConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 돈으로 쓰는 아이템. 돈도 아이템이다(설계 3.1절) - «n개 이상» 조건·주기·가져가기·저장을 돈에 대해 한 벌 더 짜지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Economy")
	TObjectPtr<const UVBItemDefinition> Currency;

	// 모든 상점에 거는 값 규칙(예: 평판 50 이상이면 0.8). 상점의 규칙과 함께 곱한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Economy")
	TArray<FVBPriceRule> PriceRules;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
