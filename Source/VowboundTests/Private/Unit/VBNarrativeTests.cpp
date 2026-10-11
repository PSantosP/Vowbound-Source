#include "Misc/AutomationTest.h"
#include "Kismet/GameplayStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryState.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "SaveSystem/VBSaveGame.h"

// 서사 2단계 공용 셋의 단위 시험. 설계 .sw/narrative 9절.
// 사실 태그 뿌리(Story.*)는 아직 내용이 없어 부모·자식이 있는 네이티브 태그를 빌려 쓴다 - 연산은 태그 이름과 무관하다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStoryStateTest, "Vowbound.Unit.Narrative.StoryState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStoryStateTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Parent = VBGameplayTags::Melee_Shape;
	const FGameplayTag Child = VBGameplayTags::Melee_Shape_Weapon;
	const FGameplayTag Other = VBGameplayTags::Melee_Shape_HandL;

	FVBStoryState State;
	TestFalse(TEXT("unset fact is false"), State.HasFact(Child));
	TestTrue(TEXT("turning a fact on reports a change"), State.SetFact(Child, true));
	TestTrue(TEXT("fact is on"), State.HasFact(Child));
	TestFalse(TEXT("same value again reports no change"), State.SetFact(Child, true));
	TestFalse(TEXT("child fact does not make the parent true"), State.HasFact(Parent));
	TestFalse(TEXT("invalid tag is never a fact"), State.SetFact(FGameplayTag(), true));
	TestTrue(TEXT("turning it off reports a change"), State.SetFact(Child, false));
	TestFalse(TEXT("fact is off"), State.HasFact(Child));

	TestEqual(TEXT("missing counter reads 0"), State.GetCounter(Other), 0);
	TestEqual(TEXT("add returns the new value"), State.AddToCounter(Other, 3), 3);
	TestEqual(TEXT("negative add subtracts"), State.AddToCounter(Other, -1), 2);
	TestEqual(TEXT("reaching 0 returns 0"), State.AddToCounter(Other, -2), 0);
	TestEqual(TEXT("a zero counter is not stored (one meaning for 0)"), State.Counters.Num(), 0);

	FVBStoryState A, B;
	A.SetFact(Child, true);
	A.AddToCounter(Other, 5);
	B.AddToCounter(Other, 5);
	TestFalse(TEXT("different facts are not equal"), A == B);
	B.SetFact(Child, true);
	TestTrue(TEXT("same facts and counters are equal"), A == B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRulesTest, "Vowbound.Unit.Narrative.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRulesTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Fact = VBGameplayTags::Melee_Shape_Weapon;
	const FGameplayTag Counter = VBGameplayTags::Melee_Shape_HandR;

	// 주인 액터 없는 컴포넌트 = 복제할 곳이 없어 쓰기를 받는다(UVBStoryStateComponent::CanWrite).
	UVBStoryStateComponent* Story = NewObject<UVBStoryStateComponent>();
	FVBRuleContext Context;
	Context.Story = Story;
	TestTrue(TEXT("no world means effects may apply (nothing to replicate to)"), Context.CanApplyEffects());

	TArray<FInstancedStruct> Conditions;
	TestTrue(TEXT("empty condition list is true"), VBRules::EvaluateAll(Conditions, Context));

	FVBCondition_Fact HasFact;
	HasFact.Fact = Fact;
	Conditions.Add(FInstancedStruct::Make(HasFact));
	TestFalse(TEXT("fact condition is false before the effect"), VBRules::EvaluateAll(Conditions, Context));

	TArray<FInstancedStruct> Effects;
	FVBEffect_SetFact TurnOn;
	TurnOn.Fact = Fact;
	Effects.Add(FInstancedStruct::Make(TurnOn));
	FVBEffect_AddCounter AddTwo;
	AddTwo.Counter = Counter;
	AddTwo.Delta = 2;
	Effects.Add(FInstancedStruct::Make(AddTwo));
	TestTrue(TEXT("effects apply"), VBRules::ApplyAll(Effects, Context));
	TestTrue(TEXT("fact condition is true after the effect"), VBRules::EvaluateAll(Conditions, Context));

	FVBCondition_Fact NotFact = HasFact;
	NotFact.bNegate = true;
	TestFalse(TEXT("negated fact condition is false"), VBRules::EvaluateAll({FInstancedStruct::Make(NotFact)}, Context));

	FVBCondition_Counter AtLeastTwo;
	AtLeastTwo.Counter = Counter;
	AtLeastTwo.Compare = EVBRuleCompare::AtLeast;
	AtLeastTwo.Value = 2;
	FVBCondition_Counter AtLeastThree = AtLeastTwo;
	AtLeastThree.Value = 3;
	TestTrue(TEXT("counter 2 >= 2"), VBRules::EvaluateAll({FInstancedStruct::Make(AtLeastTwo)}, Context));
	TestFalse(TEXT("counter 2 >= 3 is false"), VBRules::EvaluateAll({FInstancedStruct::Make(AtLeastThree)}, Context));
	TestFalse(TEXT("all conditions must hold"), VBRules::EvaluateAll({FInstancedStruct::Make(AtLeastTwo), FInstancedStruct::Make(AtLeastThree)}, Context));

	// 알 수 없는 것은 참으로 흘리지 않는다 - 빈 칸, 조건이 아닌 구조체, 사실 저장소 없는 문맥.
	AddExpectedMessagePlain(TEXT("조건 배열에 조건이 아닌 원소"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("empty slot is false"), VBRules::EvaluateAll({FInstancedStruct()}, Context));
	TestFalse(TEXT("non-condition struct is false"), VBRules::EvaluateAll({FInstancedStruct::Make(FVBStoryCounter())}, Context));
	FVBRuleContext NoStory;
	AddExpectedMessagePlain(TEXT("문맥에 사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("no fact store is false"), VBRules::EvaluateAll(Conditions, NoStory));

	// 바탕형 조건은 참(조건 없는 자리의 기본), 바탕형 효과는 아무것도 안 한다.
	TestTrue(TEXT("base condition is true"), VBRules::EvaluateAll({FInstancedStruct::Make(FVBCondition())}, Context));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStorySaveRoundTripTest, "Vowbound.Unit.Narrative.SaveRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStorySaveRoundTripTest::RunTest(const FString& Parameters)
{
	UVBStoryStateComponent* Source = NewObject<UVBStoryStateComponent>();
	Source->SetFact(VBGameplayTags::Melee_Shape_Weapon, true);
	Source->SetFact(VBGameplayTags::Melee_Window, true);
	Source->AddToCounter(VBGameplayTags::Melee_Shape_FootL, 7);

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	const FVBSaveContext Context;
	Source->WriteSave(*Save, Context);

	// 바이트로 한 번 굴린다 - SaveGame 표시가 빠진 필드는 여기서 조용히 사라진다(UVBSaveGame 직렬화 규칙).
	TArray<uint8> Bytes;
	TestTrue(TEXT("save serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	const UVBSaveGame* Loaded = Cast<UVBSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("save deserializes"), Loaded))
	{
		return false;
	}

	UVBStoryStateComponent* Target = NewObject<UVBStoryStateComponent>();
	int32 Changed = 0;
	Target->OnStoryStateChanged.AddLambda([&Changed]() { ++Changed; });
	Target->ReadSave(*Loaded, Context);
	TestTrue(TEXT("facts and counters survive the byte round trip"), Target->GetState() == Source->GetState());
	TestEqual(TEXT("counter value survives"), Target->GetCounter(VBGameplayTags::Melee_Shape_FootL), 7);
	TestEqual(TEXT("reading a save notifies listeners once"), Changed, 1);
	return true;
}
#endif
