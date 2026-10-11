// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonButtonBase.h"
#include "VBButtonBase.generated.h"

class UCommonTextBlock;

/**
 * 모든 UI 버튼의 부모. 설계 .sw/ui-foundation/design.md 2.4.
 * 버튼 모양(브러시·글자 스타일)은 UCommonButtonStyle 자산 하나가 정하고, 이 클래스는 글자를 붙이는 일만 한다.
 *
 * 왜 필요한가: UCommonButtonBase 는 상태(보통·호버·눌림·선택·비활성)가 바뀔 때 브러시만 바꾸고
 *   버튼 안의 글자에는 스타일의 글자 스타일을 입히지 않는다. NativeOnCurrentTextStyleChanged 를 받아 직접 입힌다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBButtonBase : public UCommonButtonBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI|Button")
	void SetButtonText(const FText& InText);

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeOnCurrentTextStyleChanged() override;

	// 버튼에 적힐 글. 디자이너에서 바로 보이도록 PreConstruct 에서도 적용한다.
	UPROPERTY(EditAnywhere, Category="Vowbound|UI|Button")
	FText ButtonText;

private:
	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 글자 없는 아이콘 버튼도 있으니 선택이다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UCommonTextBlock> ButtonLabel;

	void ApplyText();
};
