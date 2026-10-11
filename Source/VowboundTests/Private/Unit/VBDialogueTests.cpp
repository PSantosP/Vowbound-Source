#include "Misc/AutomationTest.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 대화 단계의 단위 시험. 설계 .sw/narrative 11절.
// 주인 액터 없는 대화 컴포넌트 = 권한 검사가 통과하는 자리라 서버 경로가 돌고, 서버·클라 RPC 도 이 자리에서 바로 실행된다.
// 사실 저장소(GameState)가 없는 자리라 «잠긴 조건» 은 빈 조건 칸으로 만든다 - 빈 칸은 거짓이다(VBRules::EvaluateAll).

#if WITH_AUTOMATION_TESTS
namespace
{
	FVBDialogueChoice MakeChoice(const TCHAR* Text, FName Next, bool bLocked = false)
	{
		FVBDialogueChoice Choice;
		Choice.Text = FText::FromString(Text);
		Choice.NextNodeId = Next;
		if (bLocked)
		{
			Choice.Conditions.AddDefaulted();
		}
		return Choice;
	}

	FVBDialogueNode& AddNode(UVBDialogueAsset* Dialogue, FName NodeId, FName Next = NAME_None)
	{
		FVBDialogueNode& Node = Dialogue->Nodes.AddDefaulted_GetRef();
		Node.NodeId = NodeId;
		Node.Line = FText::FromName(NodeId);
		Node.NextNodeId = Next;
		return Node;
	}

	// First -(선택 0)-> Who -(계속)-> Last -(계속)-> 끝. First 의 선택지 1 은 잠겼고 2 는 바로 끝.
	UVBDialogueAsset* MakeTestDialogue()
	{
		UVBDialogueAsset* Dialogue = NewObject<UVBDialogueAsset>();
		FVBDialogueEntryPoint& Locked = Dialogue->EntryPoints.AddDefaulted_GetRef();
		Locked.Conditions.AddDefaulted();
		Locked.StartNodeId = TEXT("Last");
		Dialogue->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("First");

		FVBDialogueNode& First = AddNode(Dialogue, TEXT("First"));
		First.Choices.Add(MakeChoice(TEXT("who"), TEXT("Who")));
		First.Choices.Add(MakeChoice(TEXT("locked"), TEXT("Last"), /*bLocked=*/true));
		First.Choices.Add(MakeChoice(TEXT("bye"), NAME_None));
		AddNode(Dialogue, TEXT("Who"), TEXT("Last"));
		AddNode(Dialogue, TEXT("Last"));
		return Dialogue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueEntryTest, "Vowbound.Unit.Dialogue.Entry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueEntryTest::RunTest(const FString& Parameters)
{
	const UVBDialogueAsset* Dialogue = MakeTestDialogue();
	AddExpectedMessagePlain(TEXT("조건이 아닌 원소"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("first true entry wins; the locked one is skipped"), Dialogue->ChooseEntry(FVBRuleContext()), FName(TEXT("First")));
	TestNotNull(TEXT("node found by id"), Dialogue->FindNode(TEXT("Who")));
	TestNull(TEXT("missing node is null"), Dialogue->FindNode(TEXT("Nope")));
	TestNull(TEXT("empty id is null"), Dialogue->FindNode(NAME_None));

	const UVBDialogueAsset* Empty = NewObject<UVBDialogueAsset>();
	TestTrue(TEXT("no entry points = nothing to say"), Empty->ChooseEntry(FVBRuleContext()).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueFlowTest, "Vowbound.Unit.Dialogue.Flow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueFlowTest::RunTest(const FString& Parameters)
{
	const UVBDialogueAsset* Dialogue = MakeTestDialogue();
	UVBDialogueComponent* Component = NewObject<UVBDialogueComponent>();
	int32 ViewChanges = 0;
	Component->OnViewChanged.AddLambda([&ViewChanges]() { ++ViewChanges; });

	// 진입점 판정 1 + 첫 노드 선택지 판정 1 + 잠긴 선택 요청 재판정 1 = 빈 조건 칸 경고 3.
	AddExpectedMessagePlain(TEXT("조건이 아닌 원소"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
	AddExpectedMessagePlain(TEXT("선택지 5 번이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	TestTrue(TEXT("start"), Component->StartDialogue(Dialogue, nullptr));
	TestTrue(TEXT("server is in dialogue"), Component->IsInDialogue());
	TestEqual(TEXT("view shows the entry node"), Component->GetView().NodeId, FName(TEXT("First")));
	TestEqual(TEXT("availability per choice"), Component->GetView().ChoiceAvailable.Num(), 3);
	TestTrue(TEXT("open choice is available"), Component->GetView().ChoiceAvailable[0]);
	TestFalse(TEXT("locked choice is not"), Component->GetView().ChoiceAvailable[1]);
	TestFalse(TEXT("second start while talking is refused"), Component->StartDialogue(Dialogue, nullptr));

	Component->RequestChoose(1);
	TestEqual(TEXT("locked choice does not move"), Component->GetView().NodeId, FName(TEXT("First")));
	Component->RequestChoose(5);
	TestEqual(TEXT("bad index does not move"), Component->GetView().NodeId, FName(TEXT("First")));

	Component->RequestChoose(0);
	TestEqual(TEXT("choice follows its link"), Component->GetView().NodeId, FName(TEXT("Who")));
	Component->RequestContinue();
	TestEqual(TEXT("continue follows NextNodeId"), Component->GetView().NodeId, FName(TEXT("Last")));
	Component->RequestContinue();
	TestFalse(TEXT("continue past the last node ends"), Component->IsInDialogue());
	TestFalse(TEXT("view is cleared on end"), Component->GetView().IsActive());
	// 보기 바뀜: First, Who, Last, 끝.
	TestEqual(TEXT("one view change per node plus the end"), ViewChanges, 4);

	Component->RequestContinue();
	TestFalse(TEXT("request after the end is ignored"), Component->IsInDialogue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueLeaveTest, "Vowbound.Unit.Dialogue.Leave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueLeaveTest::RunTest(const FString& Parameters)
{
	const UVBDialogueAsset* Dialogue = MakeTestDialogue();
	UVBDialogueComponent* Component = NewObject<UVBDialogueComponent>();
	AddExpectedMessagePlain(TEXT("조건이 아닌 원소"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 4);

	Component->StartDialogue(Dialogue, nullptr);
	Component->RequestLeave();
	TestFalse(TEXT("leave ends the dialogue"), Component->IsInDialogue());

	Component->StartDialogue(Dialogue, nullptr);
	Component->RequestChoose(2);
	TestFalse(TEXT("a choice with no link ends the dialogue"), Component->IsInDialogue());
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueValidateTest, "Vowbound.Unit.Dialogue.Validate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueValidateTest::RunTest(const FString& Parameters)
{
	// 시험용 잠금(빈 조건 칸)을 걷어낸 것이 «멀쩡한» 대화다. 걷어내기 전 것은 검증기가 저작 실수로 본다.
	auto MakeClean = []()
	{
		UVBDialogueAsset* Dialogue = MakeTestDialogue();
		Dialogue->EntryPoints[0].Conditions.Reset();
		Dialogue->Nodes[0].Choices[1].Conditions.Reset();
		return Dialogue;
	};
	{
		FDataValidationContext Context;
		TestEqual(TEXT("well-formed dialogue is valid"), MakeClean()->IsDataValid(Context), EDataValidationResult::Valid);
	}
	{
		FDataValidationContext Context;
		TestEqual(TEXT("empty condition slot is invalid"), MakeTestDialogue()->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	{
		UVBDialogueAsset* Broken = MakeClean();
		Broken->Nodes[1].NextNodeId = TEXT("Nowhere");
		FDataValidationContext Context;
		TestEqual(TEXT("dangling link is invalid"), Broken->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	{
		// 끝으로 가는 길이 없는 고리: A -> B -> A. 진입점에서 닿으니 «닿지 않음» 경고가 아니라 오류여야 한다.
		UVBDialogueAsset* Trapped = NewObject<UVBDialogueAsset>();
		Trapped->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("A");
		AddNode(Trapped, TEXT("A"), TEXT("B"));
		AddNode(Trapped, TEXT("B"), TEXT("A"));
		FDataValidationContext Context;
		TestEqual(TEXT("a loop with no way to the end is invalid"), Trapped->IsDataValid(Context), EDataValidationResult::Invalid);

		// 같은 고리에 끝으로 나가는 선택지 하나를 달면 유효하다(대조).
		Trapped->Nodes[1].Choices.Add(MakeChoice(TEXT("back"), TEXT("A")));
		Trapped->Nodes[1].Choices.Add(MakeChoice(TEXT("leave"), NAME_None));
		Trapped->Nodes[1].NextNodeId = NAME_None;
		FDataValidationContext ExitContext;
		TestEqual(TEXT("the same loop with an exit is valid"), Trapped->IsDataValid(ExitContext), EDataValidationResult::Valid);
	}
	{
		UVBDialogueAsset* Duplicate = MakeClean();
		AddNode(Duplicate, TEXT("Who"));
		FDataValidationContext Context;
		TestEqual(TEXT("duplicate node id is invalid"), Duplicate->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	return true;
}
#endif

#endif
