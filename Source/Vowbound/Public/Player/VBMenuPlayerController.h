// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "VBMenuPlayerController.generated.h"

class UVBMainMenuWidget;

/**
 * 메인 메뉴 PlayerController.
 * 이유: UI 전용 입력 모드 + 마우스 커서를 켜고 메인 메뉴 위젯을 생성/표시한다.
 *       게임플레이 PC(AVBPlayerController)와 분리해 메뉴 레벨에서만 쓴다.
 */
UCLASS()
class VOWBOUND_API AVBMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	// 에디터에서 WBP_MainMenu 할당.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBMainMenuWidget> MainMenuWidgetClass;

	// 생성된 메인 메뉴 인스턴스.
	UPROPERTY()
	TObjectPtr<UVBMainMenuWidget> MainMenuWidget;
};
