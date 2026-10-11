// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBOffscreenThreatWidget.generated.h"

class UImage;

/**
 * 화면 밖 위협 화살표 (design §11)
 * 예고 중인 적이 화면 밖일 때 HUD 가장자리에 뜨는 화살표 하나. 위치·회전은 UVBHUDWidget 이 정하고, 이 위젯은 색만 받는다.
 * 텍스처는 위를 가리키는 그림으로 저작한다 - HUD 가 각도 0 = 위 로 계산한다.
 * WBP_OffscreenThreat 가 부모로 쓴다. 이미지 이름은 Arrow 여야 한다(BindWidget).
 */
UCLASS()
class VOWBOUND_API UVBOffscreenThreatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 답의 색. 큐가 추적기에 실어 보낸 값을 HUD 가 그대로 넘긴다.
	void SetThreatColor(const FLinearColor& Color);

protected:
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UImage> Arrow;
};
