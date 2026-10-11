// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBStoryState.h"
#include "VBSaveGameTypes.generated.h"

/**
 * 게임플레이/오디오 설정.
 * 이유: UE의 UGameUserSettings(해상도/그래픽 품질 등 엔진 레벨)와 분리한다.
 *       여기엔 "게임 디자인이 소유하는" 옵션(볼륨/감도/언어)만 둔다.
 *       글로벌(슬롯 무관)이라 별도 "VBSettings" 슬롯에 저장된다.
 */
USTRUCT(BlueprintType)
struct FVBSettingsSaveData
{
	GENERATED_BODY()

	// 오디오 버스 볼륨 3종 (0.0 ~ 1.0). 저장·복원만 되고 적용되지 않는다.
	// 이유: 이 프로젝트에 아직 오디오 계층 자체가 없다(사운드 자산 0, 사운드 코드 0).
	// 볼륨을 "적용"하는 것은 정리가 아니라 오디오 시스템 신설이라 이 라운드의 범위가 아니다.
	// 따라서 사용자에게 거짓을 보이지 않도록 WBP_Settings 에서 컨트롤을 숨긴다 - 슬라이더가
	// 화면에 있는 한 어떤 경고 로그도 사용자를 속이는 것을 막지 못한다. 오디오 계층이 생기면
	// 여기 값을 서브믹스에 물리고 컨트롤을 되살리면 된다(필드는 그때 그대로 쓴다).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Settings")
	float MasterVolume = 1.0f;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Settings")
	float MusicVolume = 1.0f;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Settings")
	float SfxVolume = 1.0f;

	// 카메라/조준 감도 배율 (1.0 = 기본). AVBCharacter::HandleLook 이 매 입력마다 여기서 직접 읽는다
	// (UVBSaveGameSubsystem::GetSettingsData) - 캐시가 없어 설정 변경이 다음 입력부터 즉시 반영된다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Settings")
	float LookSensitivity = 1.0f;

	// Y축 반전 여부. 감도와 같은 경로로 HandleLook 이 읽는다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Settings")
	bool bInvertYAxis = false;

	// 로케일 코드 (예: "ko", "en"). 저장·복원만 되고 적용되지 않는다.
	// 이유: 번역 대상 텍스트를 아직 로컬라이즈 대시보드에 수집하지 않아 컬처를 바꿔도 바뀔 문자열이 없다.
	// 볼륨과 같은 판단 - 컨트롤을 WBP_Settings 에서 숨긴다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Settings")
	FString Language = TEXT("ko");
};

/**
 * 게임 진행도. 메인 메뉴 "Continue" 의 핵심 데이터.
 * 확장형: 스토리 플래그는 켜진 항목만 보관(TSet)하여 필드 추가 없이 늘려간다.
 */
USTRUCT(BlueprintType)
struct FVBProgressSaveData
{
	GENERATED_BODY()

	// 마지막 저장 시점의 레벨(맵) 패키지 이름. 로드 시 OpenLevel 대상.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	FName LastLevelName;

	// 레벨 내 복원 지점 식별자. 체크포인트 액터가 자기 Id 로 위치를 복원한다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	FName LastCheckpointId;

	// 누적 플레이 시간(초). 슬롯 목록 표시 + 통계용.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	double PlayTimeSeconds = 0.0;

	// 쓰는 곳 없음(2026-09-28 소스 검색 0). 사실은 아래 Story 가 잇는다 - 태그라 이름을 바꿔도 리다이렉트로 따라온다(설계 .sw/narrative 2.1절).
	//   지우지 않는 이유: 저장 형식은 append-only 다. 필드를 빼면 옛 슬롯을 읽을 때 그 자리를 건너뛰는 규칙에 기대게 된다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	TSet<FName> StoryFlags;

	// 저장 시점 플레이어 폰의 월드 Transform. 수동 저장(일시정지)이 채운다.
	// 로드 시 WP 스트리밍 준비 후 이 위치로 복원(UVBPlayerRestoreComponent). 체크포인트(LastCheckpointId)와 별개 축.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	FTransform SavedPlayerTransform;

	// 위 Transform 이 유효한지. false면 위치 복원을 건너뛰고 PlayerStart 기본 스폰을 유지한다
	// (New Game / 위치 미기록 세이브 / 기존 v1 슬롯 — append-only 하위호환의 안전 폴백).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	bool bHasSavedTransform = false;

	// 상호작용 대상의 켜짐 상태(문 열림·스위치 켜짐 등). 키 = UVBInteractableComponent::GetSaveKey.
	// 쓰는 쪽 = UVBSaveGameSubsystem::SaveCurrentGameToSlot(UVBInteractableSubsystem::CollectSavedStates),
	// 읽는 쪽 = AVBGameMode 의 Continue 적용(UVBInteractableSubsystem::ApplySavedStates). append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	TMap<FName, bool> InteractableStates;

	// 세계 사실·카운터. 쓰고 읽는 쪽 = UVBStoryStateComponent(저장 참여자). append-only - 옛 슬롯은 비어 있다(= 아무 사실도 없음).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Progress")
	FVBStoryState Story;
};

/**
 * 플레이어 빌드/스탯 스냅샷.
 * 이유: GAS AttributeSet 의 현재 값과 해금 상태를 직렬화 가능한 단순 타입으로 보관한다.
 *       FGameplayAttribute 직접 저장은 피하고 Attribute 이름(FName)을 키로 쓴다 - 타입을 바꾸면
 *       구 세이브가 조용히 유실된다.
 *       채우고 읽는 쪽은 AVBPlayerState::SnapshotBuild / ApplyBuild 이고, 대상 목록과 순서는
 *       AVBPlayerState::SavedAttributes 하나가 소유한다.
 */
USTRUCT(BlueprintType)
struct FVBPlayerBuildSaveData
{
	GENERATED_BODY()

	// Attribute 이름 → 값 스냅샷 (예: "Health" → 80.0).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Build")
	TMap<FName, float> AttributeValues;

	// 해금/습득한 어빌리티 식별자 (예: "Ability.Combat.HeavyAttack").
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Build")
	TArray<FName> UnlockedAbilities;

	// 현재 장착 무기 식별자.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Build")
	FName EquippedWeaponId;

	// 가진 무기(UVBWeaponStateComponent::OwnedWeapons, 설계 .sw/prologue-opening 3.1). EVBWeaponType 의 이름 문자열 - EquippedWeaponId 와 같은 안정 식별자.
	//   bOwnedWeaponsSaved 가 거짓이면 이 칸이 생기기 전의 옛 세이브다 - 복원은 맵 시작값을 그대로 둔다. «빈 목록 = 맨몸» 과 구별하려고 둘을 둔다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Build")
	TArray<FName> OwnedWeaponIds;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Build")
	bool bOwnedWeaponsSaved = false;
};

/** 가진 아이템 한 줄. 자산 경로가 아니라 ItemId 태그로 적는다(UVBItemDefinition 주석 - 자산을 옮겨도 옛 슬롯이 비지 않는다). */
USTRUCT(BlueprintType)
struct FVBSavedItem
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Inventory")
	FGameplayTag ItemId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Inventory")
	int32 Count = 0;
};

/** 인벤토리 구역. 쓰고 읽는 쪽 = UVBInventoryComponent(저장 참여자). 옛 슬롯은 비어 있다(= 빈손). */
USTRUCT(BlueprintType)
struct FVBInventorySaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Inventory")
	TArray<FVBSavedItem> Items;

	// 퀵슬롯 칸마다 ItemId(칸 순서, 빈 칸 = 빈 태그). 덧붙인 칸이라 옛 슬롯은 빈 배열 = 모든 칸이 빈다(설계 .sw/quickslot-input 4.1절).
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Inventory")
	TArray<FGameplayTag> QuickSlots;
};

/** 장비 구역. 쓰고 읽는 쪽 = UVBEquipmentComponent(저장 참여자, .sw/remaining-features/b1-equipment.md). 옛 슬롯은 빈 배열 = 아무것도 안 입음. */
USTRUCT(BlueprintType)
struct FVBEquipmentSaveData
{
	GENERATED_BODY()

	// 칸마다 입은 아이템의 ItemId(칸 순서 = SlotLayout, 빈 칸 = 빈 태그).
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Equipment")
	TArray<FGameplayTag> Slots;
};

/** 성장 구역. 쓰고 읽는 쪽 = UVBProgressionComponent(.sw/remaining-features/b2-growth.md). 레벨·남은 포인트는 저장하지 않는다 - 이 둘에서 센다. 옛 슬롯 = 0 / 빈 목록. */
USTRUCT(BlueprintType)
struct FVBGrowthSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Growth")
	int32 TotalXP = 0;

	// 연 스킬 칸(NodeId). 연 순서대로.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Growth")
	TArray<FGameplayTag> Unlocked;
};

/** 일지의 한 줄. 정의는 QuestId 태그로 되찾는다(아이템과 같은 판단 - 자산을 옮겨도 슬롯이 안 빈다). */
USTRUCT(BlueprintType)
struct FVBSavedQuest
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Quest")
	FGameplayTag QuestId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Quest")
	bool bCompleted = false;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Quest")
	int32 StageIndex = 0;

	// 지금 단계의 목표별 진행.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Quest")
	TArray<int32> Progress;
};

/** 퀘스트 구역. 쓰고 읽는 쪽 = UVBQuestLogComponent(저장 참여자). 옛 슬롯은 비어 있다(= 받은 퀘스트 없음). */
USTRUCT(BlueprintType)
struct FVBQuestSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Quest")
	TArray<FVBSavedQuest> Quests;
};

/** 상점 한 곳에서 아이템 하나를 판 수. 상점·아이템 모두 태그로 적는다(아이템과 같은 판단 - 자산을 옮겨도 슬롯이 안 빈다). */
USTRUCT(BlueprintType)
struct FVBSavedShopSale
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Shop")
	FGameplayTag ShopId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Shop")
	FGameplayTag ItemId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Shop")
	int32 Sold = 0;
};

/** 상점 재고 구역. 쓰고 읽는 쪽 = UVBShopStateSubsystem(저장 참여자). 옛 슬롯은 비어 있다(= 아무것도 안 팔림). */
USTRUCT(BlueprintType)
struct FVBShopSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Shop")
	TArray<FVBSavedShopSale> Sales;
};

/** 서랍장 하나에 남은 것. 빈 Remaining 도 적는다 - «다 털림» 이고, 기록이 없는 «아직 안 굴림» 과 다르다(.sw/narrative 14.5 W1). */
USTRUCT(BlueprintType)
struct FVBSavedLootContainer
{
	GENERATED_BODY()

	// 서랍장 상호작용 대상의 저장 키(기본 = 액터 이름 - 켜짐 저장과 같은 키).
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Loot")
	FName Key;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Loot")
	TArray<FVBSavedItem> Remaining;
};

/** 서랍장 남은 몫 구역. 쓰고 읽는 쪽 = UVBLootContainerSubsystem(저장 참여자). 옛 슬롯은 비어 있다(= 아무 서랍장도 안 굴림). */
USTRUCT(BlueprintType)
struct FVBLootContainerSaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Loot")
	TArray<FVBSavedLootContainer> Containers;
};

/**
 * 슬롯 목록 표시용 경량 메타데이터 (조회 결과 DTO, 직렬화 대상 아님).
 * 이유: 멀티 슬롯 선택 UI 가 각 슬롯의 전체 데이터를 들고 있을 필요 없이
 *       이름/시각/플레이타임만 받아 목록을 그린다. SaveGame 지정자는 불필요.
 */
USTRUCT(BlueprintType)
struct FVBSaveSlotMeta
{
	GENERATED_BODY()

	// 슬롯 번호 (0 기반). 빈 슬롯도 이 값은 채워진다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Save|Slot")
	int32 SlotIndex = 0;

	// 해당 슬롯에 저장 파일이 있는지. false면 "빈 슬롯"으로 표시.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Save|Slot")
	bool bExists = false;

	// 슬롯에 표시할 이름.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Save|Slot")
	FString DisplayName;

	// 저장 시각 (UTC).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Save|Slot")
	FDateTime Timestamp;

	// 누적 플레이 시간(초).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Save|Slot")
	double PlayTimeSeconds = 0.0;

	// 마지막 레벨 이름 (썸네일/배경 결정 등에 활용).
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Save|Slot")
	FName LastLevelName;
};
