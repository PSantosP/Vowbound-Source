// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "VBQuestJournalScreenWidget.generated.h"

class UInputAction;
class UPanelWidget;
class UTextBlock;
class UVBListRowWidget;
class UVBQuestDefinition;
class UVBQuestLogComponent;

/**
 * 퀘스트 일지 화면(UI.Screen.Journal). 설계 .sw/narrative 12.5절. 인벤토리 화면과 같은 모양이다.
 *
 * 표시는 아무것도 결정하지 않는다: 목록은 UVBQuestLogComponent::OnQuestLogChanged 를 듣고 다시 그리고, 목표 글은 VBQuestText 가 만든다(HUD 와 같은 글).
 * 목록 한 줄은 공용 목록 줄(UVBListRowWidget)이다 - 대화·인벤토리·상점과 같은 줄(설계 13.1절 3번).
 * 여는 쪽은 HUD(JournalInputAction). 닫기는 뒤로 가기(bIsBackHandler) 또는 같은 입력을 한 번 더.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBQuestJournalScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 목록 한 줄의 WBP(공용 목록 줄). 비면 목록이 비어 보이고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<UVBListRowWidget> EntryWidgetClass;

	// 화면이 떠 있는 동안 이 입력을 한 번 더 누르면 닫는다(HUD 가 여는 입력과 같은 IA 를 꽂는다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TObjectPtr<UInputAction> CloseInputAction;

	// 끝난 퀘스트 줄의 옆 글.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FText CompletedDetail = NSLOCTEXT("VBQuestJournal", "CompletedDetail", "완료");

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtEmpty;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtQuestTitle;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtQuestDetail;

private:
	void Rebuild();
	void Select(int32 RowIndex);
	void RefreshDetail();
	void HandleCloseInput();

	TWeakObjectPtr<UVBQuestLogComponent> QuestLog;
	TWeakObjectPtr<const UVBQuestDefinition> Selected;
	// 줄 번호 -> 퀘스트(줄 위젯은 번호만 알린다).
	TArray<TWeakObjectPtr<const UVBQuestDefinition>> RowQuests;
	FDelegateHandle QuestLogChangedHandle;
};
