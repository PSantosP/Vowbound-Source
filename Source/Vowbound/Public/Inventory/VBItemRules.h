// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBItemRules.generated.h"

class UVBItemDefinition;

/**
 * 아이템을 규칙으로 잇는다(설계 .sw/narrative 3.1절의 «주기·가져가기·n개 이상», 12.3절).
 * 퀘스트 보상·대화 선택지·퀘스트 «조건» 목표가 쓴다. 넣고 빼는 길은 여전히 UVBInventoryComponent API 하나다 - 여기서는 그것을 부르기만 한다.
 * 왜 Story 가 아니라 Inventory 폴더인가: 사실·규칙의 바탕(Story)이 인벤토리를 알면 의존이 거꾸로 선다.
 */

/** 부른 플레이어에게 아이템을 준다. 상한에서 잘리면 들어간 만큼만(인벤토리 규칙). */
USTRUCT(BlueprintType, meta=(DisplayName="아이템 주기"))
struct VOWBOUND_API FVBEffect_GiveItem : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item")
	TObjectPtr<const UVBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item", meta=(ClampMin="1"))
	int32 Count = 1;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/** 부른 플레이어에게서 아이템을 가져간다. 모자라면 아무것도 안 가져간다(반쪽 거래 없음). */
USTRUCT(BlueprintType, meta=(DisplayName="아이템 가져가기"))
struct VOWBOUND_API FVBEffect_TakeItem : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item")
	TObjectPtr<const UVBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item", meta=(ClampMin="1"))
	int32 Count = 1;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/** 부른 플레이어가 가진 수가 값과 비교해 맞나. */
USTRUCT(BlueprintType, meta=(DisplayName="아이템 개수"))
struct VOWBOUND_API FVBCondition_ItemCount : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item")
	TObjectPtr<const UVBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item")
	EVBRuleCompare Compare = EVBRuleCompare::AtLeast;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item", meta=(ClampMin="0"))
	int32 Count = 1;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};
