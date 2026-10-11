#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBAN_ItemUseMarker.h"
#include "Animation/VBAN_SendAttackTraceEvent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBInventoryView.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "SaveSystem/VBSaveGame.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 아이템 단계의 단위 시험. 설계 .sw/narrative 10절.
// 주인 액터 없는 인벤토리 = 복제·권한이 없는 자리라 서버 경로가 그대로 돈다(RPC 도 이 자리에서 바로 실행된다).
// 아이템 태그(Item.*)는 아직 내용이 없어 네이티브 태그를 빌려 쓴다 - 연산은 태그 이름과 무관하다.

#if WITH_AUTOMATION_TESTS
namespace
{
	UVBItemDefinition* MakeItem(FGameplayTag Id, int32 MaxCount)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		Item->ItemId = Id;
		Item->MaxCount = MaxCount;
		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventoryAddRemoveTest, "Vowbound.Unit.Inventory.AddRemove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventoryAddRemoveTest::RunTest(const FString& Parameters)
{
	UVBItemDefinition* Gauze = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 5);
	UVBItemDefinition* Gold = MakeItem(VBGameplayTags::Melee_Shape_HandL, 0);
	UVBInventoryComponent* Inventory = NewObject<UVBInventoryComponent>();

	int32 Changed = 0;
	Inventory->OnInventoryChanged.AddLambda([&Changed]() { ++Changed; });
	int32 Acquired = 0;
	Inventory->OnItemAcquired.AddLambda([&Acquired](const UVBItemDefinition*, int32 N) { Acquired += N; });

	TestEqual(TEXT("add returns what went in"), Inventory->AddItem(Gauze, 3), 3);
	TestEqual(TEXT("add clamps at MaxCount"), Inventory->AddItem(Gauze, 4), 2);
	TestEqual(TEXT("count is at the cap"), Inventory->GetCount(Gauze), 5);
	TestEqual(TEXT("full item accepts nothing"), Inventory->AddItem(Gauze, 1), 0);
	TestEqual(TEXT("one line per definition"), Inventory->GetEntries().Num(), 1);
	TestEqual(TEXT("MaxCount 0 is unlimited"), Inventory->AddItem(Gold, 100000), 100000);
	TestEqual(TEXT("acquired notice carries what went in"), Acquired, 3 + 2 + 100000);
	TestEqual(TEXT("no change notice for a full add"), Changed, 3);

	TestFalse(TEXT("removing more than held removes nothing"), Inventory->RemoveItem(Gauze, 6));
	TestEqual(TEXT("count untouched after refused remove"), Inventory->GetCount(Gauze), 5);
	TestTrue(TEXT("remove within count"), Inventory->RemoveItem(Gauze, 5));
	TestEqual(TEXT("reaching 0 drops the line (one meaning for 0)"), Inventory->GetEntries().Num(), 1);
	TestEqual(TEXT("removed item reads 0"), Inventory->GetCount(Gauze), 0);
	TestFalse(TEXT("null item is refused"), Inventory->RemoveItem(nullptr, 1));
	TestEqual(TEXT("null item adds nothing"), Inventory->AddItem(nullptr, 1), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventoryUseTest, "Vowbound.Unit.Inventory.Use",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventoryUseTest::RunTest(const FString& Parameters)
{
	UVBItemDefinition* Potion = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 10);
	Potion->Fragments.Add(FInstancedStruct::Make(FVBItemFragment_Usable()));
	UVBItemDefinition* Key = MakeItem(VBGameplayTags::Melee_Shape_HandR, 1);
	UVBItemDefinition* Locked = MakeItem(VBGameplayTags::Melee_Shape_FootL, 10);
	FVBItemFragment_Usable NeedsFact;
	FVBCondition_Fact Fact;
	Fact.Fact = VBGameplayTags::Melee_Window;
	NeedsFact.Conditions.Add(FInstancedStruct::Make(Fact));
	Locked->Fragments.Add(FInstancedStruct::Make(NeedsFact));

	UVBInventoryComponent* Inventory = NewObject<UVBInventoryComponent>();
	Inventory->AddItem(Potion, 2);
	Inventory->AddItem(Key, 1);
	Inventory->AddItem(Locked, 1);

	TestTrue(TEXT("usable item with no conditions can be used"), Inventory->CanUseItem(Potion));
	Inventory->RequestUseItem(Potion);
	TestEqual(TEXT("using consumes one"), Inventory->GetCount(Potion), 1);

	TestFalse(TEXT("item without a usable fragment cannot be used"), Inventory->CanUseItem(Key));
	AddExpectedMessagePlain(TEXT("쓸 수 있는 아이템이 아니다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Inventory->RequestUseItem(Key);
	TestEqual(TEXT("refused use keeps the item"), Inventory->GetCount(Key), 1);

	// 조건을 평가할 사실 저장소가 없는 자리 - 조건은 거짓, 아이템은 남는다(알 수 없는 것을 참으로 흘리지 않는다).
	AddExpectedMessagePlain(TEXT("문맥에 사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("unmet condition blocks use"), Inventory->CanUseItem(Locked));
	Inventory->RequestUseItem(Locked);
	TestEqual(TEXT("blocked use keeps the item"), Inventory->GetCount(Locked), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventorySaveTest, "Vowbound.Unit.Inventory.SaveWrite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventorySaveTest::RunTest(const FString& Parameters)
{
	UVBItemDefinition* Gauze = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 10);
	UVBInventoryComponent* Inventory = NewObject<UVBInventoryComponent>();
	Inventory->AddItem(Gauze, 4);

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	const FVBSaveContext Context;
	Inventory->WriteSave(*Save, Context);
	if (!TestEqual(TEXT("one saved line"), Save->Inventory.Items.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("saved by ItemId tag, not asset path"), Save->Inventory.Items[0].ItemId == VBGameplayTags::Melee_Shape_Weapon.GetTag());
	TestEqual(TEXT("saved count"), Save->Inventory.Items[0].Count, 4);

	// 에셋 매니저에 없는 아이템(여기서 만든 임시 정의)은 되찾지 못한다 - 조용히 빈손이 되지 않고 경고한다.
	UVBInventoryComponent* Loaded = NewObject<UVBInventoryComponent>();
	AddExpectedMessagePlain(TEXT("되찾지 못했다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Loaded->ReadSave(*Save, Context);
	TestEqual(TEXT("unresolvable item is not invented"), Loaded->GetEntries().Num(), 0);

	// 되찾지 못한 줄은 다음 저장에 그대로 다시 실린다 - 버리면 그 저장이 가진 것을 영영 지운다.
	UVBSaveGame* Resaved = NewObject<UVBSaveGame>();
	Loaded->WriteSave(*Resaved, Context);
	TestTrue(TEXT("unresolved line is written back as it was"), Resaved->Inventory.Items.Num() == 1
		&& Resaved->Inventory.Items[0].ItemId == VBGameplayTags::Melee_Shape_Weapon.GetTag() && Resaved->Inventory.Items[0].Count == 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventoryResolveSavedTest, "Vowbound.Unit.Inventory.ResolveSaved",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventoryResolveSavedTest::RunTest(const FString& Parameters)
{
	// 불러오기 판정(설계 2차 검증 2). 되찾기를 시험이 쥐어 에셋 매니저 없이 상한·합치기·지키기를 본다.
	UVBItemDefinition* Gauze = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 5);
	const FGameplayTag Unknown = VBGameplayTags::Melee_Shape_HandL;
	const auto Resolve = [Gauze](const FGameplayTag& Id) -> const UVBItemDefinition* { return Id == Gauze->ItemId ? Gauze : nullptr; };
	const auto Row = [](FGameplayTag Id, int32 Count) { FVBSavedItem Saved; Saved.ItemId = Id; Saved.Count = Count; return Saved; };

	// 거즈 4 + 거즈 3(같은 아이템 둘 - 상한 5) + 모르는 아이템 2 + 0개 줄 + 상한이 이미 찬 뒤의 거즈 1.
	const TArray<FVBSavedItem> Saved = {Row(Gauze->ItemId, 4), Row(Gauze->ItemId, 3), Row(Unknown, 2), Row(Gauze->ItemId, 0), Row(Gauze->ItemId, 1)};
	TArray<FVBInventoryEntry> Entries;
	TArray<FVBSavedItem> Kept;
	// 기대 경고는 시험 전체의 합으로 한 번씩 적는다 - 같은 글을 두 번 적으면 하나만 남는다(FAutomationTestBase::ExpectedMessages 가 글로 견주는 TSet).
	//   상한 = 첫 판 2 + 둘째 판 2, 되찾지 못함 = 판마다 1, 버림 = 첫 판 1.
	AddExpectedMessagePlain(TEXT("상한"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 4);
	AddExpectedMessagePlain(TEXT("되찾지 못했다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
	AddExpectedMessagePlain(TEXT("버린다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	UVBInventoryComponent::ResolveSavedItems(Saved, Resolve, Entries, Kept);

	TestTrue(TEXT("one line per definition, filled to the cap"), Entries.Num() == 1 && Entries[0].Definition == Gauze && Entries[0].Count == 5);
	// 넘친 몫(2 와 1)과 모르는 줄(2)은 지킨다 - 상한을 낮춘 패치가 가진 것을 지우지 않는다. 0개 줄은 지킬 것이 없다.
	int32 KeptGauze = 0;
	int32 KeptUnknown = 0;
	for (const FVBSavedItem& K : Kept)
	{
		KeptGauze += K.ItemId == Gauze->ItemId ? K.Count : 0;
		KeptUnknown += K.ItemId == Unknown ? K.Count : 0;
	}
	TestEqual(TEXT("over-cap share is kept"), KeptGauze, 3);
	TestEqual(TEXT("unresolved line is kept"), KeptUnknown, 2);
	TestEqual(TEXT("nothing else is kept"), Kept.Num(), 3);
	TestEqual(TEXT("held + kept = everything that was saved (no loss)"), Entries[0].Count + KeptGauze, 4 + 3 + 1);

	// 지킨 줄을 다음 불러오기에 다시 넣으면 같은 결과다 - 쌓이지도 사라지지도 않는다(상한이 그대로일 때).
	TArray<FVBSavedItem> Second = {Row(Gauze->ItemId, Entries[0].Count)};
	Second.Append(Kept);
	TArray<FVBInventoryEntry> Entries2;
	TArray<FVBSavedItem> Kept2;
	UVBInventoryComponent::ResolveSavedItems(Second, Resolve, Entries2, Kept2);
	TestTrue(TEXT("round trip: still 5 held"), Entries2.Num() == 1 && Entries2[0].Count == 5);
	TestEqual(TEXT("round trip: still 3 lines kept"), Kept2.Num(), 3);

	// 상한을 올리면 지킨 몫이 돌아온다.
	Gauze->MaxCount = 0;
	TArray<FVBInventoryEntry> Entries3;
	TArray<FVBSavedItem> Kept3;
	UVBInventoryComponent::ResolveSavedItems(Second, Resolve, Entries3, Kept3);
	TestTrue(TEXT("cap lifted: all 8 come back"), Entries3.Num() == 1 && Entries3[0].Count == 8);
	TestEqual(TEXT("cap lifted: only the unknown line is kept"), Kept3.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventoryUseMotionTest, "Vowbound.Unit.Inventory.UseMotion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventoryUseMotionTest::RunTest(const FString& Parameters)
{
	// 몸 동작이 있는 아이템(설계 .sw/quickslot-input 6.1절). 확정은 몽타주의 표식 때 사용 GA 가 ConfirmUse 로 한다 - 요청만으로는 소모되지 않는다.
	UVBItemDefinition* Gauze = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 10);
	FVBItemFragment_Usable WithMotion;
	WithMotion.UseMontage = NewObject<UAnimMontage>();
	Gauze->Fragments.Add(FInstancedStruct::Make(WithMotion));
	UVBItemDefinition* Instant = MakeItem(VBGameplayTags::Melee_Shape_HandL, 10);
	Instant->Fragments.Add(FInstancedStruct::Make(FVBItemFragment_Usable()));

	UVBInventoryComponent* Inventory = NewObject<UVBInventoryComponent>();
	Inventory->AddItem(Gauze, 2);
	Inventory->AddItem(Instant, 2);

	// 주인 없는 인벤토리에는 몸도 ASC 도 없다 - 동작을 시작할 수 없으면 아무것도 소모하지 않는다(시작 못 한 사용을 «쓴 것» 으로 흘리지 않는다).
	Inventory->RequestUseItem(Gauze);
	TestEqual(TEXT("a motion item is not consumed by the request alone"), Inventory->GetCount(Gauze), 2);
	// 대조군: 같은 자리·같은 길에서 바로 쓰는 아이템은 소모된다 - 위의 0 소모가 «사용 길이 통째로 막혀서» 가 아님을 보인다.
	Inventory->RequestUseItem(Instant);
	TestEqual(TEXT("control: an instant item is consumed on the same path"), Inventory->GetCount(Instant), 1);

	// 확정 = 사용 GA 가 표식 때 부르는 그 함수. 부를 때마다 하나씩, 0 이면 거절.
	TestTrue(TEXT("confirm consumes the motion item"), Inventory->ConfirmUse(Gauze));
	TestEqual(TEXT("one consumed per confirm"), Inventory->GetCount(Gauze), 1);
	TestTrue(TEXT("second confirm consumes the last one"), Inventory->ConfirmUse(Gauze));
	AddExpectedMessagePlain(TEXT("가진 것이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("confirm at 0 is refused"), Inventory->ConfirmUse(Gauze));
	TestEqual(TEXT("refused confirm leaves 0, not negative"), Inventory->GetCount(Gauze), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBItemUseMarkerCountTest, "Vowbound.Unit.Inventory.UseMarkerCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBItemUseMarkerCountTest::RunTest(const FString& Parameters)
{
	// 표식은 몽타주마다 정확히 하나(설계 6.1절). 세는 것은 UVBAN_ItemUseMarker::CountIn 하나다.
	const auto AddNotify = [](UAnimSequenceBase* Animation, UAnimNotify* Notify)
	{
		Animation->Notifies.AddDefaulted_GetRef().Notify = Notify;
	};
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	TestEqual(TEXT("no marker"), UVBAN_ItemUseMarker::CountIn(*Montage), 0);

	// 다른 노티·비어 있는 노티 줄(이름만 있는 스켈레톤 노티)은 세지 않는다.
	AddNotify(Montage, NewObject<UVBAN_SendAttackTraceEvent>(Montage));
	AddNotify(Montage, nullptr);
	TestEqual(TEXT("other notifies are not markers"), UVBAN_ItemUseMarker::CountIn(*Montage), 0);

	AddNotify(Montage, NewObject<UVBAN_ItemUseMarker>(Montage));
	TestEqual(TEXT("one marker on the montage"), UVBAN_ItemUseMarker::CountIn(*Montage), 1);

	// 조각의 애니에 놓인 표식도 몽타주 재생 중에 발화하므로 센다.
	UAnimComposite* Clip = NewObject<UAnimComposite>();
	AddNotify(Clip, NewObject<UVBAN_ItemUseMarker>(Clip));
	FSlotAnimationTrack& Slot = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0] : Montage->AddSlot(TEXT("DefaultSlot"));
	Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef().SetAnimReference(Clip);
	TestEqual(TEXT("a marker on a segment's animation counts too"), UVBAN_ItemUseMarker::CountIn(*Montage), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBItemUseMontageProblemsTest, "Vowbound.Unit.Inventory.UseMontageProblems",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBItemUseMontageProblemsTest::RunTest(const FString& Parameters)
{
	// 사용 몽타주의 약속(설계 6.1절 + 독립 검토 10-05): 표식 하나·몽타주 위·자동 블렌드아웃 시작 앞, 그리고 끝나는 몽타주.
	//   길이 2초, 기본 BlendOut 0.25초 -> 블렌드아웃은 1.75초에 시작한다(UAnimMontage 생성자의 BlendOut 시간).
	const auto MakeUseMontage = [](float MarkerTime)
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>();
		Montage->SetCompositeLength(2.0f);
		FAnimNotifyEvent& Marker = Montage->Notifies.AddDefaulted_GetRef();
		Marker.Notify = NewObject<UVBAN_ItemUseMarker>(Montage);
		Marker.SetTime(MarkerTime);
		return Montage;
	};
	const auto ProblemCount = [](const UAnimMontage* Montage) { return UVBAN_ItemUseMarker::FindUseMontageProblems(*Montage).Num(); };

	UAnimMontage* Good = MakeUseMontage(1.0f);
	TestEqual(TEXT("one marker on the montage before blend-out is valid"), ProblemCount(Good), 0);
	// 블렌드아웃 안의 표식 = 사용 GA 가 이미 끝난 뒤라 확정이 안 온다.
	TestEqual(TEXT("a marker inside the blend-out window is a problem"), ProblemCount(MakeUseMontage(1.9f)), 1);
	TestEqual(TEXT("a marker exactly at blend-out start is a problem"), ProblemCount(MakeUseMontage(1.75f)), 1);
	// 몽타주 배속 2 면 0.25초(실시간) 블렌드아웃이 몽타주 위치로 0.5 - 1.5 에서 시작한다. 배속 1 기준(1.75)으로는 통과할 1.6 이 문제가 된다.
	UAnimMontage* Fast = MakeUseMontage(1.6f);
	Fast->RateScale = 2.0f;
	TestEqual(TEXT("rate scale moves blend-out start earlier in montage time"), ProblemCount(Fast), 1);
	UAnimMontage* FastOk = MakeUseMontage(1.4f);
	FastOk->RateScale = 2.0f;
	TestEqual(TEXT("control: a marker before the scaled blend-out start is fine"), ProblemCount(FastOk), 0);

	UAnimMontage* TwoMarkers = MakeUseMontage(1.0f);
	TwoMarkers->Notifies.AddDefaulted_GetRef().Notify = NewObject<UVBAN_ItemUseMarker>(TwoMarkers);
	TestTrue(TEXT("two markers are a problem"), ProblemCount(TwoMarkers) >= 1);
	UAnimMontage* NoMarker = NewObject<UAnimMontage>();
	NoMarker->SetCompositeLength(2.0f);
	TestEqual(TEXT("no marker is a problem"), ProblemCount(NoMarker), 1);

	// 조각 애니에 놓인 표식: 개수는 하나라도 몽타주 위가 아니라 문제다.
	UAnimMontage* OnSegment = NewObject<UAnimMontage>();
	OnSegment->SetCompositeLength(2.0f);
	UAnimComposite* Clip = NewObject<UAnimComposite>();
	Clip->Notifies.AddDefaulted_GetRef().Notify = NewObject<UVBAN_ItemUseMarker>(Clip);
	FSlotAnimationTrack& Slot = OnSegment->SlotAnimTracks.Num() > 0 ? OnSegment->SlotAnimTracks[0] : OnSegment->AddSlot(TEXT("DefaultSlot"));
	Slot.AnimTrack.AnimSegments.AddDefaulted_GetRef().SetAnimReference(Clip);
	TestEqual(TEXT("a lone marker on a segment animation is a problem"), ProblemCount(OnSegment), 1);

	// 끝나지 않는 몽타주 = 사용 GA 가 계속 켜진다.
	UAnimMontage* NoBlendOut = MakeUseMontage(1.0f);
	NoBlendOut->bEnableAutoBlendOut = false;
	TestEqual(TEXT("auto blend-out off is a problem"), ProblemCount(NoBlendOut), 1);
	UAnimMontage* Looping = MakeUseMontage(1.0f);
	FCompositeSection& Section = Looping->CompositeSections.AddDefaulted_GetRef();
	Section.SectionName = TEXT("Loop");
	Section.NextSectionName = TEXT("Loop");
	TestEqual(TEXT("a section that loops back is a problem"), ProblemCount(Looping), 1);
	// 대조군: 같은 섹션이 다음 없이 끝나면 문제가 아니다 - 위의 1 이 «섹션이 있어서» 가 아님을 보인다.
	Section.NextSectionName = NAME_None;
	TestEqual(TEXT("control: the same section without a loop is fine"), ProblemCount(Looping), 0);

#if WITH_EDITOR
	// 아이템 검증이 이 목록을 그대로 쓴다.
	const auto ItemWith = [](UAnimMontage* UseMontage)
	{
		UVBItemDefinition* Item = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 10);
		FVBItemFragment_Usable Usable;
		Usable.UseMontage = UseMontage;
		Item->Fragments.Add(FInstancedStruct::Make(Usable));
		return Item;
	};
	{
		FDataValidationContext Context;
		TestEqual(TEXT("item with a good use montage is valid"), ItemWith(Good)->IsDataValid(Context), EDataValidationResult::Valid);
	}
	{
		FDataValidationContext Context;
		TestEqual(TEXT("item with a marker in the blend-out is invalid"), ItemWith(MakeUseMontage(1.9f))->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	{
		FDataValidationContext Context;
		TestEqual(TEXT("an instant item (no montage) needs no marker"), ItemWith(nullptr)->IsDataValid(Context), EDataValidationResult::Valid);
	}
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventoryViewTest, "Vowbound.Unit.Inventory.ViewFilterSort",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventoryViewTest::RunTest(const FString& Parameters)
{
	// 화면이 무엇을 어떤 순서로 보이나(.sw/remaining-features/a1-inventory.md). 태그 계층은 네이티브 태그를 빌린다 - Melee.Shape 아래 셋, Melee.Window 하나.
	const auto Named = [](FGameplayTag Id, const TCHAR* Name)
	{
		UVBItemDefinition* Item = MakeItem(Id, 99);
		Item->DisplayName = FText::FromString(Name);
		return Item;
	};
	UVBItemDefinition* Gauze = Named(VBGameplayTags::Melee_Shape_Weapon, TEXT("C-거즈"));
	UVBItemDefinition* Key = Named(VBGameplayTags::Melee_Window, TEXT("A-열쇠"));
	UVBItemDefinition* Herb = Named(VBGameplayTags::Melee_Shape_HandL, TEXT("B-약초"));
	const auto Entry = [](const UVBItemDefinition* Item, int32 Count) { FVBInventoryEntry E; E.Definition = Item; E.Count = Count; return E; };
	// 얻은 순서 = 거즈(3) -> 열쇠(1) -> 약초(5).
	const TArray<FVBInventoryEntry> Entries = {Entry(Gauze, 3), Entry(Key, 1), Entry(Herb, 5)};
	const FGameplayTag All;
	const FGameplayTag Shape = FGameplayTag::RequestGameplayTag(TEXT("Melee.Shape"));

	TestTrue(TEXT("acquired order is the list as held"), VBInventoryView::BuildRows(Entries, All, EVBInventorySort::Acquired) == TArray<const UVBItemDefinition*>{Gauze, Key, Herb});
	TestTrue(TEXT("name order"), VBInventoryView::BuildRows(Entries, All, EVBInventorySort::Name) == TArray<const UVBItemDefinition*>{Key, Herb, Gauze});
	TestTrue(TEXT("count order, most first"), VBInventoryView::BuildRows(Entries, All, EVBInventorySort::Count) == TArray<const UVBItemDefinition*>{Herb, Gauze, Key});
	// 종류 = 태그 이름 순: Melee.Shape.HandL < Melee.Shape.Weapon < Melee.Window.
	TestTrue(TEXT("kind order groups by tag"), VBInventoryView::BuildRows(Entries, All, EVBInventorySort::Kind) == TArray<const UVBItemDefinition*>{Herb, Gauze, Key});
	// 필터 = 부모 태그 아래만. 대조군: 빈 필터는 셋 다.
	TestTrue(TEXT("parent tag filter keeps only its children"), VBInventoryView::BuildRows(Entries, Shape, EVBInventorySort::Acquired) == TArray<const UVBItemDefinition*>{Gauze, Herb});
	TestEqual(TEXT("control: empty filter shows all"), VBInventoryView::BuildRows(Entries, All, EVBInventorySort::Acquired).Num(), 3);

	// 같은 값은 얻은 순서를 지킨다(안정 정렬) - 수량이 같은 둘.
	UVBItemDefinition* Second = Named(VBGameplayTags::Melee_Shape_FootL, TEXT("D-붕대"));
	const TArray<FVBInventoryEntry> Ties = {Entry(Gauze, 2), Entry(Second, 2)};
	TestTrue(TEXT("ties keep acquired order"), VBInventoryView::BuildRows(Ties, All, EVBInventorySort::Count) == TArray<const UVBItemDefinition*>{Gauze, Second});

	TestTrue(TEXT("sort cycles back to acquired"), VBInventoryView::Next(VBInventoryView::Next(VBInventoryView::Next(VBInventoryView::Next(EVBInventorySort::Acquired)))) == EVBInventorySort::Acquired);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInventoryDiscardTest, "Vowbound.Unit.Inventory.Discard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInventoryDiscardTest::RunTest(const FString& Parameters)
{
	// 버리기는 땅의 아이템을 먼저 만들고 성공했을 때만 뺀다. 주인 없는 인벤토리엔 몸이 없어 떨구기가 실패한다 - 그때 아무것도 안 빠져야 한다.
	UVBItemDefinition* Gauze = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 10);
	UVBItemDefinition* Key = MakeItem(VBGameplayTags::Melee_Window, 1);
	Key->bCanDiscard = false;
	UVBInventoryComponent* Inventory = NewObject<UVBInventoryComponent>();
	Inventory->AddItem(Gauze, 3);
	Inventory->AddItem(Key, 1);

	TestFalse(TEXT("an item that cannot be discarded is refused"), Inventory->Discard(Key, 1));
	TestEqual(TEXT("refused key stays"), Inventory->GetCount(Key), 1);
	TestFalse(TEXT("display check agrees"), Inventory->CanDiscard(Key));
	TestFalse(TEXT("discarding more than held is refused"), Inventory->Discard(Gauze, 4));
	TestFalse(TEXT("discarding zero is refused"), Inventory->Discard(Gauze, 0));
	TestTrue(TEXT("display check allows a held discardable item"), Inventory->CanDiscard(Gauze));
	AddExpectedMessagePlain(TEXT("버리기 실패"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("no body to drop at - refused"), Inventory->Discard(Gauze, 1));
	TestEqual(TEXT("a failed drop removes nothing (no silent loss)"), Inventory->GetCount(Gauze), 3);
	return true;
}
#endif
