// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interaction/VBToggleProp.h"
#include "VBLootContainer.generated.h"

class UVBLootTable;

/**
 * 내용물이 든 여닫는 소품(서랍장·상자). 설계 .sw/narrative 14.5 W1 - user 결정 2026-10-02 «남은 몫은 서랍장에 남는다».
 *
 * 열 때 준다: 처음 열면 Table 을 한 번 굴려 남은 몫으로 적고, 들어간 만큼 뺀 나머지를 다시 적는다. 닫기는 주지 않는다. 다시 굴리지 않는다.
 *   주는 때는 상호작용 처리가 다 끝난 뒤(UVBInteractableComponent::OnCompletionApplied) - 완료 사건보다 아이템 사건이 먼저 가지 않게.
 * 남은 몫의 주인 = UVBLootContainerSubsystem(키 = 상호작용 저장 키). 이 액터는 원천(Table)과 주는 일만 든다.
 * 일회용(bOneShot)이면 다 털린 뒤에만 열린 채 잠긴다(IsOneShotSpent). 남은 몫이 있으면 닫았다 다시 열어 나머지를 받는다.
 *   잠그는 손은 바탕 소품이다 - 여기서는 «다 쓰였나» 만 덮고, 바뀔 때 RefreshOneShotLock 을 부른다.
 * 파생 액터인 이유(컴포넌트가 아니라): 잠금의 주인이 소품이라, 컴포넌트가 잠그면 bInteractionEnabled 를 쓰는 주체가 둘이 된다(규칙 26).
 * 완료 효과에 «보상표 주기» 를 같이 달지 않는다 - 그 효과는 남은 몫 없이 따로 준다.
 */
UCLASS()
class VOWBOUND_API AVBLootContainer : public AVBToggleProp
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool IsOneShotSpent() const override;

	// 이 서랍장에 무엇이 들었나. 놓인 자리마다 다르다. 비면 열어도 아무것도 없다(다 털린 것으로 적힌다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Loot")
	TObjectPtr<const UVBLootTable> Table;

private:
	UFUNCTION()
	void HandleCompletionApplied(AActor* InteractingActor);

	void HandleContentsLoaded();

	// 남은 몫 기록의 키. 상호작용 저장 키(기본 = 액터 이름)와 같다. 저장을 끈 대상이면 액터 이름 - 남은 몫은 그래도 이 판 안에서는 지켜야 한다.
	FName GetContentsKey() const;

	FDelegateHandle ContentsLoadedHandle;
};
