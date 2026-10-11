// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBWorldItem.generated.h"

class UStaticMeshComponent;
class UVBInteractableComponent;
class UVBItemDefinition;

/**
 * 땅에 놓인 아이템 - 상호작용으로 줍는다. 설계 .sw/narrative 10.3절.
 *
 * 줍기는 상호작용 시스템을 그대로 탄다(동작 없는 Tap 대상은 바로 완료된다) - 탐색·프롬프트·거리 규칙을 새로 짜지 않는다.
 * 주운 것은 상호작용 대상의 «켜짐» 이다. 그래서 재획득 방지는 새로 짓지 않는다 - 켜짐 저장(Progress.InteractableStates, 키 = 액터 이름)이
 *   Continue 때 이 대상을 켜진 채로 돌려놓고, 켜진 줍기는 숨는다.
 * 줍기 설정(Tap·「줍기」 문구·거리)은 BP 파생(BP_WorldItem)의 상호작용 컴포넌트에 꽂는다 - 맵에는 그 BP 를 놓는다.
 */
UCLASS()
class VOWBOUND_API AVBWorldItem : public AActor
{
	GENERATED_BODY()

public:
	AVBWorldItem();

	virtual void OnConstruction(const FTransform& Transform) override;

	// 서버에서만. 넘친 몫을 Anchor 의 발밑에 줍기로 놓는다(.sw/narrative 14.5 덧붙임 - 적 드롭이 가득 찬 인벤토리에 넘칠 때).
	//   클래스·벌림·바닥 추적은 UVBItemSettings. 놓은 줍기는 저장하지 않는다(이름이 매번 달라 다시 찾을 수 없다) - 이어하기 뒤에는 없다.
	//   SpreadIndex = 같은 자리에 몇 번째로 놓는가(0 = 가운데). 못 놓으면 이유를 경고하고 nullptr.
	static AVBWorldItem* SpawnDrop(const AActor* Anchor, const UVBItemDefinition* InItem, int32 InCount, int32 SpreadIndex);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Item")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 무엇을 몇 개. 놓인 자리마다 정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item")
	TObjectPtr<const UVBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Item", meta=(ClampMin="1"))
	int32 Count = 1;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);

	// 주운 상태면 숨기고 상호작용을 끈다. 시작 상태(Continue 로 켜진 채 돌아온 것 포함)와 바뀐 때 모두 이 길이다.
	void ApplyPickedUp(bool bPickedUp);
};
