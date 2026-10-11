// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBGrowthRules.generated.h"

class UVBSkillTreeDefinition;

/** 칸을 열 수 있나의 답. 설계 .sw/remaining-features/b2-growth.md 1절 5번. */
UENUM(BlueprintType)
enum class EVBSkillUnlockResult : uint8
{
	Unlocked,
	UnknownNode,
	AlreadyUnlocked,
	LevelTooLow,
	MissingPrerequisite,
	NotEnoughPoints,
	// 판정 밖의 거절 - 서버가 아니거나 성장 설정(트리)이 없다. 순수 판정(CanUnlock)은 이 값을 내지 않는다.
	Unavailable,
};

/**
 * 성장의 순수 판정. 레벨·포인트는 저장하지 않고 여기서 센다(누적 경험치 + 연 칸 목록이 유일한 진실).
 * 요청(서버 RPC)과 불러오기가 같은 CanUnlock 을 탄다 - 장비의 VBEquipment::Equip 과 같은 모양.
 */
namespace VBGrowth
{
	// 누적 경험치의 레벨. XPToNextLevel[i] = 레벨 i+1 -> i+2 에 필요한 양. 음수 경험치는 0 으로 본다.
	VOWBOUND_API int32 LevelForXP(TConstArrayView<int32> XPToNextLevel, int32 TotalXP);

	// 그 레벨이 시작되는 누적 경험치(레벨 1 = 0). 최대 레벨 너머는 최대 레벨의 값.
	VOWBOUND_API int32 XPAtLevelStart(TConstArrayView<int32> XPToNextLevel, int32 Level);

	VOWBOUND_API int32 PointsEarned(int32 Level, int32 PointsPerLevel);

	// 연 칸 비용의 합. 트리에 없는 칸은 0 으로 센다(지운 칸이 남은 세이브 - 불러오기가 따로 버린다).
	VOWBOUND_API int32 PointsSpent(const UVBSkillTreeDefinition& Tree, TConstArrayView<FGameplayTag> Unlocked);

	VOWBOUND_API EVBSkillUnlockResult CanUnlock(const UVBSkillTreeDefinition& Tree, TConstArrayView<FGameplayTag> Unlocked,
		int32 Level, int32 PointsAvailable, const FGameplayTag& NodeId);

	VOWBOUND_API const TCHAR* LexToString(EVBSkillUnlockResult Result);

	// HUD 레벨업·해금 알림의 기준. 레벨·연 칸은 복제·불러오기로 «한꺼번에» 도 바뀌므로 «직전에 본 것» 과 비교해 새로 생긴 것만 알린다.
	struct FNoticeBaseline
	{
		int32 LastLevel = 0;
		TArray<FGameplayTag> Unlocked;
		int32 SaveReadSerial = 0;
		bool bSeen = false;
	};
	struct FNotices
	{
		int32 LevelUpTo = 0;                 // 0 = 레벨업 알림 없음. 여러 레벨이 한 번에 오르면 최신 레벨 하나
		TArray<FGameplayTag> NewlyUnlocked;  // 연 순서대로
	};
	// 처음 본 갱신과 세이브를 읽어 바뀐 갱신(SaveReadSerial 이 바뀜)은 기준만 잡고 아무것도 알리지 않는다 - 레벨 1 을 먼저 본 HUD 가
	//   이어하기의 레벨 5 를 «네 번 오름» 으로 읽어 알림이 몰려 떴다(f3 verify 10-08 Q3). 그 뒤의 진짜 오름·해금만 알린다.
	VOWBOUND_API FNotices StepNoticeBaseline(FNoticeBaseline& Baseline, int32 Level, TConstArrayView<FGameplayTag> Unlocked, int32 SaveReadSerial);
}

/**
 * 부른 플레이어에게 경험치를 준다. 적의 처치 효과(UVBEnemyConfig::OnKilledEffects)와 퀘스트 보상이 쓴다 - 주는 양은 주는 쪽 데이터 한 줄.
 * 처치 사건(Event.Enemy.Killed)을 따로 듣지 않는 이유: 들으면 «얼마를 주나» 가 적 데이터 밖에 또 생긴다(설계 1절 1번).
 */
USTRUCT(BlueprintType, meta=(DisplayName="경험치 주기"))
struct VOWBOUND_API FVBEffect_GiveXP : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Growth", meta=(ClampMin="1"))
	int32 Amount = 0;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
