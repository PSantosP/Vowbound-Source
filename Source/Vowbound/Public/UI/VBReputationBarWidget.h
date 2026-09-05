// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBReputationBarWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UAbilitySystemComponent;
struct FOnAttributeChangeData;

/**
 * 명성(Reputation)을 프로그레스바로 보여주는 위젯.
 *
 * 무엇: UVBReputationAttributeSet::Reputation 하나만 구독해 바와 숫자를 갱신한다.
 * 왜 별도 클래스인가: 일시정지 메뉴에 직접 얹으면 그 위젯이 "메뉴 이동"과 "어트리뷰트 표시"
 *       두 관심사를 갖게 되고, 나중에 HUD 에도 띄우려면 로직이 메뉴에 갇힌다.
 *       UVBHealthBarWidget 과 같은 자리·같은 모양이다(그쪽이 이 패턴의 원본이다).
 * 배선: 소유 위젯이 ASC 준비 이후 InitializeWithASC 를 부른다 - 생성 시점이 아니라
 *       호출 시점을 소유자가 정하는 것이 UVBHUDWidget::InitializeHUD 가 세운 규약이다.
 * 부작용: 없다. 읽기 전용 구독이며 어트리뷰트를 쓰지 않는다.
 */
UCLASS()
class VOWBOUND_API UVBReputationBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ASC 에서 명성 변화를 받아 바를 갱신한다. null 이면 아무 일도 하지 않는다.
	void InitializeWithASC(UAbilitySystemComponent* ASC);

protected:
	// BindWidget: BP 디자이너에서 같은 이름의 위젯과 자동 연결. 없으면 컴파일이 실패한다 -
	//   이 위젯의 존재 이유가 바 하나이므로 누락은 조용히 넘기지 않고 시끄럽게 실패시킨다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UProgressBar> ReputationBar;

	// 숫자 표시(선택). 바만 있어도 동작한다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> ReputationText;

private:
	void OnReputationChanged(const FOnAttributeChangeData& Data);
	void UpdateBar();

	// 비율 계산용 캐시. 어트리뷰트를 매번 되묻지 않는다.
	float CurrentReputation = 0.0f;
};
