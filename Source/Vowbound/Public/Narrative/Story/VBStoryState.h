// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBStoryState.generated.h"

/** 이름 붙은 숫자 하나(예: Story.Count.PatientsSaved). 배열 원소라 복제된다 - TMap 은 복제되지 않는다. */
USTRUCT(BlueprintType)
struct FVBStoryCounter
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Story")
	FGameplayTag Tag;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Story")
	int32 Value = 0;
};

/**
 * 세계에 대해 참인 것들 - 켜진 사실 태그와 이름 붙은 카운터. 설계 .sw/narrative 2.1절.
 *
 * 왜 컴포넌트와 가르나: 조건 평가·저장 왕복을 월드 없이 시험하려면 데이터와 연산만 따로 있어야 한다.
 *   복제·권한·바뀜 신호는 UVBStoryStateComponent 가 맡고, 여기는 무엇이 참인지만 안다.
 * 태그인 이유: 오타가 태그 선택기에서 걸리고, 계층 질의(Story.Village.*)가 되고, 이름을 바꿔도 GameplayTagRedirects 로 옛 세이브가 따라온다.
 * 평판은 여기 없다 - GAS 속성(UVBReputationAttributeSet)이 주인이다(설계 5절).
 */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBStoryState
{
	GENERATED_BODY()

	// 켜진 사실만 담는다. 꺼짐 = 없음.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Story")
	FGameplayTagContainer Facts;

	// 0 인 카운터는 담지 않는다 - 없는 것과 0 이 같은 뜻이라 두 가지 표현이 생기지 않게.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Story")
	TArray<FVBStoryCounter> Counters;

	// 정확히 이 태그가 켜져 있나. 부모 태그로 묻지 않는다 - Story.Village 가 켜졌다고 Story.Village.WellCleansed 가 참이 되면 안 된다.
	bool HasFact(FGameplayTag Fact) const;

	// 바뀌었으면 true(같은 값이면 false - 바뀜 신호를 헛되이 보내지 않게).
	bool SetFact(FGameplayTag Fact, bool bOn);

	int32 GetCounter(FGameplayTag Counter) const;

	// 더하고 결과를 돌려준다. 결과가 0 이면 항목을 지운다.
	int32 AddToCounter(FGameplayTag Counter, int32 Delta);

	bool operator==(const FVBStoryState& Other) const;
};
