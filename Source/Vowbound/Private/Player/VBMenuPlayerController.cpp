// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Player/VBMenuPlayerController.h"

#include "UI/VBMainMenuWidget.h"
#include "Blueprint/UserWidget.h"
#include "Vowbound/Vowbound.h"

void AVBMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	bShowMouseCursor = true;

	if (!MainMenuWidgetClass)
	{
		// 클래스 미할당은 BP 설정 누락 — 로그로 표면화(조용한 빈 화면 방지).
		VB_LOG(Warning, "[VBMenuPlayerController] MainMenuWidgetClass 미할당 — 메뉴가 표시되지 않습니다.");
		return;
	}

	MainMenuWidget = CreateWidget<UVBMainMenuWidget>(this, MainMenuWidgetClass);
	if (!MainMenuWidget)
	{
		return;
	}

	MainMenuWidget->AddToViewport();

	// 메뉴는 게임 입력을 받지 않고 UI 만 처리. 커서로 조작.
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenuWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
}
