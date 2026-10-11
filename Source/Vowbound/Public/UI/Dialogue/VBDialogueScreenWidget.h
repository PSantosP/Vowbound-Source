// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "VBDialogueScreenWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UVBListRowWidget;
class UVBDialogueComponent;

/**
 * 대화 화면(UI.Screen.Dialogue). 설계 .sw/narrative 11.4절.
 *
 * 여닫는 주인은 대화 컴포넌트다(주인 클라에서 노드가 오면 열고, 끝나면 내린다). 이 화면은 그 컴포넌트의 OnViewChanged 만 듣고 그린다.
 * 표시는 아무것도 결정하지 않는다: 선택지를 고를 수 있나는 서버가 보낸 값이고, 버튼은 요청만 보낸다.
 *   숨길지 잠긴 채 보일지(bShowWhenUnavailable)와 «계속» 인지 «끝» 인지는 대화 자산에 적힌 데이터를 읽을 뿐이다.
 * 뒤로 가기 = 대화를 떠나 달라는 요청. 바로 내리지 않는다 - 서버가 끝을 보내면 컴포넌트가 내린다(진행의 주인이 하나).
 * 게임을 멈추지 않는다(user 결정). 입력 모드와 정지 여부는 WBP 의 화면 설정(InputMode=Menu, bPausesGame=false)이 말한다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBDialogueScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBDialogueScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 선택지 한 줄의 WBP(공용 목록 줄). 비면 선택지가 안 보이고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> ChoiceWidgetClass;

	// 선택지 없이 다음 대사가 있을 때 버튼 글.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText ContinueText;

	// 마지막 대사이거나 고를 수 있는 선택지가 없을 때 버튼 글.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText EndText;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSpeaker;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtLine;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> ChoiceBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnContinue;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtContinue;

private:
	void Refresh();
	void HandleChoiceClicked(int32 ChoiceIndex);

	UFUNCTION()
	void HandleContinueClicked();

	TWeakObjectPtr<UVBDialogueComponent> DialogueComponent;
	FDelegateHandle ViewChangedHandle;
};
