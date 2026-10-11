// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Quest/VBQuestText.h"

#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"

#define LOCTEXT_NAMESPACE "VBQuestText"

FText VBQuestText::DescribeObjectives(const FVBQuestLogEntry& Entry)
{
	const UVBQuestDefinition* Quest = Entry.Definition;
	if (!Quest || Entry.Status != EVBQuestStatus::Active || !Quest->Stages.IsValidIndex(Entry.StageIndex))
	{
		return FText::GetEmpty();
	}
	const FVBQuestStage& Stage = Quest->Stages[Entry.StageIndex];
	TArray<FText> Lines;
	for (int32 Index = 0; Index < Stage.Objectives.Num(); ++Index)
	{
		const FVBQuestObjective* Objective = Stage.Objectives[Index].GetPtr<FVBQuestObjective>();
		if (!Objective)
		{
			continue;
		}
		const int32 Required = Objective->GetRequiredCount();
		const int32 Done = Entry.Progress.IsValidIndex(Index) ? Entry.Progress[Index] : 0;
		FText Line = Objective->Description;
		if (Done >= Required)
		{
			Line = FText::Format(LOCTEXT("Done", "{0} (완료)"), Line);
		}
		else if (Required > 1)
		{
			Line = FText::Format(LOCTEXT("Count", "{0} ({1}/{2})"), Line, Done, Required);
		}
		if (Objective->bOptional)
		{
			Line = FText::Format(LOCTEXT("Optional", "(선택) {0}"), Line);
		}
		Lines.Add(FText::Format(LOCTEXT("Bullet", "- {0}"), Line));
	}
	return FText::Join(FText::FromString(TEXT("\n")), Lines);
}

FText VBQuestText::DescribeTracker(const FVBQuestLogEntry& Entry)
{
	const UVBQuestDefinition* Quest = Entry.Definition;
	if (!Quest || Entry.Status != EVBQuestStatus::Active || !Quest->Stages.IsValidIndex(Entry.StageIndex))
	{
		return FText::GetEmpty();
	}
	return FText::Format(LOCTEXT("Tracker", "{0}\n{1}\n{2}"), Quest->Title, Quest->Stages[Entry.StageIndex].Description,
	                     DescribeObjectives(Entry));
}

#undef LOCTEXT_NAMESPACE
