// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBButtonBase.h"

#include "CommonTextBlock.h"

void UVBButtonBase::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyText();
	NativeOnCurrentTextStyleChanged();
}

void UVBButtonBase::SetButtonText(const FText& InText)
{
	ButtonText = InText;
	ApplyText();
}

void UVBButtonBase::ApplyText()
{
	if (ButtonLabel)
	{
		ButtonLabel->SetText(ButtonText);
	}
}

void UVBButtonBase::NativeOnCurrentTextStyleChanged()
{
	Super::NativeOnCurrentTextStyleChanged();
	if (ButtonLabel)
	{
		// 스타일 자산에 글자 스타일이 없으면(null) 글자 블록 자신의 스타일을 그대로 둔다.
		if (const TSubclassOf<UCommonTextStyle> TextStyle = GetCurrentTextStyleClass())
		{
			ButtonLabel->SetStyle(TextStyle);
		}
	}
}
