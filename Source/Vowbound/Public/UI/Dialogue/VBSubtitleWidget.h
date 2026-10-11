// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBSubtitleWidget.generated.h"

class UTextBlock;
class UVBSpokenLineComponent;
struct FVBSpokenLine;

/**
 * 화면 아래 자막 한 줄. 설계 .sw/prologue-opening 3.4·3.6.
 *
 * 그리기만 한다 - 무엇을 언제 말할지는 효과(FVBEffect_SpeakLine)와 PC 의 UVBSpokenLineComponent 가 정한다.
 * 새 줄이 오면 지금 줄을 바로 갈아 끼운다(쌓지 않는다). 시간이 지나면 접는다.
 * HUD 가 런타임에 루트 캔버스에 붙이고 BindTo 를 부른다(퀵슬롯 칸 줄과 같은 길 - WBP_HUD 트리는 저작 도구가 통째로만 덮는다).
 * 입력을 받지 않는다(HitTestInvisible) - 자막이 조종을 가리지 않게.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBSubtitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 듣기 시작한다. 다른 컴포넌트에 묶여 있었으면 풀고 새로 묶는다. nullptr 이면 풀기만 한다.
	void BindTo(UVBSpokenLineComponent* InLines);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	// 줄은 본체라 필수, 화자 이름은 혼잣말 자막 변형에서 뺄 수 있어 선택이다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtLine;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSpeaker;

private:
	void HandleLine(const FVBSpokenLine& Line, float Seconds);
	void HideLine();

	TWeakObjectPtr<UVBSpokenLineComponent> BoundLines;
	FDelegateHandle LineHandle;
	FTimerHandle HideTimer;
};
