// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBStartingLoadout.generated.h"

/**
 * 이 맵에서 새로 시작한 플레이어가 처음 가진 무기. 설계 .sw/prologue-opening 8.7.
 *
 * 왜 맵에 놓는 액터인가: VowboundMap 과 L_Prologue_Hospital 이 같은 BP_VBGameMode 를 쓴다(2026-10-03 확인).
 *   GameMode 기본값으로는 맵마다 다르게 줄 수 없다.
 * 놓지 않은 맵은 무기 전부를 가진 채 시작한다(UVBWeaponStateComponent::InitializeComponent) - 기존 시험 맵의 동작 그대로.
 * 이어하기는 저장된 목록을 쓰고 이 값을 무시한다(복원이 시작값을 덮는다).
 * 맵에 둘 이상이면 첫째를 쓰고 경고한다.
 */
UCLASS(HideCategories=(Input, Replication, Rendering, Collision, HLOD, Physics, Networking, LevelInstance, Cooking, DataLayers, WorldPartition))
class VOWBOUND_API AVBStartingLoadout : public AInfo
{
	GENERATED_BODY()

public:
	// 빈 목록 = 맨몸으로 시작한다(프롤로그 - 무기는 주워야 생긴다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Weapon")
	TArray<EVBWeaponType> StartingWeapons;
};
