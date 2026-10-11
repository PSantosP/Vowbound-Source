// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "VBQuestRules.generated.h"

class UVBQuestDefinition;

/**
 * 퀘스트를 규칙으로 잇는다. 설계 .sw/narrative 12.3절.
 * 대화 선택지가 퀘스트를 시작하고, 대화 진입점이 퀘스트 상태로 갈린다 - 대화 코드는 퀘스트를 모른다.
 * 퀘스트를 «진행» 시키는 효과는 일부러 없다 - 진행의 주인은 사건 하나다(설계 2.2절).
 */

/** 부른 플레이어에게 퀘스트를 준다. 이미 일지에 있으면 아무것도 안 한다. */
USTRUCT(BlueprintType, meta=(DisplayName="퀘스트 시작"))
struct VOWBOUND_API FVBEffect_StartQuest : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Quest")
	TObjectPtr<const UVBQuestDefinition> Quest;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/** 부른 플레이어의 퀘스트 상태가 이것인가. Stage 가 0 이상이면 진행 중일 때 그 단계까지 맞아야 한다. */
USTRUCT(BlueprintType, meta=(DisplayName="퀘스트 상태"))
struct VOWBOUND_API FVBCondition_QuestState : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Quest")
	TObjectPtr<const UVBQuestDefinition> Quest;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Quest")
	EVBQuestStatus Status = EVBQuestStatus::Active;

	// -1 = 단계 상관없음. 0 이상 = 진행 중일 때 이 단계(0 부터)여야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Quest", meta=(ClampMin="-1"))
	int32 Stage = -1;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};
