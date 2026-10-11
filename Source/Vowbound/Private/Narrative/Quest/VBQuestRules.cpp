// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Quest/VBQuestRules.h"

#include "GameFramework/PlayerState.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Vowbound/Vowbound.h"

void FVBEffect_StartQuest::Apply(const FVBRuleContext& Context) const
{
	UVBQuestLogComponent* Log = UVBQuestLogComponent::FindFor(Context.Instigator);
	if (!Log || !Quest)
	{
		VB_LOG(Warning, "효과(퀘스트 시작 %s): %s", *GetNameSafe(Quest), !Quest ? TEXT("퀘스트가 비었다") : TEXT("부른 플레이어에게 퀘스트 일지가 없다"));
		return;
	}
	Log->StartQuest(Quest);
}

bool FVBCondition_QuestState::Evaluate(const FVBRuleContext& Context) const
{
	const UVBQuestLogComponent* Log = UVBQuestLogComponent::FindFor(Context.Instigator);
	if (!Log || !Quest)
	{
		// 읽을 곳이 없으면 거짓 - 알 수 없는 것을 참으로 흘리지 않는다(VBRules 규약).
		VB_LOG(Warning, "조건(퀘스트 상태 %s): %s - 거짓으로 친다", *GetNameSafe(Quest), !Quest ? TEXT("퀘스트가 비었다") : TEXT("부른 플레이어에게 퀘스트 일지가 없다"));
		return false;
	}
	if (Log->GetStatus(Quest) != Status)
	{
		return false;
	}
	return Stage < 0 || Status != EVBQuestStatus::Active || Log->GetStageIndex(Quest) == Stage;
}
