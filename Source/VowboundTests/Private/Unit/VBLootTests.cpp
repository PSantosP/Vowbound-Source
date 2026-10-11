#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Loot/VBLootContainerSubsystem.h"
#include "Inventory/Loot/VBLootRules.h"
#include "Inventory/Loot/VBLootTable.h"
#include "Math/RandomStream.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "SaveSystem/VBSaveGame.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 보상표의 단위 시험. 설계 .sw/narrative 14.3·14.7절.
// 굴리기는 씨앗을 받는 순수 함수라 확률 0·1, 개수 범위의 양끝, 합치기를 결정적으로 잰다. 넣는 쪽(효과 -> 인벤토리)은 기존 AddItem 길이고 PIE 가 본다.
// 아이템 태그(Item.*)는 ini 내용이라 네이티브 태그를 빌려 쓴다 - 연산은 태그 이름과 무관하다.

#if WITH_AUTOMATION_TESTS
namespace
{
	UVBItemDefinition* MakeLootItem(FGameplayTag Id)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		Item->ItemId = Id;
		return Item;
	}

	FVBLootEntry MakeEntry(const UVBItemDefinition* Item, int32 Min, int32 Max, float Chance)
	{
		FVBLootEntry Entry;
		Entry.Item = Item;
		Entry.MinCount = Min;
		Entry.MaxCount = Max;
		Entry.Chance = Chance;
		return Entry;
	}

	int32 CountOf(const TArray<FVBLootDrop>& Drops, const UVBItemDefinition* Item)
	{
		const FVBLootDrop* Drop = Drops.FindByPredicate([Item](const FVBLootDrop& D) { return D.Item == Item; });
		return Drop ? Drop->Count : 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLootRollTest, "Vowbound.Unit.Loot.Roll",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLootRollTest::RunTest(const FString& Parameters)
{
	const FVBRuleContext Context;
	UVBItemDefinition* Gauze = MakeLootItem(VBGameplayTags::Melee_Shape_Weapon);
	UVBItemDefinition* Gold = MakeLootItem(VBGameplayTags::Melee_Shape_HandL);
	FRandomStream Stream(1234);

	// 확률 1 은 늘, 고정 개수는 그 수 그대로, 범위는 양끝을 포함한다.
	{
		UVBLootTable* Table = NewObject<UVBLootTable>();
		Table->Entries = {MakeEntry(Gauze, 2, 2, 1.0f), MakeEntry(Gold, 1, 3, 1.0f)};
		bool bAlwaysGauze = true;
		bool bGoldInRange = true;
		int32 GoldLow = MAX_int32;
		int32 GoldHigh = 0;
		for (int32 Index = 0; Index < 300; ++Index)
		{
			const TArray<FVBLootDrop> Drops = VBLoot::Roll(*Table, Context, Stream);
			bAlwaysGauze &= CountOf(Drops, Gauze) == 2;
			const int32 GoldCount = CountOf(Drops, Gold);
			bGoldInRange &= GoldCount >= 1 && GoldCount <= 3;
			GoldLow = FMath::Min(GoldLow, GoldCount);
			GoldHigh = FMath::Max(GoldHigh, GoldCount);
		}
		TestTrue(TEXT("chance 1 with fixed count always gives exactly that count"), bAlwaysGauze);
		TestTrue(TEXT("count stays inside [Min, Max]"), bGoldInRange);
		TestEqual(TEXT("Min is reachable"), GoldLow, 1);
		TestEqual(TEXT("Max is reachable"), GoldHigh, 3);
	}

	// 확률 0 은 한 번도 안 나온다(FRand 는 [0, 1)). 0.25 는 대략 그만큼 - 비교 방향(< Chance)을 거꾸로 쓰면 750 근처가 나온다.
	{
		UVBLootTable* Never = NewObject<UVBLootTable>();
		Never->Entries = {MakeEntry(Gauze, 1, 1, 0.0f)};
		UVBLootTable* Quarter = NewObject<UVBLootTable>();
		Quarter->Entries = {MakeEntry(Gauze, 1, 1, 0.25f)};
		int32 NeverHits = 0;
		int32 QuarterHits = 0;
		for (int32 Index = 0; Index < 2000; ++Index)
		{
			NeverHits += VBLoot::Roll(*Never, Context, Stream).Num();
			QuarterHits += VBLoot::Roll(*Quarter, Context, Stream).Num();
		}
		TestEqual(TEXT("chance 0 never drops"), NeverHits, 0);
		TestTrue(FString::Printf(TEXT("chance 0.25 drops about a quarter of 2000 (got %d)"), QuarterHits), QuarterHits >= 400 && QuarterHits <= 600);
	}

	// 같은 아이템 두 줄은 한 줄로 합친다(획득 알림 한 줄). 빈 아이템 줄은 건너뛴다.
	{
		UVBLootTable* Table = NewObject<UVBLootTable>();
		Table->Entries = {MakeEntry(Gauze, 1, 1, 1.0f), MakeEntry(nullptr, 1, 1, 1.0f), MakeEntry(Gauze, 2, 2, 1.0f)};
		const TArray<FVBLootDrop> Drops = VBLoot::Roll(*Table, Context, Stream);
		TestEqual(TEXT("same item merges into one drop"), Drops.Num(), 1);
		TestEqual(TEXT("merged count is the sum"), CountOf(Drops, Gauze), 3);
	}

	// 조건이 거짓인 줄은 빠지고, 나머지 줄은 그대로 굴러간다.
	{
		UVBLootTable* Table = NewObject<UVBLootTable>();
		FVBLootEntry Locked = MakeEntry(Gauze, 1, 1, 1.0f);
		// 사실 저장소가 없는 문맥에서 사실 조건은 거짓이다(VBRules 규약 - 모르면 거짓).
		FVBCondition_Fact Fact;
		Fact.Fact = VBGameplayTags::Melee_Window;
		Locked.Conditions.Add(FInstancedStruct::Make(Fact));
		Table->Entries = {Locked, MakeEntry(Gold, 1, 1, 1.0f)};
		AddExpectedMessagePlain(TEXT("문맥에 사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		const TArray<FVBLootDrop> Drops = VBLoot::Roll(*Table, Context, Stream);
		TestEqual(TEXT("entry with a false condition is skipped"), CountOf(Drops, Gauze), 0);
		TestEqual(TEXT("other entries still roll"), CountOf(Drops, Gold), 1);
	}
	return true;
}

// 서랍장 남은 몫(14.5 W1): 넣기 함수가 상한을 흉내 낸다. 남은 몫을 잃지 않는 것이 요점이라
//   대조로 «넣기가 0 을 주면 줄이 그대로 남는다» 와 «다 들어가면 줄이 지워진다» 를 같이 본다 - 줄을 늘 지우거나 늘 남기는 구현은 둘 중 하나에서 깨진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLootGiveStacksTest, "Vowbound.Unit.Loot.GiveStacks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLootGiveStacksTest::RunTest(const FString& Parameters)
{
	const FGameplayTag GauzeId = VBGameplayTags::Melee_Shape_Weapon;
	const FGameplayTag GoldId = VBGameplayTags::Melee_Shape_HandL;
	const FGameplayTag UnknownId = VBGameplayTags::Melee_Window;
	auto Row = [](FGameplayTag Id, int32 Count) { FVBSavedItem R; R.ItemId = Id; R.Count = Count; return R; };
	auto CountIn = [](const TArray<FVBSavedItem>& Rows, FGameplayTag Id)
	{
		const FVBSavedItem* Found = Rows.FindByPredicate([Id](const FVBSavedItem& R) { return R.ItemId == Id; });
		return Found ? Found->Count : 0;
	};

	// 첫 열기: 거즈는 자리가 1 칸뿐(상한), 돈은 다 들어가고, 모르는 아이템은 하나도 못 넣는다.
	TArray<FVBSavedItem> Remaining = {Row(GauzeId, 3), Row(GoldId, 15), Row(UnknownId, 2), Row(GoldId, 0)};
	int32 GauzeRoom = 1;
	auto Give = [&](const FVBSavedItem& R) -> int32
	{
		if (R.ItemId == GauzeId)
		{
			const int32 Put = FMath::Min(R.Count, GauzeRoom);
			GauzeRoom -= Put;
			return Put;
		}
		return R.ItemId == GoldId ? R.Count : 0;
	};
	TestEqual(TEXT("first open gives what fits"), VBLoot::GiveStacks(Remaining, Give), 1 + 15);
	TestEqual(TEXT("capped item keeps the rest"), CountIn(Remaining, GauzeId), 2);
	TestEqual(TEXT("fully given row is removed"), CountIn(Remaining, GoldId), 0);
	TestEqual(TEXT("unresolved row is kept whole"), CountIn(Remaining, UnknownId), 2);
	TestEqual(TEXT("rows left = capped + unresolved (zero row dropped)"), Remaining.Num(), 2);

	// 다시 열기(인벤토리를 비웠다): 나머지 거즈가 들어간다. 모르는 줄은 여전히 남는다.
	GauzeRoom = 99;
	TestEqual(TEXT("second open gives the rest"), VBLoot::GiveStacks(Remaining, Give), 2);
	TestEqual(TEXT("gauze row gone after the rest"), CountIn(Remaining, GauzeId), 0);
	TestEqual(TEXT("unresolved row still kept"), CountIn(Remaining, UnknownId), 2);

	// 자리를 다투는 두 줄(같은 상한 2 칸을 나눠 쓴다)은 앞 줄이 먼저 들어간다 - 표 순서가 우선순위다. 뒤에서부터 넣는 구현은 여기서 깨진다.
	TArray<FVBSavedItem> Shared = {Row(GauzeId, 2), Row(GoldId, 2)};
	int32 SharedRoom = 2;
	VBLoot::GiveStacks(Shared, [&SharedRoom](const FVBSavedItem& R)
	{
		const int32 Put = FMath::Min(R.Count, SharedRoom);
		SharedRoom -= Put;
		return Put;
	});
	TestEqual(TEXT("first row in table order wins shared room"), CountIn(Shared, GauzeId), 0);
	TestEqual(TEXT("later row keeps its count"), CountIn(Shared, GoldId), 2);

	// 넣기가 줄보다 많이 넣었다고 해도 그 줄의 수까지만 뺀다(음수 줄이 남지 않는다).
	TArray<FVBSavedItem> Over = {Row(GoldId, 4)};
	TestEqual(TEXT("over-report is clamped to the row"), VBLoot::GiveStacks(Over, [](const FVBSavedItem&) { return 10; }), 4);
	TestTrue(TEXT("over-report empties the row"), Over.IsEmpty());
	return true;
}

// 서랍장 남은 몫 저장 왕복: «기록 없음(안 굴림)» 과 «빈 기록(다 털림)» 이 갈린 채 돌아와야 한다 - 잠금의 근거가 그 차이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLootContainerSaveTest, "Vowbound.Unit.Loot.ContainerSave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLootContainerSaveTest::RunTest(const FString& Parameters)
{
	const FName Emptied(TEXT("Cupboard_Emptied"));
	const FName Partial(TEXT("Cupboard_Partial"));
	const FName Untouched(TEXT("Cupboard_Untouched"));
	FVBSavedItem Gauze;
	Gauze.ItemId = VBGameplayTags::Melee_Shape_Weapon;
	Gauze.Count = 2;

	UVBLootContainerSubsystem* State = NewObject<UVBLootContainerSubsystem>();
	State->SetRemaining(Emptied, {});
	State->SetRemaining(Partial, {Gauze});
	AddExpectedMessagePlain(TEXT("키가 비었다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	State->SetRemaining(NAME_None, {Gauze});

	// 세이브에는 이미 다른 맵의 서랍장 기록이 있다(이어하기 뒤 맵을 넘어와 저장하는 경우) - 쓰기가 지우면 안 된다.
	const FName OtherMap(TEXT("Cupboard_OtherMap"));
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	FVBSavedLootContainer& Foreign = Save->LootContainers.Containers.AddDefaulted_GetRef();
	Foreign.Key = OtherMap;
	const FVBSaveContext Context;
	State->WriteSave(*Save, Context);
	TestEqual(TEXT("empty record is written too, other map's record kept"), Save->LootContainers.Containers.Num(), 3);
	State->WriteSave(*Save, Context);
	TestEqual(TEXT("writing twice updates in place, no duplicate rows"), Save->LootContainers.Containers.Num(), 3);

	// 읽을 수 없는 줄(0 개)과 키가 빈 기록을 섞는다 - 그것만 버리고 소리 낸다.
	FVBSavedItem Broken = Gauze;
	Broken.Count = 0;
	FVBSavedLootContainer* PartialRow = Save->LootContainers.Containers.FindByPredicate([&Partial](const FVBSavedLootContainer& R) { return R.Key == Partial; });
	if (!TestNotNull(TEXT("partial row written"), PartialRow))
	{
		return false;
	}
	PartialRow->Remaining.Add(Broken);
	Save->LootContainers.Containers.AddDefaulted_GetRef().Remaining.Add(Gauze);
	UVBLootContainerSubsystem* Loaded = NewObject<UVBLootContainerSubsystem>();
	Loaded->SetRemaining(Untouched, {Gauze});
	AddExpectedMessagePlain(TEXT("읽을 수 없는 줄"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("키가 빈 기록"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	bool bLoadedSignal = false;
	Loaded->OnContentsLoaded.AddLambda([&bLoadedSignal]() { bLoadedSignal = true; });
	Loaded->ReadSave(*Save, Context);

	const TArray<FVBSavedItem>* EmptiedRows = Loaded->Find(Emptied);
	const TArray<FVBSavedItem>* PartialRows = Loaded->Find(Partial);
	TestTrue(TEXT("emptied comes back as a record"), EmptiedRows != nullptr);
	TestTrue(TEXT("emptied record is empty (= spent)"), EmptiedRows && EmptiedRows->IsEmpty());
	// Melee_* 태그는 아이템이 아니다 - 불러오기가 아이템을 되찾으려 했다면 이 줄을 잃었을 것이다(되찾기는 줄 때 한다).
	TestTrue(TEXT("partial keeps its unresolvable row, broken row dropped"), PartialRows && PartialRows->Num() == 1 && (*PartialRows)[0].Count == 2);
	TestTrue(TEXT("other map's record round-trips"), Loaded->Find(OtherMap) != nullptr);
	TestTrue(TEXT("loading replaces - a record absent from the save is gone (= not rolled)"), Loaded->Find(Untouched) == nullptr);
	TestTrue(TEXT("load signal fires so containers recheck the lock"), bLoadedSignal);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLootValidateTest, "Vowbound.Unit.Loot.Validate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBLootValidateTest::RunTest(const FString& Parameters)
{
	UVBItemDefinition* Gauze = MakeLootItem(VBGameplayTags::Melee_Shape_Weapon);
	UVBItemDefinition* NoId = MakeLootItem(FGameplayTag());

	UVBLootTable* Good = NewObject<UVBLootTable>();
	Good->Entries = {MakeEntry(Gauze, 1, 2, 0.5f)};
	{
		FDataValidationContext Context;
		TestEqual(TEXT("valid table"), Good->IsDataValid(Context), EDataValidationResult::Valid);
	}

	// 대조: 같은 표에 결함을 하나씩 넣으면 각각 무효가 된다.
	struct FCase { const TCHAR* Name; TFunction<void(UVBLootTable&)> Break; };
	const TArray<FCase> Cases = {
		{TEXT("empty item"), [](UVBLootTable& T) { T.Entries[0].Item = nullptr; }},
		{TEXT("item without ItemId"), [NoId](UVBLootTable& T) { T.Entries[0].Item = NoId; }},
		{TEXT("min below 1"), [](UVBLootTable& T) { T.Entries[0].MinCount = 0; }},
		{TEXT("min above max"), [](UVBLootTable& T) { T.Entries[0].MinCount = 3; }},
		{TEXT("chance 0"), [](UVBLootTable& T) { T.Entries[0].Chance = 0.0f; }},
		{TEXT("chance above 1"), [](UVBLootTable& T) { T.Entries[0].Chance = 1.5f; }},
		{TEXT("empty condition slot"), [](UVBLootTable& T) { T.Entries[0].Conditions.AddDefaulted(); }},
	};
	for (const FCase& Case : Cases)
	{
		UVBLootTable* Broken = DuplicateObject(Good, GetTransientPackage());
		Case.Break(*Broken);
		FDataValidationContext Context;
		TestEqual(Case.Name, Broken->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	return true;
}
#endif
#endif
