// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Quest/VBQuestDefinition.h"

#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBQuestDefinition"

int32 FVBQuestObjective_Event::CountEvent(const FVBGameEvent& Event) const
{
	if (!EventTag.IsValid() || !Event.EventTag.MatchesTag(EventTag))
	{
		return 0;
	}
	if (!TargetQuery.IsEmpty() && !TargetQuery.Matches(Event.TargetTags))
	{
		return 0;
	}
	return FMath::Max(1, Event.Count);
}

bool FVBQuestObjective_Condition::Evaluate(const FVBRuleContext& Context) const
{
	// 빈 배열은 VBRules 에서 참이다 - 조건 없는 조건 목표는 저작 실수라 검증기가 오류로 잡는다(IsDataValid).
	return VBRules::EvaluateAll(Conditions, Context);
}

FPrimaryAssetType UVBQuestDefinition::GetQuestAssetType()
{
	// 함수 안 정적 변수 - FName 은 모듈 정적 초기화 때 아직 쓸 수 없을 수 있다(UVBItemDefinition 과 같은 이유).
	static const FPrimaryAssetType Type(TEXT("VBQuest"));
	return Type;
}

FPrimaryAssetId UVBQuestDefinition::MakeQuestAssetId(FGameplayTag InQuestId)
{
	return FPrimaryAssetId(GetQuestAssetType(), InQuestId.GetTagName());
}

FPrimaryAssetId UVBQuestDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetQuestAssetType(), QuestId.IsValid() ? QuestId.GetTagName() : GetFName());
}

#if WITH_EDITOR
EDataValidationResult UVBQuestDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Fail = [&Result, &Context](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!QuestId.IsValid())
	{
		Fail(LOCTEXT("NoQuestId", "QuestId 가 비었다 - 저장·조건·대화가 이 퀘스트를 부를 수 없다."));
	}
	if (Stages.IsEmpty())
	{
		Fail(LOCTEXT("NoStages", "단계가 없다 - 시작하자마자 끝난다."));
	}
	for (int32 StageIndex = 0; StageIndex < Stages.Num(); ++StageIndex)
	{
		const FVBQuestStage& Stage = Stages[StageIndex];
		const FText Where = FText::Format(LOCTEXT("StageWhere", "단계 {0}"), StageIndex);
		if (!VBRules::ValidateStructArray(Stage.Objectives, FVBQuestObjective::StaticStruct(), Where, Context)
			|| !VBRules::ValidateStructArray(Stage.OnCompleteEffects, FVBEffect::StaticStruct(), Where, Context))
		{
			Result = EDataValidationResult::Invalid;
			continue;
		}
		int32 Required = 0;
		for (int32 ObjectiveIndex = 0; ObjectiveIndex < Stage.Objectives.Num(); ++ObjectiveIndex)
		{
			const FVBQuestObjective& Objective = Stage.Objectives[ObjectiveIndex].Get<FVBQuestObjective>();
			Required += Objective.bOptional ? 0 : 1;
			const FText ObjectiveWhere = FText::Format(LOCTEXT("ObjectiveWhere", "{0} 목표 {1}"), Where, ObjectiveIndex);
			if (const FVBQuestObjective_Event* EventObjective = Stage.Objectives[ObjectiveIndex].GetPtr<FVBQuestObjective_Event>())
			{
				if (!EventObjective->EventTag.IsValid())
				{
					Fail(FText::Format(LOCTEXT("NoEventTag", "{0}: 사건 태그가 비었다 - 아무것도 안 센다."), ObjectiveWhere));
				}
			}
			if (const FVBQuestObjective_Condition* ConditionObjective = Stage.Objectives[ObjectiveIndex].GetPtr<FVBQuestObjective_Condition>())
			{
				if (ConditionObjective->Conditions.IsEmpty())
				{
					Fail(FText::Format(LOCTEXT("NoConditions", "{0}: 조건이 없다 - 단계가 시작하자마자 찬다."), ObjectiveWhere));
				}
				else if (!VBRules::ValidateStructArray(ConditionObjective->Conditions, FVBCondition::StaticStruct(), ObjectiveWhere, Context))
				{
					Result = EDataValidationResult::Invalid;
				}
			}
		}
		if (Required == 0)
		{
			Fail(FText::Format(LOCTEXT("NoRequired", "{0}: 필수 목표가 없다 - 단계가 시작하자마자 넘어간다."), Where));
		}
	}
	if (!VBRules::ValidateStructArray(Rewards, FVBEffect::StaticStruct(), LOCTEXT("RewardsWhere", "보상"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	// 부모(UObject)는 NotValidated 를 돌려준다. 여기까지 오류가 없으면 «봤고 멀쩡하다» 로 올린다.
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
