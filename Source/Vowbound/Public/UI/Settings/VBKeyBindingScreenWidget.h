// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "Input/VBKeyBindingRules.h"
#include "VBKeyBindingScreenWidget.generated.h"

class IInputProcessor;
class UButton;
class UPanelWidget;
class UTextBlock;
class UVBInputUserSettings;
class UVBListRowWidget;

/**
 * 키 다시 지정 화면(UI.Screen.KeyBindings, Modal 층). 설계 .sw/quickslot-input 10.1절.
 *
 * 키 배치의 주인은 사용자 설정(UVBInputUserSettings)이다. 이 화면은 줄을 읽어 그리고, 바꾸기·기본값을 부르고, 바꾼 뒤 저장을 부른다.
 * 장치는 탭(키보드 / 패드)으로 가른다 - 키를 받을 칸의 장치가 탭으로 정해진다. 처음 탭은 지금 쓰는 장치.
 * 키 받기는 Slate 입력 전처리기가 한다(맨 앞에 끼워 CommonUI 보다 먼저 본다). 누름은 삼키고 뗄 때 정한다 -
 *   줄 버튼은 뗄 때 눌리므로 그 뗌은 받기가 시작되기 전에 지나간다.
 * 상태는 셋 - 고르는 중 / 키를 기다리는 중 / 겹침 확인 중. 뒤로 가기는 앞의 상태를 먼저 거둔다.
 * Modal 층인 이유: Menu 층에 쌓으면 일시정지 메뉴가 꺼졌다 켜지며 첫 페이지로 돌아가 닫은 뒤 설정 페이지를 잃는다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBKeyBindingScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBKeyBindingScreenWidget(const FObjectInitializer& ObjectInitializer);

	// 입력 전처리기가 뗀 키를 넘긴다. 키를 기다리는 중이 아니면 무시한다.
	void HandleCapturedKey(const FKey& CapturedKey);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual bool NativeOnHandleBackAction() override;
	// 닫기 버튼(기반 BtnClose): 뒤로 가기와 뜻이 다르다 - 뒤로 가기는 입력 대기를 먼저 거두지만 닫기는 대기 중이어도 화면을 닫는다.
	virtual void NativeOnCloseClicked() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 목록 한 줄의 WBP(공용 목록 줄 - 글 = 동작 이름, 옆 글 = 지금 키). 비면 목록이 비어 보이고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> EntryWidgetClass;

	// 키를 기다릴 때 - 키보드 탭. 키보드에는 Esc 가 취소다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText WaitingKeyboardText;

	// 키를 기다릴 때 - 패드 탭. 패드는 버튼을 전부 쓰는 중이라 취소용으로 뺄 버튼이 없다 - 지금 버튼을 다시 누르면 바뀌는 것이 없다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText WaitingGamepadText;

	// 받을 수 없는 키(다른 장치의 키·Esc·시점 축 등, VBKeyBinding::CanBind). {Key} = 누른 키.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText CannotBindFormat;

	// 겹침 확인. {Key} = 새 키, {Actions} = 그 키를 쓰는 줄들의 이름.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText ConflictFormat;

	// 바꾼 뒤. {Action} = 줄 이름, {Key} = 새 키.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText ReboundFormat;

	// 바꾸기가 실패했을 때(까닭은 로그 - UVBInputUserSettings::RebindKey). 조용히 그대로면 눌렀는데 아무 일도 없는 화면이 된다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText RebindFailedText;

	// 기본값으로 되돌린 뒤.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText ResetDoneText;

	// 키가 없는 줄의 옆 글.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText UnboundKeyText;

	// 사용자 설정을 못 찾았을 때 - 프로젝트 설정의 사용자 설정 클래스가 UVBInputUserSettings 가 아니거나 로컬 플레이어가 없다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText NoSettingsText;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	// 목록·상태 글·탭·확인 칸·닫기는 필수다 - 이름이 틀리면 «키를 기다리는데 아무 말이 없는 화면» 이나 «겹치면 영영 못 바꾸는 화면» 이
	//   조용히 나가므로 WBP 컴파일에서 멈추게 한다. 기본값 버튼만 선택이다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtStatus;

	// 지금 탭의 버튼은 꺼 둔다 - 꺼진 모습이 «여기가 지금 탭» 의 표시다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnKeyboardTab;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnGamepadTab;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnReset;

	// 닫기 버튼(BtnClose)은 기반 UVBScreenWidget 이 든다(2026-10-07). 필수(생성자의 bRequireCloseButton) - PIE 에서는 Esc 가
	//   PIE 종료라 이 버튼이 화면을 벗어나는 길이다(상점 닫기와 같은 까닭).

	// 겹침 확인 칸. 확인 중에만 보인다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UWidget> ConfirmBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtConfirm;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnConfirmYes;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnConfirmNo;

private:
	enum class EState : uint8
	{
		Browsing,
		Waiting,
		Confirming,
	};

	UVBInputUserSettings* GetSettings() const;

	void ShowDevice(EVBKeyDevice Device);
	void Rebuild();
	void SelectRow(int32 RowIndex);
	void StartCapture();
	void StopCapture();
	void ApplyRebind(FName MappingName, const FKey& NewKey);
	void EnterBrowsing(const FText& Status);
	void SaveSettings();
	FText KeyText(const FKey& Key) const;
	FText RowName(FName MappingName) const;

	UFUNCTION()
	void HandleKeyboardTabClicked();

	UFUNCTION()
	void HandleGamepadTabClicked();

	UFUNCTION()
	void HandleResetClicked();

	UFUNCTION()
	void HandleConfirmYesClicked();

	UFUNCTION()
	void HandleConfirmNoClicked();

	EVBKeyDevice ShownDevice = EVBKeyDevice::KeyboardMouse;
	EState State = EState::Browsing;

	// 지금 그린 줄. 목록 줄의 번호 = 이 배열의 번호.
	TArray<FVBKeyBindingRow> Rows;

	// 키를 기다리거나 확인하는 줄의 번호와, 확인 중인 새 키.
	int32 EditingRow = INDEX_NONE;
	FKey PendingKey;

	// 키를 기다리는 동안만 Slate 에 끼워 둔다.
	TSharedPtr<IInputProcessor> CaptureProcessor;
};
