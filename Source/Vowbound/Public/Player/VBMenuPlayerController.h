// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "VBMenuPlayerController.generated.h"

/**
 * 메인 메뉴 PlayerController.
 * 이유: 게임플레이 PC(AVBPlayerController)와 분리해 메뉴 레벨에서만 쓴다.
 *       메뉴 화면을 여는 요청만 한다. 화면 클래스는 Project Settings > Vowbound UI 의 UI.Screen.MainMenu 가 정하고,
 *       입력 모드·커서는 그 화면 설정을 CommonUI 가 적용한다(.sw/ui-foundation).
 */
UCLASS()
class VOWBOUND_API AVBMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
};
