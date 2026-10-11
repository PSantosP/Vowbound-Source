#include "Misc/AutomationTest.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "UObject/Package.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Player/VBPlayerState.h"
#include "Progression/VBGrowthConfig.h"
#include "Progression/VBGrowthRules.h"
#include "Progression/VBProgressionComponent.h"
#include "Progression/VBSkillTreeDefinition.h"
#include "SaveSystem/VBSaveGame.h"

// 성장 판정의 단위 시험. 설계 .sw/remaining-features/b2-growth.md.
// 칸 이름은 판정과 무관하다 - 네이티브 태그를 빌려 쓴다(장비 시험과 같은 방식).

#if WITH_AUTOMATION_TESTS
namespace
{
	FVBSkillNode MakeNode(const FGameplayTag& Id, int32 Cost, int32 Level, TArray<FGameplayTag> Prerequisites = {})
	{
		FVBSkillNode Node;
		Node.NodeId = Id;
		Node.Cost = Cost;
		Node.RequiredLevel = Level;
		Node.Prerequisites = MoveTemp(Prerequisites);
		Node.Stats.Add(VBGameplayTags::Melee_Shape_Weapon, 1.0f);
		return Node;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGrowthLevelTableTest, "Vowbound.Unit.Growth.LevelTable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGrowthLevelTableTest::RunTest(const FString& Parameters)
{
	const TArray<int32> Table = {100, 150, 200}; // 최대 레벨 4, 누적 문턱 100 / 250 / 450
	TestEqual(TEXT("0 xp = level 1"), VBGrowth::LevelForXP(Table, 0), 1);
	TestEqual(TEXT("negative xp = level 1"), VBGrowth::LevelForXP(Table, -5), 1);
	TestEqual(TEXT("99 = level 1"), VBGrowth::LevelForXP(Table, 99), 1);
	TestEqual(TEXT("100 = level 2 (threshold reached)"), VBGrowth::LevelForXP(Table, 100), 2);
	TestEqual(TEXT("249 = level 2"), VBGrowth::LevelForXP(Table, 249), 2);
	TestEqual(TEXT("450 = level 4 (max)"), VBGrowth::LevelForXP(Table, 450), 4);
	TestEqual(TEXT("overflow past max stays at max"), VBGrowth::LevelForXP(Table, 100000), 4);
	TestEqual(TEXT("empty table = level 1 forever"), VBGrowth::LevelForXP({}, 100000), 1);
	TestEqual(TEXT("a 0 step reads as 1 (no free levels)"), VBGrowth::LevelForXP(TArray<int32>{0, 0}, 1), 2);
	TestEqual(TEXT("level 1 starts at 0"), VBGrowth::XPAtLevelStart(Table, 1), 0);
	TestEqual(TEXT("level 3 starts at 250"), VBGrowth::XPAtLevelStart(Table, 3), 250);
	TestEqual(TEXT("past max = max start"), VBGrowth::XPAtLevelStart(Table, 9), 450);
	TestEqual(TEXT("points at level 1 = 0"), VBGrowth::PointsEarned(1, 1), 0);
	TestEqual(TEXT("points at level 4, 2 per level = 6"), VBGrowth::PointsEarned(4, 2), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGrowthUnlockTest, "Vowbound.Unit.Growth.Unlock",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGrowthUnlockTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Root = VBGameplayTags::Melee_Shape_Weapon;
	const FGameplayTag Child = VBGameplayTags::Melee_Shape_HandL;
	const FGameplayTag Expensive = VBGameplayTags::Melee_Shape_HandR;
	const FGameplayTag Unknown = VBGameplayTags::Melee_Shape_FootL;
	UVBSkillTreeDefinition* Tree = NewObject<UVBSkillTreeDefinition>();
	Tree->Nodes = {MakeNode(Root, 1, 1), MakeNode(Child, 1, 3, {Root}), MakeNode(Expensive, 3, 1)};

	TArray<FGameplayTag> Unlocked;
	TestTrue(TEXT("unknown node"), VBGrowth::CanUnlock(*Tree, Unlocked, 5, 9, Unknown) == EVBSkillUnlockResult::UnknownNode);
	TestTrue(TEXT("root opens with a point"), VBGrowth::CanUnlock(*Tree, Unlocked, 1, 1, Root) == EVBSkillUnlockResult::Unlocked);
	TestTrue(TEXT("no point, no root"), VBGrowth::CanUnlock(*Tree, Unlocked, 1, 0, Root) == EVBSkillUnlockResult::NotEnoughPoints);
	TestTrue(TEXT("debt (negative points) blocks"), VBGrowth::CanUnlock(*Tree, Unlocked, 1, -1, Root) == EVBSkillUnlockResult::NotEnoughPoints);
	TestTrue(TEXT("cost 3 needs 3"), VBGrowth::CanUnlock(*Tree, Unlocked, 1, 2, Expensive) == EVBSkillUnlockResult::NotEnoughPoints);
	TestTrue(TEXT("child: level checked before prerequisites"), VBGrowth::CanUnlock(*Tree, Unlocked, 2, 5, Child) == EVBSkillUnlockResult::LevelTooLow);
	TestTrue(TEXT("child: prerequisite missing"), VBGrowth::CanUnlock(*Tree, Unlocked, 3, 5, Child) == EVBSkillUnlockResult::MissingPrerequisite);
	Unlocked.Add(Root);
	TestTrue(TEXT("root again = already"), VBGrowth::CanUnlock(*Tree, Unlocked, 3, 5, Root) == EVBSkillUnlockResult::AlreadyUnlocked);
	TestTrue(TEXT("child opens after root"), VBGrowth::CanUnlock(*Tree, Unlocked, 3, 5, Child) == EVBSkillUnlockResult::Unlocked);
	Unlocked.Add(Expensive);
	TestEqual(TEXT("spent = 1 + 3"), VBGrowth::PointsSpent(*Tree, Unlocked), 4);
	Unlocked.Add(Unknown);
	TestEqual(TEXT("a node the tree no longer has costs nothing"), VBGrowth::PointsSpent(*Tree, Unlocked), 4);
	return true;
}

// HUD 레벨업·해금 알림 기준(f3 verify 10-08 Q3 - 이어하기 직후 알림이 몰려 떴다). 불러온 갱신은 조용하고, 그 뒤의 진짜 오름·해금은 알린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGrowthNoticeBaselineTest, "Vowbound.Unit.Growth.NoticeBaseline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGrowthNoticeBaselineTest::RunTest(const FString& Parameters)
{
	const FGameplayTag A = FGameplayTag::RequestGameplayTag(TEXT("Skill.Node.Body.Health1"));
	const FGameplayTag B = FGameplayTag::RequestGameplayTag(TEXT("Skill.Node.Body.Health2"));
	const FGameplayTag C = FGameplayTag::RequestGameplayTag(TEXT("Skill.Node.Blade.Attack1"));
	VBGrowth::FNoticeBaseline Baseline;

	VBGrowth::FNotices N = VBGrowth::StepNoticeBaseline(Baseline, 1, {}, 0);
	TestTrue(TEXT("the first look only sets the baseline"), N.LevelUpTo == 0 && N.NewlyUnlocked.IsEmpty());

	// 빙의 때 레벨 1 을 먼저 보고, 그 뒤 세이브(레벨 5, 칸 둘)가 적용된 갱신 - 몰려 뜨던 자리.
	N = VBGrowth::StepNoticeBaseline(Baseline, 5, { A, B }, 1);
	TestTrue(TEXT("a save load is quiet even when level and nodes jump"), N.LevelUpTo == 0 && N.NewlyUnlocked.IsEmpty());

	N = VBGrowth::StepNoticeBaseline(Baseline, 6, { A, B }, 1);
	TestEqual(TEXT("a real level-up after the load is announced"), N.LevelUpTo, 6);
	N = VBGrowth::StepNoticeBaseline(Baseline, 8, { A, B }, 1);
	TestEqual(TEXT("several levels at once collapse to the newest"), N.LevelUpTo, 8);

	N = VBGrowth::StepNoticeBaseline(Baseline, 8, { A, B, C }, 1);
	TestTrue(TEXT("a real unlock after the load is announced once"), N.NewlyUnlocked.Num() == 1 && N.NewlyUnlocked[0] == C && N.LevelUpTo == 0);
	N = VBGrowth::StepNoticeBaseline(Baseline, 8, { A, B, C }, 1);
	TestTrue(TEXT("no change, no notice"), N.LevelUpTo == 0 && N.NewlyUnlocked.IsEmpty());

	// 두 번째 불러오기(이어하기를 또 함)도 조용하다 - 레벨이 내려가도 알림은 없다.
	N = VBGrowth::StepNoticeBaseline(Baseline, 3, { A }, 2);
	TestTrue(TEXT("a second load is quiet too"), N.LevelUpTo == 0 && N.NewlyUnlocked.IsEmpty());
	N = VBGrowth::StepNoticeBaseline(Baseline, 4, { A, B }, 2);
	TestTrue(TEXT("and live progress after it is announced"), N.LevelUpTo == 4 && N.NewlyUnlocked.Num() == 1 && N.NewlyUnlocked[0] == B);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGrowthTreeValidationTest, "Vowbound.Unit.Growth.TreeValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGrowthTreeValidationTest::RunTest(const FString& Parameters)
{
	const FGameplayTag A = VBGameplayTags::Melee_Shape_Weapon;
	const FGameplayTag B = VBGameplayTags::Melee_Shape_HandL;
	const FGameplayTag C = VBGameplayTags::Melee_Shape_HandR;
	UVBSkillTreeDefinition* Tree = NewObject<UVBSkillTreeDefinition>();

	Tree->Nodes = {MakeNode(A, 1, 1), MakeNode(B, 1, 3, {A})};
	TestEqual(TEXT("a sound tree has no problems (control)"), UVBSkillTreeDefinition::FindProblems(*Tree).Num(), 0);

	Tree->Nodes = {MakeNode(A, 1, 1), MakeNode(A, 1, 1)};
	TestTrue(TEXT("duplicate id is a problem"), UVBSkillTreeDefinition::FindProblems(*Tree).Num() > 0);

	Tree->Nodes = {MakeNode(B, 1, 1, {C})};
	TestTrue(TEXT("a prerequisite the tree lacks is a problem"), UVBSkillTreeDefinition::FindProblems(*Tree).Num() > 0);

	Tree->Nodes = {MakeNode(A, 1, 1, {C}), MakeNode(B, 1, 1, {A}), MakeNode(C, 1, 1, {B})};
	TestTrue(TEXT("a prerequisite cycle is a problem"), UVBSkillTreeDefinition::FindProblems(*Tree).Num() > 0);

	Tree->Nodes = {MakeNode(A, 0, 1)};
	TestTrue(TEXT("cost 0 is a problem"), UVBSkillTreeDefinition::FindProblems(*Tree).Num() > 0);

	FVBSkillNode Empty = MakeNode(A, 1, 1);
	Empty.Stats.Reset();
	Tree->Nodes = {Empty};
	TestTrue(TEXT("a node that does nothing is a problem"), UVBSkillTreeDefinition::FindProblems(*Tree).Num() > 0);
	Empty.GrantedAbility = UGameplayAbility::StaticClass();
	Tree->Nodes = {Empty};
	TestEqual(TEXT("an ability-only node is fine"), UVBSkillTreeDefinition::FindProblems(*Tree).Num(), 0);
	return true;
}

namespace
{
	// Config 는 BP 기본값(EditDefaultsOnly)이라 시험은 리플렉션으로 꽂는다 - 장비 시험의 EquipmentEffect 와 같은 방식.
	void SetGrowthConfig(UVBProgressionComponent* Progression, UVBGrowthConfig* Config)
	{
		FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(UVBProgressionComponent::StaticClass()->FindPropertyByName(TEXT("Config")));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Progression, Config);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGrowthComponentTest, "Vowbound.Unit.Growth.Component",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGrowthComponentTest::RunTest(const FString& Parameters)
{
	// 실제 AVBPlayerState 위의 성장 컴포넌트: 경험치 -> 레벨 -> 포인트 -> 칸 열기 -> 효과·능력, 저장 쓰기·읽기.
	//   능력치 효과는 실제 공용 GE(GE_StatBonus) - 장비 시험과 같은 자산에 기댄다.
	UClass* StatEffect = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/Vowbound/GAS/Effects/GE_StatBonus.GE_StatBonus_C"));
	const FGameplayTag MaxHealth = FGameplayTag::RequestGameplayTag(TEXT("Data.Stat.MaxHealth"), false);
	const FGameplayTag NodeA = FGameplayTag::RequestGameplayTag(TEXT("Skill.Node.Body.Health1"), false);
	const FGameplayTag NodeB = FGameplayTag::RequestGameplayTag(TEXT("Skill.Node.Blade.Riposte"), false);
	const FGameplayTag Gone = FGameplayTag::RequestGameplayTag(TEXT("Skill.Node.Body.Shield"), false);
	if (!TestNotNull(TEXT("GE_StatBonus"), StatEffect) || !TestTrue(TEXT("growth tags registered"), MaxHealth.IsValid() && NodeA.IsValid() && NodeB.IsValid() && Gone.IsValid()))
	{
		return false;
	}
	UVBSkillTreeDefinition* Tree = NewObject<UVBSkillTreeDefinition>();
	FVBSkillNode A;
	A.NodeId = NodeA;
	A.Stats.Add(MaxHealth, 10.0f);
	FVBSkillNode B;
	B.NodeId = NodeB;
	B.Prerequisites = {NodeA};
	B.GrantedAbility = UGameplayAbility::StaticClass();
	Tree->Nodes = {A, B};
	UVBGrowthConfig* Config = NewObject<UVBGrowthConfig>();
	Config->XPToNextLevel = {100, 150};
	Config->PointsPerLevel = 1;
	Config->SkillTree = Tree;
	Config->StatBonusEffect = StatEffect;

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = GEngine ? UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init) : nullptr;
	if (!TestNotNull(TEXT("owned world"), World))
	{
		return false;
	}
	UPackage* Package = World->GetPackage();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVBPlayerState* State = World->SpawnActor<AVBPlayerState>();
	UVBProgressionComponent* Progression = State ? State->FindComponentByClass<UVBProgressionComponent>() : nullptr;
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(State);
	if (TestNotNull(TEXT("progression on the player state"), Progression) && TestNotNull(TEXT("ASC"), ASC))
	{
		SetGrowthConfig(Progression, Config);
		TestEqual(TEXT("starts at level 1"), Progression->GetLevel(), 1);
		Progression->GiveXP(100);
		TestEqual(TEXT("100 xp = level 2"), Progression->GetLevel(), 2);
		TestEqual(TEXT("one point"), Progression->GetPointsAvailable(), 1);
		TestTrue(TEXT("B needs A first"), Progression->Unlock(NodeB) == EVBSkillUnlockResult::MissingPrerequisite);
		TestTrue(TEXT("A opens"), Progression->Unlock(NodeA) == EVBSkillUnlockResult::Unlocked);
		TestEqual(TEXT("A's stat is one stat effect"), ASC->GetGameplayEffectCount(StatEffect, nullptr), 1);
		TestTrue(TEXT("B: no point left"), Progression->Unlock(NodeB) == EVBSkillUnlockResult::NotEnoughPoints);
		Progression->GiveXP(150);
		TestEqual(TEXT("250 xp = level 3 (max)"), Progression->GetLevel(), 3);
		TestTrue(TEXT("B opens"), Progression->Unlock(NodeB) == EVBSkillUnlockResult::Unlocked);
		TestNotNull(TEXT("B grants its ability"), ASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));
		TestEqual(TEXT("still one stat effect (B has no stats)"), ASC->GetGameplayEffectCount(StatEffect, nullptr), 1);

		FVBSaveContext Context;
		Context.World = World;
		Context.PlayerState = State;
		UVBSaveGame* Save = NewObject<UVBSaveGame>();
		Progression->WriteSave(*Save, Context);
		TestEqual(TEXT("save keeps the total xp"), Save->Growth.TotalXP, 250);
		TestEqual(TEXT("save keeps both nodes"), Save->Growth.Unlocked.Num(), 2);

		// 트리에서 지워진 칸(Gone)이 든 저장 - 버리고, 나머지는 그대로. 같은 저장을 두 번 읽어도 효과·능력이 겹치지 않는다.
		Save->Growth.Unlocked.Add(Gone);
		const int32 SerialBeforeLoad = Progression->GetSaveReadSerial();
		Progression->ReadSave(*Save, Context);
		Progression->ReadSave(*Save, Context);
		TestEqual(TEXT("each load marks the change as a load (HUD notices rebaseline on it)"), Progression->GetSaveReadSerial(), SerialBeforeLoad + 2);
		TestEqual(TEXT("load drops a node the tree no longer has"), Progression->GetUnlocked().Num(), 2);
		TestEqual(TEXT("load keeps one stat effect (no stacking)"), ASC->GetGameplayEffectCount(StatEffect, nullptr), 1);
		int32 AbilitySpecs = 0;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			AbilitySpecs += Spec.Ability && Spec.Ability->GetClass() == UGameplayAbility::StaticClass() ? 1 : 0;
		}
		TestEqual(TEXT("load grants B's ability once (no stacking)"), AbilitySpecs, 1);
	}
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	Package->SetDirtyFlag(false);
	return true;
}
#endif
