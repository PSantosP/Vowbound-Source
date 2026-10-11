// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Player/VBMenuPlayerController.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "UI/VBUIManagerSubsystem.h"
#include "Vowbound/Vowbound.h"

void AVBMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalController())
	{
		return;
	}
	// 메인 메뉴 화면을 연다. 메뉴 입력 모드·커서·초점은 그 화면의 설정을 CommonUI 가 적용한다(.sw/ui-foundation 3절) -
	//   예전처럼 여기서 SetInputMode(UIOnly) 를 걸면 입력 모드의 주인이 둘이 된다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
	{
		UI->PushScreen(VBGameplayTags::UI_Screen_MainMenu);
	}
	else
	{
		// 조용한 빈 화면 방지.
		VB_LOG(Warning, "[VBMenuPlayerController] UI 관리자를 못 찾았다 — 메뉴가 표시되지 않습니다.");
	}
}
