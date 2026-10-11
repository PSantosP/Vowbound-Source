// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "UI/VBListRowWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

void UVBListRowWidget::Setup(int32 InRowIndex, const FText& InLabel, const FText& InDetail, bool bEnabled)
{
	RowIndex = InRowIndex;
	if (TxtLabel)
	{
		TxtLabel->SetText(InLabel);
	}
	if (TxtDetail)
	{
		TxtDetail->SetText(InDetail);
		TxtDetail->SetVisibility(InDetail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (BtnRow)
	{
		BtnRow->SetIsEnabled(bEnabled);
	}
	// 그림은 SetIcon 을 부른 화면에서만 보인다 - 같은 줄 WBP 를 쓰는 대화·일지·칸 버튼에 빈 그림 칸이 남지 않게 여기서 접어 둔다.
	if (ImgIcon)
	{
		ImgIcon->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UVBListRowWidget::SetIcon(const TSoftObjectPtr<UTexture2D>& Icon)
{
	if (!ImgIcon)
	{
		return;
	}
	ImgIcon->SetVisibility(Icon.IsNull() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (!Icon.IsNull())
	{
		ImgIcon->SetBrushFromSoftTexture(Icon);
	}
}

void UVBListRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (BtnRow)
	{
		BtnRow->OnClicked.AddUniqueDynamic(this, &UVBListRowWidget::HandleClicked);
	}
}

void UVBListRowWidget::HandleClicked()
{
	OnRowClicked.ExecuteIfBound(RowIndex);
}
