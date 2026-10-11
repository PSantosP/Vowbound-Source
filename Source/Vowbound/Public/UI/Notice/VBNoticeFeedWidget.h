// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Text/TextLayout.h" // ETextJustify - 줄 정렬 칸
#include "Styling/SlateColor.h"
#include "UI/Notice/VBNoticeFeed.h"
#include "VBNoticeFeedWidget.generated.h"

class UPanelWidget;
class UTextBlock;
class UVBNoticeSubsystem;

/**
 * 알림 줄을 그린다(WBP_NoticeFeed). 목록의 주인은 UVBNoticeSubsystem 이고 이 위젯은 읽기만 한다.
 * 뿌리 위젯(UVBPrimaryLayout)이 층 넷 위에 붙인다 - 메뉴 위에도 보인다. 줄은 EntryBox 안에 런타임으로 만들고 다시 쓴다.
 * 끝나 가는 줄은 FadeSeconds 동안 흐려진다.
 */
UCLASS()
class VOWBOUND_API UVBNoticeFeedWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	// 구독은 NativeConstruct·NativeDestruct 짝으로 한다(f3 verify 10-08 Q1) - 화면에서 내렸다 다시 붙이면 Construct 는 다시 불리고 OnInitialized 는 안 불린다.
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 줄이 쌓이는 상자(세로). 오래된 줄이 위다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> EntryBox;

	// 끝나기 전 흐려지는 시간(실시간 초).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI", meta=(ClampMin="0", Units="s"))
	float FadeSeconds = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI", meta=(ClampMin="6"))
	int32 FontSize = 20;

	// 종류별 글 색. 없는 종류는 DefaultColor.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TMap<EVBNoticeCategory, FSlateColor> CategoryColors;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FSlateColor DefaultColor = FSlateColor(FLinearColor::White);

	// 줄 정렬·그림자 - 글 크기·색과 같은 자리에 둔다(f3 verify 10-08 Q6: 종전엔 코드에 박혀 있었다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TEnumAsByte<ETextJustify::Type> LineJustification = ETextJustify::Center;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FVector2D ShadowOffset = FVector2D(1.0f, 1.0f);

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FLinearColor ShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.8f);

private:
	void Rebuild(const TArray<FVBNoticeEntry>& Entries);

	TWeakObjectPtr<UVBNoticeSubsystem> Notices;
	FDelegateHandle ChangedHandle;
	bool bDirty = true;

	// 만든 줄. 줄 수가 줄면 감추고 다시 쓴다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> Lines;
};
