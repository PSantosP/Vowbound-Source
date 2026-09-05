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
};
