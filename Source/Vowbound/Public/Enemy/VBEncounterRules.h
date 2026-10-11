// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBEncounterRules.generated.h"

/**
 * 조우 문(UVBEncounterGateComponent)을 움직이는 규칙. 설계 .sw/cinematic-playback 10절 4, .sw/prologue-opening 3.2.
 *
 * 왜 Enemy/ 인가: 조우 문이 적 갈래의 것이다. 규칙 핵심(VBRuleTypes)은 적을 모른다 - 갈래마다 자기 효과를 자기 곳에 둔다.
 * 대상은 «부른 플레이어» 가 아니라 꼬리표다: 이 월드에서 EncounterTags 가 질의와 겹치는 조우 문 전부.
 */

/** 꼬리표가 맞는 조우를 보류하거나 푼다. 시네마틱 재생이 장면 시작·끝 효과로 건다. */
USTRUCT(BlueprintType, meta=(DisplayName="조우 보류"))
struct VOWBOUND_API FVBEffect_HoldEncounter : public FVBEffect
{
	GENERATED_BODY()

	// 이 꼬리표 중 하나라도 가진 조우 문에 건다. 비면 아무것도 안 한다(월드 전체를 멈추지 않는다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter")
	FGameplayTagContainer EncounterTags;

	// 참 = 보류, 거짓 = 풀기.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Encounter")
	bool bHold = true;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
