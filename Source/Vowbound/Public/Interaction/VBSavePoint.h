// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBSavePoint.generated.h"

class UStaticMeshComponent;
class UVBInteractableComponent;

/**
 * 저장 포인트 - 자동 상호작용의 첫 사례(user 결정 2026-09-26: 자동 저장 + 알림). 설계 .sw/interaction/design.md 8.3.
 *
 * 범위에 들어오면 입력 없이 한 번 저장한다. 판정(들어왔나)은 UVBInteractorComponent 가 서버에서 하고,
 *   여기는 완료 신호를 받아 저장 경로 하나(UVBSaveGameSubsystem::SaveCurrentGame)를 부를 뿐이다.
 * 알림은 여기 없다 - HUD 가 저장 완료(OnGameSaveCompleted)를 듣고 띄운다. 일시정지 메뉴 저장에도 같은 알림이 뜬다.
 * 설정 자산의 Mode 는 Auto 여야 한다. 아니면 누를 때만 저장된다(그것도 동작은 하므로 BeginPlay 가 말만 한다).
 */
UCLASS()
class VOWBOUND_API AVBSavePoint : public AActor
{
	GENERATED_BODY()

public:
	AVBSavePoint();

protected:
	virtual void BeginPlay() override;

	// 보이는 표지. 메시는 배치에서 정한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UStaticMeshComponent> Marker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UVBInteractableComponent> Interactable;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);
};
