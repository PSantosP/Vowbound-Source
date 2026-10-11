// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSaveGame.generated.h"

/**
 * Vowbound 세이브 데이터 컨테이너.
 * 이유: 모든 영속 데이터를 한 직렬화 단위로 모은다. Settings 는 글로벌 슬롯,
 *       Progress/PlayerBuild 는 게임 슬롯에서 사용 — 같은 클래스를 재사용하되
 *       용도별 슬롯으로 분리한다(VBSaveGameSubsystem 참조).
 *
 * 직렬화 규칙: 저장 대상 멤버에는 반드시 UPROPERTY(SaveGame) 를 붙인다.
 *             SaveGame proxy archive(ArIsSaveGame)는 CPF_SaveGame 플래그가 없는
 *             프로퍼티를 건너뛸 수 있으므로, 명시해야 "조용히 빈 저장"을 막는다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// 세이브 포맷 버전. 필드 의미가 바뀌면 증가시키고 마이그레이션 분기를 추가한다.
	static constexpr int32 CurrentSaveVersion = 1;

	// 이 세이브가 기록된 시점의 포맷 버전. 로드 시 마이그레이션 판단 기준.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Meta")
	int32 SaveVersion = CurrentSaveVersion;

	// 저장 시각 (UTC). 슬롯 목록 정렬/표시용.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="Vowbound|Save|Meta")
	FDateTime SaveTimestamp;

	// 슬롯에 표시할 사람이 읽는 이름 (예: "챕터 1 - 서막").
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save|Meta")
	FString SlotDisplayName;

	// --- 도메인 데이터 ---
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBSettingsSaveData Settings;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBProgressSaveData Progress;

	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBPlayerBuildSaveData PlayerBuild;

	// 가진 아이템(2026-09-30, .sw/narrative 10.2). append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBInventorySaveData Inventory;

	// 퀘스트 일지(2026-10-01, .sw/narrative 12.2). append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBQuestSaveData Quests;

	// 상점에서 판 수(2026-10-01, .sw/narrative 13.4). append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBShopSaveData Shops;

	// 서랍장에 남은 몫(2026-10-03, .sw/narrative 14.5 W1). append-only - 옛 슬롯은 비어 있다.
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBLootContainerSaveData LootContainers;

	// 입은 장비(2026-10-06, .sw/remaining-features/b1-equipment.md). append-only - 옛 슬롯은 비어 있다(= 아무것도 안 입음).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBEquipmentSaveData Equipment;

	// 성장 - 누적 경험치·연 스킬 칸(2026-10-07, .sw/remaining-features/b2-growth.md). append-only - 옛 슬롯은 0 / 빈 목록(= 처음 상태).
	UPROPERTY(SaveGame, BlueprintReadWrite, Category="Vowbound|Save")
	FVBGrowthSaveData Growth;
};
