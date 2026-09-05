// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBMainMenuWidget.h"

#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UI/VBSaveSlotListWidget.h"
#include "Kismet/KismetSystemLibrary.h"

// "가장 최근 슬롯" 정책은 UVBSaveGameSubsystem 이 소유한다. 아래 둘은 얇은 위임일 뿐이다.
// 왜: 게임오버 "계속하기"(AVBPlayerController::ContinueFromLastSave)가 같은 정책을 필요로 하는데, 정책이
//   위젯에 남아 있으면 두 벌로 복사되어 한쪽만 고쳐졌을 때 조용히 다른 세이브를 연다. 순수 이동(로직 무변경).
bool UVBMainMenuWidget::HasAnySaveGame() const
{
	const UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	return Subsystem ? Subsystem->HasAnySaveGame() : false;
}

int32 UVBMainMenuWidget::GetMostRecentSlotIndex() const
{
	const UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	return Subsystem ? Subsystem->GetMostRecentSlotIndex() : INDEX_NONE;
}

void UVBMainMenuWidget::ContinueMostRecent()
{
	UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	if (!Subsystem)
	{
		return;
	}

	const int32 RecentSlot = GetMostRecentSlotIndex();
	if (RecentSlot < 0)
	{
		return;
	}

	// 로드 완료 후 레벨을 열기 위해 1회용으로 델리게이트 구독.
	Subsystem->OnGameLoadCompleted.AddDynamic(this, &UVBMainMenuWidget::HandleLoadCompleted);
	Subsystem->LoadGameSlot(RecentSlot);
}

bool UVBMainMenuWidget::StartNewGame()
{
	UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	if (!Subsystem)
	{
		return false;
	}

	// 슬롯을 잡지 않는다. New Game 은 "지금부터 플레이"이지 "슬롯 점유"가 아니다(2026-08-12 확정).
	// 종전에는 여기서 빈 슬롯을 찾아 즉시 점유했는데, 그 결과 슬롯 수만큼만 새 게임을 시작할 수 있었고
	//   (3슬롯이면 3번) 그 뒤로는 버튼이 조용히 아무 일도 하지 않았다.
	// 슬롯은 플레이어가 실제로 저장할 때 정해진다. 슬롯을 골라 시작하는 경로는
	//   UVBSaveSlotListWidget::SelectSlot(NewGame 모드) -> StartNewGameInSlot 으로 따로 있다.
	Subsystem->StartNewGame();

	Subsystem->TravelToGameplayLevel();
	return true;
}

void UVBMainMenuWidget::HandleLoadCompleted(int32 SlotIndex, bool bSuccess)
{
	UVBSaveGameSubsystem* Subsystem = GetSaveSubsystem();
	if (Subsystem)
	{
		// 1회용 구독 해제(중복 호출 방지).
		Subsystem->OnGameLoadCompleted.RemoveDynamic(this, &UVBMainMenuWidget::HandleLoadCompleted);
	}

	if (bSuccess && Subsystem)
	{
		Subsystem->TravelToGameplayLevel();
	}
}

void UVBMainMenuWidget::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, /*bIgnorePlatformRestrictions=*/false);
}

void UVBMainMenuWidget::ShowLoadPage()
{
	ShowSlotPage(EVBSlotListMode::Load);
}

UVBSaveGameSubsystem* UVBMainMenuWidget::GetSaveSubsystem() const
{
	return UVBSaveGameSubsystem::Get(this);
}
