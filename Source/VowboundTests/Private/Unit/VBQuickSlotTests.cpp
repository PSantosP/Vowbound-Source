#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBQuickSlotRules.h"

// 퀵슬롯 판정의 단위 시험. 설계 .sw/quickslot-input 4.1절.
// 칸의 주인(UVBInventoryComponent)은 요청과 불러오기 모두 이 판정을 탄다 - 여기서 거절·옮기기·빼기를 덮는다.
// 아이템 태그는 네이티브 태그를 빌려 쓴다(VBInventoryTests 와 같다 - 판정은 태그 이름과 무관하다).

#if WITH_AUTOMATION_TESTS
namespace
{
	UVBItemDefinition* MakeQuickSlotItem(FGameplayTag Id, bool bUsable)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		Item->ItemId = Id;
		if (bUsable)
		{
			Item->Fragments.Add(FInstancedStruct::Make(FVBItemFragment_Usable()));
		}
		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuickSlotAssignTest, "Vowbound.Unit.QuickSlot.Assign",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuickSlotAssignTest::RunTest(const FString& Parameters)
{
	UVBItemDefinition* Gauze = MakeQuickSlotItem(VBGameplayTags::Melee_Shape_Weapon, true);
	UVBItemDefinition* Tonic = MakeQuickSlotItem(VBGameplayTags::Melee_Shape_HandL, true);
	UVBItemDefinition* Key = MakeQuickSlotItem(VBGameplayTags::Melee_Shape_HandR, false);
	TArray<TObjectPtr<const UVBItemDefinition>> Slots;
	Slots.Init(nullptr, 4);

	// 거절은 칸을 하나도 안 바꾼다.
	TestTrue(TEXT("item without a usable fragment is refused"), VBQuickSlot::Assign(Slots, 0, Key) == EVBQuickSlotAssignResult::NotUsable);
	TestTrue(TEXT("slot below range is refused"), VBQuickSlot::Assign(Slots, -1, Gauze) == EVBQuickSlotAssignResult::OutOfRange);
	TestTrue(TEXT("slot past range is refused"), VBQuickSlot::Assign(Slots, 4, Gauze) == EVBQuickSlotAssignResult::OutOfRange);
	TestFalse(TEXT("refusals left every slot empty"), Slots.ContainsByPredicate([](const TObjectPtr<const UVBItemDefinition>& Slot) { return Slot != nullptr; }));

	TestTrue(TEXT("gauze -> slot 1"), VBQuickSlot::Assign(Slots, 0, Gauze) == EVBQuickSlotAssignResult::Changed);
	TestTrue(TEXT("same place again is not a change"), VBQuickSlot::Assign(Slots, 0, Gauze) == EVBQuickSlotAssignResult::Unchanged);
	TestTrue(TEXT("tonic -> slot 3"), VBQuickSlot::Assign(Slots, 2, Tonic) == EVBQuickSlotAssignResult::Changed);

	// 같은 아이템을 다른 칸에 넣으면 옛 칸이 빈다 - 한 아이템은 한 칸에만.
	TestTrue(TEXT("gauze -> slot 2 moves it"), VBQuickSlot::Assign(Slots, 1, Gauze) == EVBQuickSlotAssignResult::Changed);
	TestTrue(TEXT("slot 1 emptied by the move"), Slots[0] == nullptr);
	TestTrue(TEXT("slot 2 holds gauze"), Slots[1] == Gauze);
	TestTrue(TEXT("unrelated slot untouched"), Slots[2] == Tonic);

	// 다른 아이템을 덮어쓰면 덮인 아이템은 칸에서 빠진다(어디로도 옮겨 가지 않는다).
	TestTrue(TEXT("gauze over tonic"), VBQuickSlot::Assign(Slots, 2, Gauze) == EVBQuickSlotAssignResult::Changed);
	TestTrue(TEXT("tonic is out of every slot"), !Slots.Contains(Tonic));
	TestTrue(TEXT("gauze only in slot 3"), Slots[1] == nullptr && Slots[2] == Gauze);

	// 빼기 = 빈 아이템. 빈 칸 빼기는 바뀐 것이 없다.
	TestTrue(TEXT("clear slot 3"), VBQuickSlot::Assign(Slots, 2, nullptr) == EVBQuickSlotAssignResult::Changed);
	TestTrue(TEXT("clearing an empty slot is not a change"), VBQuickSlot::Assign(Slots, 2, nullptr) == EVBQuickSlotAssignResult::Unchanged);
	TestTrue(TEXT("nullptr can always be assigned"), VBQuickSlot::CanAssign(nullptr));
	return true;
}
#endif
