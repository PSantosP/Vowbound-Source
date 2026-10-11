// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Templates/SubclassOf.h"
#include "VBPlayerRules.generated.h"

class UGameplayEffect;

/**
 * 플레이어의 GAS(ASC·평판 속성)를 읽고 쓰는 규칙. 설계 .sw/narrative 13.1절 2번.
 *
 * 왜 Narrative/Story 가 아니라 여기인가: 규칙 핵심(VBRuleTypes)은 사실 저장소와 엔진만 보고, GAS 를 아는 규칙은 게임 층에 둔다.
 *   그래야 서사 핵심을 GAS 없이 떼어 쓸 수 있다(구조 조사 2026-10-01 3.2절 T1). Player/ 는 서사와 GAS 를 둘 다 아는 게임 층이다.
 * 구조체의 리플렉션 경로(/Script/Vowbound.<이름>)는 파일과 무관하다 - 옮겨도 자산에 적힌 조건·효과는 그대로 읽힌다.
 */

/** 부른 플레이어의 평판이 값과 비교해 맞나. 읽기만 한다 - 평판의 주인은 GAS 속성이다. */
USTRUCT(BlueprintType, meta=(DisplayName="평판"))
struct VOWBOUND_API FVBCondition_Reputation : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	EVBRuleCompare Compare = EVBRuleCompare::AtLeast;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	float Value = 0.0f;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/**
 * 부른 플레이어에게 GE 를 건다. 평판을 바꾸는 길이 이것이다 - 적 처치·의료 행위와 같은 GE + SetByCaller(Data.ReputationChange) 모양.
 * 평판만을 위한 효과 타입을 두지 않는 이유: 평판 변경의 주인은 GE 이고, 체력·버프도 같은 길로 줄 수 있다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="GE 걸기"))
struct VOWBOUND_API FVBEffect_ApplyGameplayEffect : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	TSubclassOf<UGameplayEffect> Effect;

	// SetByCaller 태그 -> 값. 예: Data.ReputationChange -> 10.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story", meta=(Categories="Data"))
	TMap<FGameplayTag, float> SetByCallerMagnitudes;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/**
 * 부른 플레이어의 지금 상태를 저장하되, 불러올 때 돌아올 자리는 RestoreAt 이다. 설계 .sw/prologue-opening 3.6a.
 * 프롤로그 무기 획득: 상태는 «무기를 가졌다», 자리는 창 착지 부근. 줍는 순간의 자리를 그대로 쓰면 다시 줍는 장면처럼 읽힌다(03 권고).
 * RestoreAt 이 비었거나 못 찾으면 지금 자리로 저장하고 경고한다 - 저장을 버리는 것보다 낫다.
 * 저장 실패는 HUD 가 OnGameSaveCompleted 로 알린다. 이 효과의 완료를 기다리는 규칙은 두지 않는다(설계 8.6).
 */
USTRUCT(BlueprintType, meta=(DisplayName="체크포인트 저장"))
struct VOWBOUND_API FVBEffect_SaveCheckpoint : public FVBEffect
{
	GENERATED_BODY()

	// 돌아올 자리의 표지(같은 맵에 놓인 액터, 보통 TargetPoint). 위치와 야우만 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Story")
	TSoftObjectPtr<AActor> RestoreAt;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
