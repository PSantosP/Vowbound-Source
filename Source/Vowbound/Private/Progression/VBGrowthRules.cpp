// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Progression/VBGrowthRules.h"

#include "GameFramework/PlayerState.h"
#include "Progression/VBProgressionComponent.h"
#include "Progression/VBSkillTreeDefinition.h"
#include "Vowbound/Vowbound.h"

namespace VBGrowth
{
	int32 LevelForXP(TConstArrayView<int32> XPToNextLevel, int32 TotalXP)
	{
		int32 Level = 1;
		int32 Needed = 0;
		for (const int32 Step : XPToNextLevel)
		{
			// 1 미만 칸은 데이터 오류(IsDataValid 가 막는다) - 여기서는 1 로 본다. 0 을 그대로 쓰면 경험치 없이 레벨이 오른다.
			Needed += FMath::Max(1, Step);
			if (TotalXP < Needed)
			{
				break;
			}
			++Level;
		}
		return Level;
	}

	int32 XPAtLevelStart(TConstArrayView<int32> XPToNextLevel, int32 Level)
	{
		int32 Total = 0;
		for (int32 Index = 0; Index < FMath::Min(Level - 1, XPToNextLevel.Num()); ++Index)
		{
			Total += FMath::Max(1, XPToNextLevel[Index]);
		}
		return Total;
	}

	int32 PointsEarned(int32 Level, int32 PointsPerLevel)
	{
		return FMath::Max(0, Level - 1) * FMath::Max(0, PointsPerLevel);
	}

	int32 PointsSpent(const UVBSkillTreeDefinition& Tree, TConstArrayView<FGameplayTag> Unlocked)
	{
		int32 Spent = 0;
		for (const FGameplayTag& NodeId : Unlocked)
		{
			if (const FVBSkillNode* Node = Tree.FindNode(NodeId))
			{
				Spent += FMath::Max(1, Node->Cost);
			}
		}
		return Spent;
	}

	EVBSkillUnlockResult CanUnlock(const UVBSkillTreeDefinition& Tree, TConstArrayView<FGameplayTag> Unlocked,
		int32 Level, int32 PointsAvailable, const FGameplayTag& NodeId)
	{
		const FVBSkillNode* Node = Tree.FindNode(NodeId);
		if (!Node)
		{
			return EVBSkillUnlockResult::UnknownNode;
		}
		if (Unlocked.Contains(NodeId))
		{
			return EVBSkillUnlockResult::AlreadyUnlocked;
		}
		if (Level < Node->RequiredLevel)
		{
			return EVBSkillUnlockResult::LevelTooLow;
		}
		for (const FGameplayTag& Prerequisite : Node->Prerequisites)
		{
			if (!Unlocked.Contains(Prerequisite))
			{
				return EVBSkillUnlockResult::MissingPrerequisite;
			}
		}
		if (PointsAvailable < FMath::Max(1, Node->Cost))
		{
			return EVBSkillUnlockResult::NotEnoughPoints;
		}
		return EVBSkillUnlockResult::Unlocked;
	}

	const TCHAR* LexToString(EVBSkillUnlockResult Result)
	{
		switch (Result)
		{
		case EVBSkillUnlockResult::Unlocked: return TEXT("열림");
		case EVBSkillUnlockResult::UnknownNode: return TEXT("모르는 칸");
		case EVBSkillUnlockResult::AlreadyUnlocked: return TEXT("이미 열림");
		case EVBSkillUnlockResult::LevelTooLow: return TEXT("레벨 부족");
		case EVBSkillUnlockResult::MissingPrerequisite: return TEXT("앞 칸이 안 열림");
		case EVBSkillUnlockResult::NotEnoughPoints: return TEXT("포인트 부족");
		case EVBSkillUnlockResult::Unavailable: return TEXT("성장 설정·권한 없음");
		}
		return TEXT("?");
	}

	FNotices StepNoticeBaseline(FNoticeBaseline& Baseline, int32 Level, TConstArrayView<FGameplayTag> Unlocked, int32 SaveReadSerial)
	{
		FNotices Out;
		// 처음 본 것·불러온 것은 기준만 잡는다(선언 주석). 그 밖의 갱신에서만 직전과 비교한다.
		if (Baseline.bSeen && SaveReadSerial == Baseline.SaveReadSerial)
		{
			if (Level > Baseline.LastLevel)
			{
				Out.LevelUpTo = Level;
			}
			for (const FGameplayTag& NodeId : Unlocked)
			{
				if (!Baseline.Unlocked.Contains(NodeId))
				{
					Out.NewlyUnlocked.Add(NodeId);
				}
			}
		}
		Baseline.LastLevel = Level;
		Baseline.Unlocked = TArray<FGameplayTag>(Unlocked);
		Baseline.SaveReadSerial = SaveReadSerial;
		Baseline.bSeen = true;
		return Out;
	}
}

void FVBEffect_GiveXP::Apply(const FVBRuleContext& Context) const
{
	UVBProgressionComponent* Progression = UVBProgressionComponent::FindFor(Context.Instigator);
	if (!Progression || Amount <= 0)
	{
		VB_LOG(Warning, "효과 경험치 주기(%d): %s", Amount, !Progression ? TEXT("부른 플레이어에게 성장 컴포넌트가 없다") : TEXT("양이 0 이하"));
		return;
	}
	Progression->GiveXP(Amount);
}
