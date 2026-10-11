#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestText.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "SaveSystem/VBSaveGame.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 퀘스트 단계의 단위 시험. 설계 .sw/narrative 12절.
// 주인 액터 없는 일지 = 권한 검사가 통과하는 자리라 서버 경로가 돌고, 주인 클라 RPC(알림)도 이 자리에서 바로 실행된다.
// 사건·퀘스트 태그(Quest.*·Enemy.*)는 ini 내용이라 시험은 네이티브 태그를 빌려 쓴다 - 연산은 태그 이름과 무관하다.

#if WITH_AUTOMATION_TESTS
namespace
{
	FInstancedStruct MakeEventObjective(FGameplayTag EventTag, int32 Required, FGameplayTag Target = FGameplayTag(), bool bOptional = false)
	{
		FVBQuestObjective_Event Objective;
		Objective.Description = FText::FromString(TEXT("objective"));
		Objective.EventTag = EventTag;
		Objective.RequiredCount = Required;
		Objective.bOptional = bOptional;
		if (Target.IsValid())
		{
			Objective.TargetQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(Target));
		}
		return FInstancedStruct::Make(Objective);
	}

	FVBGameEvent MakeEvent(FGameplayTag EventTag, FGameplayTag Target = FGameplayTag(), int32 Count = 1)
	{
		FVBGameEvent Event;
		Event.EventTag = EventTag;
		if (Target.IsValid())
		{
			Event.TargetTags.AddTag(Target);
		}
		Event.Count = Count;
		return Event;
	}

	// 두 단계: 1) 처치 2번(대상 = 병사 표지) + 선택 목표 하나 2) 줍기 1번.
	UVBQuestDefinition* MakeTwoStageQuest()
	{
		UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
		Quest->QuestId = VBGameplayTags::Melee_Window;
		Quest->Title = FText::FromString(TEXT("test quest"));
		FVBQuestStage& Kill = Quest->Stages.AddDefaulted_GetRef();
		Kill.Description = FText::FromString(TEXT("kill"));
		Kill.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Enemy_Killed, 2, VBGameplayTags::Melee_Shape_Weapon));
		Kill.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Item_Used, 1, FGameplayTag(), /*bOptional=*/true));
		FVBQuestStage& Pick = Quest->Stages.AddDefaulted_GetRef();
		Pick.Description = FText::FromString(TEXT("pick"));
		Pick.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Item_Acquired, 1));
		return Quest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestProgressTest, "Vowbound.Unit.Quest.Progress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestProgressTest::RunTest(const FString& Parameters)
{
	const UVBQuestDefinition* Quest = MakeTwoStageQuest();
	UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
	TArray<EVBQuestNotice> Notices;
	Log->OnQuestNotice.AddLambda([&Notices](const UVBQuestDefinition*, EVBQuestNotice Notice) { Notices.Add(Notice); });

	TestEqual(TEXT("not started before StartQuest"), Log->GetStatus(Quest), EVBQuestStatus::NotStarted);
	TestTrue(TEXT("start"), Log->StartQuest(Quest));
	TestEqual(TEXT("active at stage 0"), Log->GetStageIndex(Quest), 0);
	TestFalse(TEXT("starting twice is refused"), Log->StartQuest(Quest));

	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_HandL));
	TestEqual(TEXT("kill of another kind does not count"), Log->GetEntries()[0].Progress[0], 0);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));
	TestEqual(TEXT("matching kill counts"), Log->GetEntries()[0].Progress[0], 1);
	TestTrue(TEXT("tracker text shows n/m"), VBQuestText::DescribeObjectives(Log->GetEntries()[0]).ToString().Contains(TEXT("(1/2)")));
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));
	TestEqual(TEXT("required objectives done -> next stage (optional one is not needed)"), Log->GetStageIndex(Quest), 1);
	TestEqual(TEXT("progress restarts for the new stage"), Log->GetEntries()[0].Progress.Num(), 1);

	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));
	TestEqual(TEXT("events for a finished stage do not leak into the next"), Log->GetEntries()[0].Progress[0], 0);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired));
	TestEqual(TEXT("last stage done -> completed"), Log->GetStatus(Quest), EVBQuestStatus::Completed);
	TestEqual(TEXT("completed quest has no stage"), Log->GetStageIndex(Quest), static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("completed quest cannot be restarted"), Log->StartQuest(Quest));
	TestNull(TEXT("nothing to track once completed"), Log->GetTrackedEntry());

	TestEqual(TEXT("notices: started, advanced, completed"), Notices.Num(), 3);
	if (Notices.Num() == 3)
	{
		TestTrue(TEXT("notice order"), Notices[0] == EVBQuestNotice::Started && Notices[1] == EVBQuestNotice::StageAdvanced
		                                && Notices[2] == EVBQuestNotice::Completed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestCountTest, "Vowbound.Unit.Quest.EventCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestCountTest::RunTest(const FString& Parameters)
{
	// 사건의 수(주운 개수)만큼 세고, 상한에서 멈춘다. 계층 태그: 목표 Event.Item 은 Event.Item.Acquired 를 품는다.
	UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
	Quest->QuestId = VBGameplayTags::Melee_Window;
	FVBQuestStage& Stage = Quest->Stages.AddDefaulted_GetRef();
	Stage.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Item_Acquired.GetTag().RequestDirectParent(), 5));
	Stage.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Enemy_Killed, 1));

	UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
	Log->StartQuest(Quest);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired, FGameplayTag(), 3));
	TestEqual(TEXT("event count is added"), Log->GetEntries()[0].Progress[0], 3);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired, FGameplayTag(), 9));
	TestEqual(TEXT("progress stops at the required count"), Log->GetEntries()[0].Progress[0], 5);
	TestEqual(TEXT("one objective left keeps the stage"), Log->GetStageIndex(Quest), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestSaveWriteTest, "Vowbound.Unit.Quest.SaveWrite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestSaveWriteTest::RunTest(const FString& Parameters)
{
	// 읽기(ReadSave)는 에셋 매니저가 실제 퀘스트 자산을 되찾아야 해서 여기서 못 돈다 - 쓰는 모양만 본다.
	const UVBQuestDefinition* Quest = MakeTwoStageQuest();
	UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
	Log->StartQuest(Quest);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Log->WriteSave(*Save, FVBSaveContext());
	TestEqual(TEXT("one quest written"), Save->Quests.Quests.Num(), 1);
	if (Save->Quests.Quests.Num() == 1)
	{
		const FVBSavedQuest& Saved = Save->Quests.Quests[0];
		TestTrue(TEXT("saved by QuestId tag"), Saved.QuestId == Quest->QuestId);
		TestFalse(TEXT("not completed"), Saved.bCompleted);
		TestEqual(TEXT("stage"), Saved.StageIndex, 0);
		TestTrue(TEXT("progress kept"), Saved.Progress.Num() == 2 && Saved.Progress[0] == 1);
	}
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestValidateTest, "Vowbound.Unit.Quest.Validate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestValidateTest::RunTest(const FString& Parameters)
{
	{
		FDataValidationContext Context;
		TestEqual(TEXT("well-formed quest is valid"), MakeTwoStageQuest()->IsDataValid(Context), EDataValidationResult::Valid);
	}
	{
		UVBQuestDefinition* OnlyOptional = MakeTwoStageQuest();
		OnlyOptional->Stages[0].Objectives.RemoveAt(0);
		FDataValidationContext Context;
		TestEqual(TEXT("a stage with only optional objectives is invalid"), OnlyOptional->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	{
		UVBQuestDefinition* NoTag = MakeTwoStageQuest();
		NoTag->Stages[1].Objectives[0] = MakeEventObjective(FGameplayTag(), 1);
		FDataValidationContext Context;
		TestEqual(TEXT("an event objective without a tag is invalid"), NoTag->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	{
		UVBQuestDefinition* NoId = MakeTwoStageQuest();
		NoId->QuestId = FGameplayTag();
		FDataValidationContext Context;
		TestEqual(TEXT("missing QuestId is invalid"), NoId->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	return true;
}
#endif

#endif
