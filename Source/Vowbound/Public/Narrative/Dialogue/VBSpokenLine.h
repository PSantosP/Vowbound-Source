// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBSpokenLine.generated.h"

class USoundBase;
class UVBSpeakerDefinition;

/**
 * 조종을 멈추지 않는 한 줄(혼잣말·짧은 외침). 설계 .sw/prologue-opening 3.4·10절.
 *
 * 대화(UVBDialogueAsset)와 다른 길인 이유: 대화는 화면을 열고 조종을 멈추고 선택지를 받는다. 이것은 자막 한 줄을 띄우고 지나간다.
 *   «저기요… 들리세요?» 를 대화로 띄우면 깨어나자마자 조종이 멈춘다.
 * 줄은 쌓지 않는다 - 새 줄이 오면 지금 줄을 갈아 끼운다. 지나간 상황의 대사가 늦게 나오지 않게(03 검토 10-03).
 */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBSpokenLine
{
	GENERATED_BODY()

	// 비면 화자 이름 칸을 접는다(주인공 혼잣말).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue")
	TObjectPtr<const UVBSpeakerDefinition> Speaker;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue", meta=(MultiLine=true))
	FText Text;

	// 음성(선택). 소유 클라에서 2D 로 한 번 재생한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue")
	TObjectPtr<USoundBase> Voice;

	// 보이는 시간(초). 0 이면 글자 수로 정한다(UVBSpokenLineComponent 의 읽기 속도).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue", meta=(ClampMin="0.0"))
	float Duration = 0.0f;
};

/**
 * 부른 플레이어의 화면에 한 줄을 띄운다. 조건이 거짓이면 아무것도 안 한다.
 * «한 번만» 은 이 효과가 따로 갖지 않는다 - 조건에 «사실 X 꺼짐» 을 두고 같은 효과 목록에 «사실 X 켜기» 를 둔다(영역의 OnceFact 와 같은 사실 길).
 */
USTRUCT(BlueprintType, meta=(DisplayName="줄 말하기"))
struct VOWBOUND_API FVBEffect_SpeakLine : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue")
	FVBSpokenLine Line;

	// 말하기 직전에 본다. 예: 호출 버튼을 안 눌렀을 때만 «아무도 없어요?».
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
