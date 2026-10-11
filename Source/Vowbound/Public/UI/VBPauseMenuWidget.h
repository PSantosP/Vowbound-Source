// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBMenuScreenWidget.h"
#include "VBPauseMenuWidget.generated.h"

class AVBPlayerController;

/**
 * 게임 중 일시정지 오버레이 위젯 (VBMainMenuWidget 패턴).
 * 화면 전환(설정 페이지 진입/복귀)은 UVBMenuScreenWidget 이 소유한다 - 메인 메뉴와 같은 규약을 쓴다.
 * 이유: BP 디자인 자유를 위해 BindWidget 강제 대신 BlueprintCallable 핸들러를 노출한다.
 *       BP 버튼 OnClicked 가 이 함수들을 호출 → 저장은 서브시스템, 계속/메뉴로는 소유 PC 로 위임.
 * 왜 위임: 레벨 전환은 PC 가 소유한다. 정지와 입력 모드는 UI 층이 소유한다(.sw/ui-foundation) -
 *   이 화면은 그 둘을 데이터(생성자)로 말할 뿐 직접 부르지 않는다.
 */
UCLASS()
class VOWBOUND_API UVBPauseMenuWidget : public UVBMenuScreenWidget
{
	GENERATED_BODY()

public:
	UVBPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

	// 저장 화면으로 전환. 저장은 반드시 슬롯을 고르게 한다 - 자동 선택은 어느 자리를 덮는지
	// 화면에 드러나지 않아 남의 세이브를 조용히 지운다(BUG-017 과 같은 종류의 사고다).
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ShowSavePage();

	// 게임 재개. 이 화면을 끄면 끝이다 - 정지 해제는 UI 관리자의 정지 목록이, 입력 복귀는 아래 HUD 화면의
	//   입력 설정이 맡는다(.sw/ui-foundation 2.2~2.3). 뒤로 가기(Esc·게임패드 B)도 같은 길이다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ResumeGame();

	// 메인 메뉴로 복귀. 소유 PC 로 위임(unpause + OpenLevel).
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void QuitToMenu();

protected:
	// 명성 바에 ASC 를 물려준다. 열 때마다 하는 이유: 화면 위젯은 재사용되고, 이 메뉴는 Esc 를 눌러야 열리므로
	// 그때는 ASC 초기화가 이미 끝나 있다.
	virtual void NativeOnActivated() override;

	// 소유 PlayerController 를 VB 타입으로 캐스트.
	AVBPlayerController* GetVBOwningController() const;

	// 명성 바(선택). BindWidget 이 아니라 Optional 인 이유는 이 클래스의 규약이다 -
	//   클래스 주석대로 이 위젯은 BindWidget 강제 대신 BP 디자인 자유를 택했고,
	//   강제로 바꾸면 바를 아직 안 넣은 기존 WBP 가 컴파일 단계에서 깨진다.
	//   미배치면 표시만 없고 메뉴 기능은 그대로 동작한다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UVBReputationBarWidget> ReputationBar;
};
