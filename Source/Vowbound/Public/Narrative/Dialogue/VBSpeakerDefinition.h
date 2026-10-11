// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBSpeakerDefinition.generated.h"

class UTexture2D;

/**
 * 말하는 사람 하나. 설계 .sw/narrative 3.2절·11.1절.
 * 대사 노드는 화자를 태그로만 든다 - 이름·초상화가 바뀌어도 대사 자산을 고치지 않는다. 태그를 이름으로 푸는 것은 대화 자산의 Speakers 목록이다.
 */
UCLASS(BlueprintType, Const)
class VOWBOUND_API UVBSpeakerDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dialogue", meta=(Categories="Speaker"))
	FGameplayTag SpeakerId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dialogue")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Dialogue")
	TSoftObjectPtr<UTexture2D> Portrait;
};
