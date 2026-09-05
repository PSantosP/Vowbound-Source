// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBGameOverWidget.h"

#include "Player/VBPlayerController.h"
#include "Components/Button.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UVBGameOverWidget::ContinueFromLastSave()
{
	// 세이브 조회/비동기 로드/레벨 전환은 전부 PC 단일 소스에서(위젯은 의도만 전달).
	if (AVBPlayerController* PC = GetVBOwningController())
	{
		PC->ContinueFromLastSave();
	}
}

void UVBGameOverWidget::QuitToMenu()
{
	// 일시정지 메뉴와 동일 경로 재사용 — 게임오버 전용 종료 코드는 0줄.
	if (AVBPlayerController* PC = GetVBOwningController())
	{
		PC->QuitToMainMenu();
	}
}

AVBPlayerController* UVBGameOverWidget::GetVBOwningController() const
{
	return Cast<AVBPlayerController>(GetOwningPlayer());
}

void UVBGameOverWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 버튼을 먼저 감춘다. Collapsed 가 아니라 Hidden 인 이유: Collapsed 는 레이아웃에서 빠져 문구가
	// 위로 밀렸다가 버튼이 나타날 때 다시 내려앉는다. Hidden 은 자리를 유지해 화면이 흔들리지 않는다.
	if (BtnContinue)
	{
		BtnContinue->SetVisibility(ESlateVisibility::Hidden);
	}
	if (BtnQuit)
	{
		BtnQuit->SetVisibility(ESlateVisibility::Hidden);
	}

	// 지연이 0 이하면 타이머 없이 즉시 드러낸다(노브를 0 으로 두면 연출을 끄는 것과 같게).
	if (ButtonRevealDelay <= 0.0f)
	{
		RevealButtons();
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			ButtonRevealTimerHandle, this, &UVBGameOverWidget::RevealButtons, ButtonRevealDelay, false);
	}
	else
	{
		// 월드가 없으면 타이머를 걸 수 없다. 버튼이 영영 안 나오는 것보다는 즉시 노출이 낫다.
		RevealButtons();
	}
}

void UVBGameOverWidget::RevealButtons()
{
	if (BtnContinue)
	{
		BtnContinue->SetVisibility(ESlateVisibility::Visible);
	}
	if (BtnQuit)
	{
		BtnQuit->SetVisibility(ESlateVisibility::Visible);
	}
}
