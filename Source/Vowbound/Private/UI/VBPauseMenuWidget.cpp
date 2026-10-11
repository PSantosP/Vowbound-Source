// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBPauseMenuWidget.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerState.h"
#include "Player/VBPlayerController.h"
#include "UI/VBReputationBarWidget.h"
#include "Vowbound/Vowbound.h"

UVBPauseMenuWidget::UVBPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 일시정지 메뉴의 계약(.sw/ui-foundation 3절): 메뉴 입력만 받고, 게임을 멈추고, 뒤로 가기로 닫힌다.
	InputMode = EVBScreenInputMode::Menu;
	bPausesGame = true;
	bIsBackHandler = true;
}

void UVBPauseMenuWidget::NativeOnActivated()
{
	Super::NativeOnActivated();

	// BP 가 바를 안 넣었으면 아무 일도 하지 않는다(BindWidgetOptional 계약).
	if (!ReputationBar)
	{
		return;
	}

	// ASC 의 홈은 AVBPlayerState 다. 구체 클래스를 캐스트하지 않고 인터페이스 경로로 묻는다 -
	//   데미지 hub 가 Cast<AVBCharacter> 로 가드를 판정해 적이 영원히 가드 못하게 됐던 것과
	//   같은 종류의 결합을 UI 에 새로 만들지 않는다(FIND-057).
	const AVBPlayerController* PC = GetVBOwningController();
	UAbilitySystemComponent* ASC = PC
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(PC->PlayerState)
		: nullptr;

	if (!ASC)
	{
		// 여기서 조용히 돌아가면 화면에는 0 에 멈춘 바만 남고 이유가 어디에도 안 남는다.
		// 바를 배치했다는 것 자체가 "보이길 기대한다"는 뜻이므로 못 채운 것은 결함이다.
		VB_LOG(Warning, "%s: 명성 바를 배치했으나 ASC 를 얻지 못했다 -> 바가 빈 채로 남는다. "
		                "PlayerState 가 IAbilitySystemInterface 를 답하는지 확인하라", *GetName());
		return;
	}

	ReputationBar->InitializeWithASC(ASC);
}

void UVBPauseMenuWidget::ShowSavePage()
{
	// 실제 저장은 슬롯을 고른 뒤 UVBSaveSlotListWidget::SelectSlot 이 수행한다.
	// 여기서 바로 저장하지 않는 이유는 헤더에 적었다(자동 선택 = 조용한 덮어쓰기).
	ShowSlotPage(EVBSlotListMode::Save);
}

void UVBPauseMenuWidget::ResumeGame()
{
	// 화면을 끄면 스택이 내리고, 정지 해제와 입력 복귀는 UI 층이 한다(헤더 주석).
	DeactivateWidget();
}

void UVBPauseMenuWidget::QuitToMenu()
{
	if (AVBPlayerController* PC = GetVBOwningController())
	{
		PC->QuitToMainMenu();
	}
}

AVBPlayerController* UVBPauseMenuWidget::GetVBOwningController() const
{
	return Cast<AVBPlayerController>(GetOwningPlayer());
}
