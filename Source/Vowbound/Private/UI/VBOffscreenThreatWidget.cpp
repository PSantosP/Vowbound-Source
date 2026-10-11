// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBOffscreenThreatWidget.h"

#include "Components/Image.h"

void UVBOffscreenThreatWidget::SetThreatColor(const FLinearColor& Color)
{
	if (Arrow)
	{
		Arrow->SetColorAndOpacity(Color);
	}
}
