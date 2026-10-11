// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "VBDialogueSpeakerComponent.generated.h"

class UVBDialogueAsset;

/**
 * «나에게 말을 걸 수 있다». NPC 액터에 붙인다. 설계 .sw/narrative 11.3절.
 *
 * 같은 액터의 상호작용 대상(UVBInteractableComponent)이 완료를 알리면(서버) 말을 건 플레이어의 대화 컴포넌트에 대화를 연다.
 *   상호작용(거리·시야·프롬프트·입력)을 새로 짓지 않는다 - 문·줍기와 같은 길이다. 프롬프트의 「대화」 는 상호작용 설정 자산이 든다.
 * 여는 조건이 거짓이면 열지 않는다. 대화 자산의 진입점 조건과 다른 점: 여는 조건은 «이 사람이 지금 말을 받나»(퀘스트가 잠근 인물 등),
 *   진입점은 «받는다면 무슨 말부터 하나» 다.
 * NPC 가 상호작용 대상을 여럿 가지면 전부 대화를 연다 - 한 NPC 에 대화 하나가 기본이다.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBDialogueSpeakerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBDialogueSpeakerComponent();

	const UVBDialogueAsset* GetDialogue() const { return Dialogue; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue")
	TObjectPtr<const UVBDialogueAsset> Dialogue;

	// 전부 참일 때만 말을 받는다. 비면 늘 받는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Dialogue", meta=(BaseStruct="/Script/Vowbound.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> OpenConditions;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);
};
