// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBListRowWidget.generated.h"

class UButton;
class UImage;
class UTextBlock;
class UTexture2D;

DECLARE_DELEGATE_OneParam(FVBOnListRowClicked, int32);

/**
 * 목록 화면의 한 줄: 글 + 옆 글(수·값) + 켜짐. 누르면 자기 번호를 알린다 - 번호가 무엇을 가리키는지와 누른 뒤의 일은 화면이 안다.
 * 쓰는 곳 = 대화 선택지·퀘스트 일지·인벤토리·상점. 설계 .sw/narrative 13.1절 3번.
 *
 * 왜 하나인가: 같은 모양의 줄이 대화 선택지 줄·인벤토리 줄로 둘이었고, 일지는 «대화 선택지» 줄을 빌려 쓰고 있었다. 상점이 넷째라
 *   여기서 뽑았다(규칙 12). 옛 두 클래스는 ClassRedirects(DefaultEngine.ini)로 이리 이어진다.
 * C++ 가 만들고 채우고, WBP 는 생김새만 든다. 버튼과 글은 필수 바인딩이다 - 선택 바인딩은 WBP 의 위젯 이름이 틀려도
 *   조용히 빈 줄이 되므로, 줄의 본체는 이름이 틀리면 WBP 컴파일에서 멈추게 한다. 옆 글은 대화 선택지처럼 없는 줄이 있어 선택이다.
 * 저장 슬롯 줄(UVBSaveSlotEntryWidget)은 칸이 여럿이라 이 모양이 아니다.
 */
UCLASS(Abstract)
class VOWBOUND_API UVBListRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 옆 글이 비면 그 칸을 접는다. 꺼진 줄은 버튼을 끈 채 보인다(잠긴 모습은 WBP 의 버튼 비활성 스타일이 정한다).
	void Setup(int32 InRowIndex, const FText& InLabel, const FText& InDetail, bool bEnabled);

	// 줄 앞 그림(선택). 그림이 비거나 WBP 에 ImgIcon 이 없으면 접는다 - 그림을 넘기지 않는 화면(대화·일지)은 부를 필요가 없다.
	//   소프트 참조를 그대로 넘겨 비동기로 싣는다(UImage::SetBrushFromSoftTexture). (.sw/remaining-features/a1-inventory.md)
	void SetIcon(const TSoftObjectPtr<UTexture2D>& Icon);

	UButton* GetButton() const { return BtnRow; }

	FVBOnListRowClicked OnRowClicked;

protected:
	virtual void NativeConstruct() override;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnRow;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtLabel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtDetail;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgIcon;

private:
	UFUNCTION()
	void HandleClicked();

	int32 RowIndex = INDEX_NONE;
};
