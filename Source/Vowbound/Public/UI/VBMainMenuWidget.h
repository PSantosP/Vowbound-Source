// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBMenuScreenWidget.h"
#include "VBMainMenuWidget.generated.h"

class UVBSaveGameSubsystem;

/**
 * 메인 메뉴 최상위 위젯.
 * 이유: BP 디자인 자유를 위해 BindWidget 강제 대신 BlueprintCallable 핸들러를 노출한다.
 *       BP 버튼 OnClicked 가 이 함수들을 호출 → C++ 가 SaveSubsystem 연동 로직을 수행.
 *       New Game / Load 의 슬롯 선택 화면 전환은 BP 가 슬롯목록 위젯을 띄워 처리한다.
 */
UCLASS()
class VOWBOUND_API UVBMainMenuWidget : public UVBMenuScreenWidget
{
	GENERATED_BODY()

public:
	// 저장된 게임이 하나라도 있는지 → Continue/Load 버튼 활성화 판단.
	UFUNCTION(BlueprintPure, Category="Vowbound|UI")
	bool HasAnySaveGame() const;

	// 가장 최근(타임스탬프 기준) 슬롯 인덱스. 없으면 -1. "Continue" 가 바로 잇는 슬롯.
	UFUNCTION(BlueprintPure, Category="Vowbound|UI")
	int32 GetMostRecentSlotIndex() const;

	// Continue: 가장 최근 슬롯을 비동기 로드 → 완료 시 게임 레벨 열기.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ContinueMostRecent();

	// New Game: 빈 슬롯을 점유해 새 게임을 시작하고 게임 레벨을 연다.
	// 세이브 적용 플래그를 세우지 않으므로 새 폰은 기본 상태로 스폰된다(Continue 와 대비).
	// 빈 슬롯이 없으면 아무것도 하지 않고 false. 호출한 BP 가 슬롯 선택 UI 를 띄워야 한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	bool StartNewGame();

	// 게임 종료.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void QuitGame();

	// 불러오기 화면으로 전환. 화면/목록 규약은 베이스(UVBMenuScreenWidget)가 소유하고
	// 여기서는 "이 버튼은 Load 다" 라는 용도만 정한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ShowLoadPage();

protected:
	// 로드 완료 콜백 — 성공 시 게임 레벨 오픈.
	UFUNCTION()
	void HandleLoadCompleted(int32 SlotIndex, bool bSuccess);

	// GameInstance 에서 세이브 서브시스템 조회.
	UVBSaveGameSubsystem* GetSaveSubsystem() const;
};
