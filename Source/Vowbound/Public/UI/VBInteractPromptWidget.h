// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBInteractPromptWidget.generated.h"

class UTextBlock;
class UProgressBar;

/**
 * 상호작용 프롬프트 한 개. 대상 위치에 뜨고 카메라가 돌아도 따라간다(user 결정 2026-09-23).
 * 위치는 UVBHUDWidget 이 매 틱 투영해 정한다 - 이 위젯은 내용만 든다.
 * 그림(배경·글꼴·진행 링 모양)은 WBP 자산이 든다.
 */
UCLASS()
class VOWBOUND_API UVBInteractPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 행동 이름과 키 이름. 키가 비었으면 키 칸을 숨긴다.
	void SetPrompt(const FText& ActionText, const FText& KeyText);

	// 0~1. 음수면 진행 칸을 숨긴다(탭 대상이거나 아직 안 누름).
	void SetHoldProgress(float Progress);

protected:
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> ActionLabel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyLabel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UProgressBar> HoldProgress;
};
