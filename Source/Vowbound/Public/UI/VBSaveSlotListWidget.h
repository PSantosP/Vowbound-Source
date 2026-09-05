// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSaveSlotListWidget.generated.h"

class UVBSaveGameSubsystem;

/**
 * 슬롯 목록의 용도.
 * NewGame: 슬롯 선택 시 그 슬롯에서 새 게임 시작(기존 데이터는 BP 가 확인 다이얼로그로 보호).
 * Load   : 슬롯 선택 시 그 슬롯을 로드.
 * Save   : 슬롯 선택 시 현재 진행을 그 슬롯에 저장. 플레이 중이므로 레벨을 열지 않는다.
 */
UENUM(BlueprintType)
enum class EVBSlotListMode : uint8
{
	NewGame,
	Load,
	Save
};

/**
 * 멀티 슬롯 선택 화면.
 * 이유: New Game / Load 가 동일한 슬롯 목록 UI 를 공유하고 동작만 모드로 분기한다.
 *       목록 데이터는 Subsystem 의 GetAllSlotMetadata 로 받아 BP 가 엔트리 위젯으로 그린다.
 */
UCLASS()
class VOWBOUND_API UVBSaveSlotListWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UVBSaveSlotListWidget(const FObjectInitializer& ObjectInitializer);

	// 화면을 열 때 BP 가 용도를 지정.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void SetListMode(EVBSlotListMode InMode);

	UFUNCTION(BlueprintPure, Category="Vowbound|UI")
	EVBSlotListMode GetListMode() const { return ListMode; }

	// 전체 슬롯 메타 조회. BP 가 이 배열로 엔트리 위젯들을 생성한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	TArray<FVBSaveSlotMeta> RefreshSlots() const;

	// 슬롯 선택 시 BP 가 호출. 모드에 따라 새 게임 시작 / 로드로 분기.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void SelectSlot(int32 SlotIndex);

	// 슬롯 삭제 후 OnSlotsChanged 로 BP UI 갱신을 알린다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void DeleteSlot(int32 SlotIndex);

	// 목록을 비우고 현재 슬롯 메타로 다시 채운다. 화면 진입/삭제 후에 부른다.
	// 왜 C++ 인가: 항목 생성은 배열 순회 + 위젯 생성 + 델리게이트 구독인데, 이걸 BP 배선으로 두면
	//   "몇 개를 어떤 순서로 만들고 누가 구독하는가" 라는 규칙이 그래프에만 존재하게 된다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void RebuildEntries();

protected:
	virtual void NativeConstruct() override;

	// 항목 위젯 클래스. WBP 에서 지정한다(기본 미지정이면 목록이 비어 보이고 경고를 남긴다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TSubclassOf<class UVBSaveSlotEntryWidget> EntryWidgetClass;

	// UMG 이름 바인딩. 노출 키워드가 없으므로 Category 를 붙이지 않는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UTextBlock> TxtTitle;

	// 모드별 화면 제목. 문구는 콘텐츠라 바꾸는 데 빌드가 필요하면 안 된다 - 홈은 여기(WBP 에서 수정).
	// 생성자가 안전 기본값을 넣으므로 WBP 를 손대지 않아도 빈 제목이 뜨지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	TMap<EVBSlotListMode, FText> ModeTitles;

	// 항목 사이 간격. 자식을 어떻게 늘어놓을지는 컨테이너의 일이므로 홈이 여기다 -
	// 항목(UVBSaveSlotEntryWidget)은 자기 안쪽 생김새만 알아야 재사용된다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|UI")
	FMargin EntrySpacing = FMargin(0.f, 3.f);

private:
	UFUNCTION()
	void HandleEntrySelect(int32 SlotIndex);

	UFUNCTION()
	void HandleEntryDelete(int32 SlotIndex);

	// 저장 완료(비동기) 후 목록 갱신. 방금 쓴 타임스탬프를 화면에 반영한다.
	UFUNCTION()
	void HandleSaveCompleted(int32 SlotIndex, bool bSuccess);

	// 모드에 따라 제목 문구를 맞춘다.
	void RefreshTitle();

protected:
	// 로드 완료 콜백 — 성공 시 게임 레벨 오픈.
	UFUNCTION()
	void HandleLoadCompleted(int32 SlotIndex, bool bSuccess);

	// 슬롯 목록이 바뀌었을 때(삭제 등) BP UI 재구성 훅.
	UFUNCTION(BlueprintImplementableEvent, Category="Vowbound|UI")
	void OnSlotsChanged();

	EVBSlotListMode ListMode = EVBSlotListMode::Load;

	// 간격 경고는 목록당 한 번만. RebuildEntries 는 슬롯 수만큼 도는 데다 재진입마다 불린다.
	bool bEntrySpacingWarned = false;
};
