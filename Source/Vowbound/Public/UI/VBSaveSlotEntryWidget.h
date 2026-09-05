// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSaveSlotEntryWidget.generated.h"

/**
 * 슬롯 목록의 한 항목 위젯.
 * 이유: 슬롯 메타 1개를 받아 표시한다. 표시 갱신은 BP(OnMetaUpdated)가 담당하고,
 *       C++ 는 메타 보관 + 표시 문자열 유틸만 제공(UI 디자인 독립).
 */
// 항목이 자기 슬롯 번호를 실어 보내는 요청. 목록이 구독해 실제 동작을 수행한다.
// 항목이 목록을 직접 알지 않게 하는 이유: 항목은 "보여주고 눌렸다고 알리는" 역할만 가진다.
//   목록 포인터를 들면 같은 항목을 다른 화면에서 재사용할 수 없고, 소유 방향이 뒤집힌다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBSlotEntryRequest, int32, SlotIndex);

UCLASS()
class VOWBOUND_API UVBSaveSlotEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category="Vowbound|UI")
	FVBSlotEntryRequest OnSelectRequested;

	UPROPERTY(BlueprintAssignable, Category="Vowbound|UI")
	FVBSlotEntryRequest OnDeleteRequested;

	// List 위젯이 항목 생성 시 호출. 메타 저장 후 BP 갱신 이벤트 발생.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void SetupFromMeta(const FVBSaveSlotMeta& InMeta);

	UFUNCTION(BlueprintPure, Category="Vowbound|UI")
	const FVBSaveSlotMeta& GetMeta() const { return Meta; }

	// 플레이타임 초 → "HH:MM:SS" 표시 문자열.
	UFUNCTION(BlueprintPure, Category="Vowbound|UI")
	static FString FormatPlayTime(double Seconds);

protected:
	virtual void NativeConstruct() override;

	// 표준 표시(이름/시각/플레이타임/삭제 가능 여부)는 C++ 가 채운다.
	// BP 훅은 남겨 둔다 - 썸네일이나 난이도 뱃지처럼 이 클래스가 모르는 것을 BP 가 덧붙이는 자리다.
	// 왜 C++ 로 옮겼나: 표시 갱신을 BP 그래프로만 두면 배선이 곧 계약이 되는데, 그 배선은
	//   컴파일러도 /verify 도 못 본다. 빈 슬롯 표기 같은 규칙이 조용히 어긋나도 알 방법이 없다.
	UFUNCTION(BlueprintImplementableEvent, Category="Vowbound|UI")
	void OnMetaUpdated(const FVBSaveSlotMeta& InMeta);

	// 현재 항목이 표현하는 슬롯 메타.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|UI")
	FVBSaveSlotMeta Meta;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtSlotName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTimestamp;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtPlayTime;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UButton> BtnSelect;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UButton> BtnDelete;

private:
	UFUNCTION()
	void HandleSelectClicked();

	UFUNCTION()
	void HandleDeleteClicked();

	// Meta 를 위젯 텍스트에 반영. 빈 슬롯이면 삭제 버튼을 숨긴다(누를 대상이 없다).
	void RefreshDisplay();
};
