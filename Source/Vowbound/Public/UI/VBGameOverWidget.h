// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBGameOverWidget.generated.h"

class AVBPlayerController;

/**
 * 사망 시 뜨는 게임오버 오버레이 위젯 (UVBPauseMenuWidget 패턴을 그대로 복제).
 * 이유: BP 디자인 자유를 위해 BindWidget 강제 대신 BlueprintCallable 핸들러만 노출한다.
 *       BP 버튼 OnClicked 가 이 함수들을 호출 → 둘 다 소유 PC 로 위임.
 * 왜 전량 위임인가: 두 버튼 모두 레벨 전환이고, 레벨 전환 + unpause + 입력모드 복원은
 *       AVBPlayerController 가 단일 소스로 소유한다(입력모드 누수 방지 교훈).
 * UVBPauseMenuWidget 과 달리 GetSaveSubsystem() 헬퍼를 두지 않는다.
 *       PauseMenu 가 서브시스템을 직접 부른 건 "저장"이 레벨 전환 없는 순수 데이터 관심사였기 때문이다.
 *       "계속하기"는 로드 결과와 무관하게 OpenLevel 로 수렴하므로 전적으로 PC 소관이다.
 */
UCLASS()
class VOWBOUND_API UVBGameOverWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 계속하기 — 마지막 세이브를 로드해 레벨 재진입. 세이브가 없으면 PC 가 현재 레벨 재시작으로 폴백한다
	// (조용한 no-op 금지). 소유 PC 로 위임.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ContinueFromLastSave();

	// 메뉴로 가기 — 소유 PC 로 위임(AVBPlayerController::QuitToMainMenu 무변경 재사용).
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void QuitToMenu();

protected:
	// 위젯이 붙는 순간 버튼을 감췄다가 지연 후 드러낸다. "GAME OVER" 문구를 먼저 읽히게 하려는 연출이고,
	// 죽자마자 뜬 버튼을 오조작으로 눌러버리는 것도 함께 막는다.
	virtual void NativeConstruct() override;

	// 소유 PlayerController 를 VB 타입으로 캐스트. 실패 시 nullptr(호출부가 방어).
	AVBPlayerController* GetVBOwningController() const;

	// 버튼 2종. WBP_GameOver 의 위젯 이름과 정확히 일치해야 한다(BindWidget 은 불일치 시 컴파일 경고 + 런타임 null).
	// BindWidgetOptional 이 아니라 BindWidget 인 이유: 버튼 없는 게임오버 화면은 빠져나갈 방법이 없어
	// 조용히 넘어가면 안 되는 계약이다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UButton> BtnContinue;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<class UButton> BtnQuit;

	// 문구가 뜬 뒤 버튼이 나타나기까지의 지연(초). 게임이 정지하지 않으므로 실시간과 같다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	float ButtonRevealDelay = 1.0f;

private:
	void RevealButtons();

	FTimerHandle ButtonRevealTimerHandle;
};
