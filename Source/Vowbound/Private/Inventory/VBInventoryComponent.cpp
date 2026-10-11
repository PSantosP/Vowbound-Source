// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBInventoryComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBQuickSlotRules.h"
#include "Inventory/VBWorldItem.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Player/VBPlayerStateLookup.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Vowbound/Vowbound.h"

// ---------------------------------------------------------------- 복제 콜백

void FVBInventoryEntry::PostReplicatedAdd(const FVBInventoryList& InList)
{
	if (InList.Owner) { InList.Owner->HandleReplicatedChange(); }
}

void FVBInventoryEntry::PostReplicatedChange(const FVBInventoryList& InList)
{
	if (InList.Owner) { InList.Owner->HandleReplicatedChange(); }
}

void FVBInventoryEntry::PreReplicatedRemove(const FVBInventoryList& InList)
{
	if (InList.Owner) { InList.Owner->HandleReplicatedChange(); }
}

// ---------------------------------------------------------------- 컴포넌트

UVBInventoryComponent::UVBInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	List.Owner = this;
}

UVBInventoryComponent* UVBInventoryComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBInventoryComponent>(Actor);
}

int32 UVBInventoryComponent::GetCount(const UVBItemDefinition* Item) const
{
	const FVBInventoryEntry* Entry = FindEntry(Item);
	return Entry ? Entry->Count : 0;
}

int32 UVBInventoryComponent::AddItem(const UVBItemDefinition* Item, int32 Count)
{
	if (!Item || Count <= 0 || !HasWriteAuthority(TEXT("AddItem")))
	{
		return 0;
	}
	FVBInventoryEntry* Entry = FindEntry(Item);
	const int32 Added = Item->ClampToMax(Entry ? Entry->Count : 0, Count);
	if (Added <= 0)
	{
		VB_LOG(Log, "인벤토리: %s 가 가득 찼다(상한 %d) - 넣지 않음", *GetNameSafe(Item), Item->MaxCount);
		return 0;
	}
	if (Entry)
	{
		Entry->Count += Added;
		List.MarkItemDirty(*Entry);
	}
	else
	{
		FVBInventoryEntry& NewEntry = List.Entries.AddDefaulted_GetRef();
		NewEntry.Definition = Item;
		NewEntry.Count = Added;
		List.MarkItemDirty(NewEntry);
	}
	VB_LOG(Log, "인벤토리: %s +%d -> %d", *GetNameSafe(Item), Added, GetCount(Item));
	OnInventoryChanged.Broadcast();

	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		FVBGameEvent Event;
		Event.EventTag = VBGameplayTags::Event_Item_Acquired;
		Event.TargetTags.AddTag(Item->ItemId);
		Event.Instigator = GetOwningPlayerState();
		Event.Count = Added;
		Events->Broadcast(Event);
	}
	// 주인 클라의 화면에 알린다. 리슨 서버의 호스트면 클라 RPC 가 이 자리에서 바로 돈다.
	ClientItemAcquired(Item, Added);
	return Added;
}

bool UVBInventoryComponent::RemoveItem(const UVBItemDefinition* Item, int32 Count)
{
	if (!Item || Count <= 0 || !HasWriteAuthority(TEXT("RemoveItem")))
	{
		return false;
	}
	const int32 Index = List.Entries.IndexOfByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
	if (Index == INDEX_NONE || List.Entries[Index].Count < Count)
	{
		return false;
	}
	FVBInventoryEntry& Entry = List.Entries[Index];
	Entry.Count -= Count;
	if (Entry.Count == 0)
	{
		// 0 인 줄은 두지 않는다 - 없음 = 0 한 가지 표현(사실 저장소의 카운터와 같은 규약).
		List.Entries.RemoveAt(Index);
		List.MarkArrayDirty();
	}
	else
	{
		List.MarkItemDirty(Entry);
	}
	VB_LOG(Log, "인벤토리: %s -%d -> %d", *GetNameSafe(Item), Count, GetCount(Item));
	OnInventoryChanged.Broadcast();
	return true;
}

void UVBInventoryComponent::RequestUseItem(const UVBItemDefinition* Item)
{
	ServerUseItem(Item);
}

bool UVBInventoryComponent::CanUseItem(const UVBItemDefinition* Item) const
{
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || GetCount(Item) <= 0)
	{
		return false;
	}
	const FVBRuleContext Context = FVBRuleContext::Make(this, GetOwningPlayerState());
	return VBRules::EvaluateAll(Usable->Conditions, Context);
}

void UVBInventoryComponent::ServerUseItem_Implementation(const UVBItemDefinition* Item)
{
	BeginUse(Item);
}

void UVBInventoryComponent::RequestDiscard(const UVBItemDefinition* Item, int32 Count)
{
	ServerDiscard(Item, Count);
}

bool UVBInventoryComponent::CanDiscard(const UVBItemDefinition* Item) const
{
	return Item && Item->bCanDiscard && GetCount(Item) > 0;
}

void UVBInventoryComponent::ServerDiscard_Implementation(const UVBItemDefinition* Item, int32 Count)
{
	Discard(Item, Count);
}

bool UVBInventoryComponent::Discard(const UVBItemDefinition* Item, int32 Count)
{
	if (!HasWriteAuthority(TEXT("Discard")))
	{
		return false;
	}
	// 표시가 켠 버튼을 믿지 않는다 - 버릴 수 있는 아이템인지, 가진 만큼인지 서버가 다시 본다.
	if (!Item || !Item->bCanDiscard || Count <= 0 || GetCount(Item) < Count)
	{
		VB_LOG(Log, "인벤토리: %s x%d 버리기 거절 - %s", *GetNameSafe(Item), Count,
			!Item ? TEXT("아이템 없음") : !Item->bCanDiscard ? TEXT("버릴 수 없는 아이템") : TEXT("가진 수 부족"));
		return false;
	}
	// 땅의 아이템을 먼저 만든다. 실패하면(몸이 없다·땅 아이템 클래스 미설정) 빼지 않는다 - 아이템이 조용히 사라지는 쪽으로 떨어지지 않게.
	const APlayerState* PlayerState = GetOwningPlayerState();
	const APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
	AVBWorldItem* Dropped = Pawn ? AVBWorldItem::SpawnDrop(Pawn, Item, Count, 0) : nullptr;
	if (!Dropped)
	{
		VB_LOG(Warning, "인벤토리: %s x%d 버리기 실패 - 떨굴 수 없다(몸 %s). 빼지 않는다", *GetNameSafe(Item), Count, *GetNameSafe(Pawn));
		return false;
	}
	if (!RemoveItem(Item, Count))
	{
		// 위에서 수를 봤으니 오지 않는 길이다. 와도 같은 아이템이 둘이 되지 않게 떨군 것을 거둔다.
		Dropped->Destroy();
		return false;
	}
	VB_LOG(Log, "인벤토리: %s x%d 버림 - 발밑 %s", *GetNameSafe(Item), Count, *GetNameSafe(Dropped));
	return true;
}

void UVBInventoryComponent::BeginUse(const UVBItemDefinition* Item)
{
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || !Usable->UseMontage)
	{
		// 몸 동작이 없으면 지금처럼 바로 쓴다. 쓸 수 없는 아이템의 거절 로그도 ConfirmUse 한 곳에서 낸다.
		ConfirmUse(Item);
		return;
	}
	// 몸 동작이 있으면 사용 GA 가 맡는다. 확정은 몽타주의 표식 때 그 GA 가 ConfirmUse 로 한다(설계 .sw/quickslot-input 6.1절).
	APlayerState* PlayerState = GetOwningPlayerState();
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerState);
	if (!Pawn || !ASC)
	{
		VB_LOG(Log, "인벤토리: %s 사용 거절 - 몸(%s)이나 ASC(%s)가 없다", *GetNameSafe(Item), *GetNameSafe(Pawn), *GetNameSafe(ASC));
		return;
	}
	FGameplayEventData Event;
	Event.EventTag = VBGameplayTags::Event_Item_Use;
	Event.Instigator = Pawn;
	Event.Target = Pawn;
	Event.OptionalObject = Item;
	// 켜진 수가 0 = 지금 쓸 수 없다(수·조건 - UVBGA_UseItem::ShouldAbilityRespondToEvent), 이미 쓰는 중·몸이 다른 일에 묶여 있다(시작 막기),
	//   또는 GA 가 부여되지 않았다. 어느 쪽이든 아무것도 소모하지 않는다. 흔한 경로(공격 중에 누름)라 경고가 아니라 Log 다.
	if (ASC->HandleGameplayEvent(Event.EventTag, &Event) == 0)
	{
		VB_LOG(Log, "인벤토리: %s 사용 동작을 지금 시작할 수 없다 - 수·조건·쓰는 중·몸이 묶임·사용 GA 미부여(BP_VBPlayerState.DefaultAbilities)", *GetNameSafe(Item));
	}
}

bool UVBInventoryComponent::ConfirmUse(const UVBItemDefinition* Item)
{
	if (!HasWriteAuthority(TEXT("ConfirmUse")))
	{
		return false;
	}
	// 표시가 켠 버튼도, 동작을 시작할 때의 미리 보기도 믿지 않는다 - 수·조각·조건을 이 시각에 다시 본다.
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || GetCount(Item) <= 0)
	{
		VB_LOG(Warning, "인벤토리: %s 사용 거절 - %s", *GetNameSafe(Item), !Usable ? TEXT("쓸 수 있는 아이템이 아니다") : TEXT("가진 것이 없다"));
		return false;
	}
	const FVBRuleContext Context = FVBRuleContext::Make(this, GetOwningPlayerState());
	if (!VBRules::EvaluateAll(Usable->Conditions, Context))
	{
		VB_LOG(Log, "인벤토리: %s 사용 거절 - 조건이 거짓", *GetNameSafe(Item));
		return false;
	}
	// 소모를 먼저 한다. 효과가 이 인벤토리를 바꾸는 날(아이템을 주는 효과)이 와도 «쓴 것이 사라졌다» 가 먼저 참이게.
	if (Usable->bConsumeOnUse && !RemoveItem(Item, 1))
	{
		return false;
	}
	VBRules::ApplyAll(Usable->Effects, Context);
	VB_LOG(Log, "인벤토리: %s 사용", *GetNameSafe(Item));

	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		FVBGameEvent Event;
		Event.EventTag = VBGameplayTags::Event_Item_Used;
		Event.TargetTags.AddTag(Item->ItemId);
		Event.Instigator = Context.Instigator;
		Events->Broadcast(Event);
	}
	return true;
}

void UVBInventoryComponent::ClientItemAcquired_Implementation(const UVBItemDefinition* Item, int32 Count)
{
	OnItemAcquired.Broadcast(Item, Count);
}

void UVBInventoryComponent::RequestAssignQuickSlot(int32 SlotIndex, const UVBItemDefinition* Item)
{
	ServerAssignQuickSlot(SlotIndex, Item);
}

void UVBInventoryComponent::ServerAssignQuickSlot_Implementation(int32 SlotIndex, const UVBItemDefinition* Item)
{
	// 표시가 보낸 칸·아이템을 믿지 않는다 - 칸 범위와 «사용» 조각을 서버가 다시 본다. 가진 수는 보지 않는다(0 개여도 칸은 남는다, 4절).
	const EVBQuickSlotAssignResult Result = VBQuickSlot::Assign(QuickSlots, SlotIndex, Item);
	VB_LOG(Log, "퀵슬롯: 칸 %d <- %s (%s)", SlotIndex, *GetNameSafe(Item), VBQuickSlot::LexToString(Result));
	if (Result == EVBQuickSlotAssignResult::Changed)
	{
		OnQuickSlotsChanged.Broadcast();
	}
}

void UVBInventoryComponent::RequestUseQuickSlot(int32 SlotIndex)
{
	ServerUseQuickSlot(SlotIndex);
}

void UVBInventoryComponent::ServerUseQuickSlot_Implementation(int32 SlotIndex)
{
	const UVBItemDefinition* Item = GetQuickSlotItem(SlotIndex);
	if (!Item || GetCount(Item) <= 0)
	{
		// 빈 칸·다 쓴 칸(흐리게 남아 있는 0 개, 설계 4절)을 누른 것은 잘못이 아니다 - 소리 내지 않는다.
		//   0 개를 사용 길까지 보내면 누를 때마다 «가진 것이 없다» 경고가 찍혔다(2026-10-03 커밋 검증 지적).
		return;
	}
	// 사용의 시작은 한 함수다(설계 4.1절) - 인벤토리 화면의 «사용» 도 같은 길이다.
	BeginUse(Item);
}

void UVBInventoryComponent::OnRep_QuickSlots()
{
	OnQuickSlotsChanged.Broadcast();
}

void UVBInventoryComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}
	Save.Inventory.Items.Reset();
	for (const FVBInventoryEntry& Entry : List.Entries)
	{
		if (Entry.Definition && Entry.Count > 0)
		{
			FVBSavedItem& Saved = Save.Inventory.Items.AddDefaulted_GetRef();
			Saved.ItemId = Entry.Definition->ItemId;
			Saved.Count = Entry.Count;
		}
	}
	// 읽을 때 넣지 못한 줄을 그대로 다시 쓴다 - 버리면 아이템 자산·ItemId 를 바꾼 한 번, 상한을 낮춘 한 번이 영영 가진 것을 지운다(검증 W2·2차 2).
	Save.Inventory.Items.Append(KeptSavedItems);

	Save.Inventory.QuickSlots.Reset();
	for (const TObjectPtr<const UVBItemDefinition>& Item : QuickSlots)
	{
		Save.Inventory.QuickSlots.Add(Item ? Item->ItemId : FGameplayTag());
	}
}

void UVBInventoryComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}
	ResolveSavedItems(Save.Inventory.Items, &UVBItemDefinition::FindById, List.Entries, KeptSavedItems);
	List.MarkArrayDirty();
	VB_LOG(Log, "인벤토리 로드: %d 줄(넣지 못해 지킨 저장 줄 %d)", List.Entries.Num(), KeptSavedItems.Num());
	OnInventoryChanged.Broadcast();

	// 칸은 요청과 같은 판정(VBQuickSlot::Assign)으로 넣는다 - 저장 줄이 «사용 조각·한 아이템 한 칸» 을 건너뛰지 못하게.
	//   못 찾은 아이템은 빈 칸 + 경고다. 가진 줄과 달리 지키지 않는 이유: 칸은 가진 것이 아니라 가리키는 자리라 잃어도 다시 넣으면 그만이고,
	//   지켜 둔 이름과 지금 칸이 저장에서 섞이면 «한 아이템 한 칸» 을 저장 쪽에서 또 따져야 한다.
	QuickSlots.Init(nullptr, QuickSlotCount);
	const TArray<FGameplayTag>& SavedSlots = Save.Inventory.QuickSlots;
	VB_CLOG(SavedSlots.Num() > QuickSlotCount, Warning, "퀵슬롯 로드: 저장된 칸 %d 가 칸 수 %d 보다 많다 - 넘는 칸은 버린다", SavedSlots.Num(), QuickSlotCount);
	for (int32 Index = 0; Index < FMath::Min(SavedSlots.Num(), QuickSlotCount); ++Index)
	{
		if (!SavedSlots[Index].IsValid())
		{
			continue;
		}
		const UVBItemDefinition* Item = UVBItemDefinition::FindById(SavedSlots[Index]);
		const EVBQuickSlotAssignResult Result = Item ? VBQuickSlot::Assign(QuickSlots, Index, Item) : EVBQuickSlotAssignResult::Unchanged;
		VB_CLOG(!Item || (Result != EVBQuickSlotAssignResult::Changed), Warning, "퀵슬롯 로드: 칸 %d 의 %s 를 넣지 못했다(%s) - 빈 칸",
			Index, *SavedSlots[Index].ToString(), Item ? VBQuickSlot::LexToString(Result) : TEXT("되찾지 못함"));
	}
	OnQuickSlotsChanged.Broadcast();
}

void UVBInventoryComponent::ResolveSavedItems(const TArray<FVBSavedItem>& Saved, TFunctionRef<const UVBItemDefinition*(const FGameplayTag&)> Resolve,
	TArray<FVBInventoryEntry>& OutEntries, TArray<FVBSavedItem>& OutKept)
{
	OutEntries.Reset();
	OutKept.Reset();
	for (const FVBSavedItem& Row : Saved)
	{
		if (Row.Count <= 0)
		{
			// 빈 줄은 되살릴 것도 지킬 것도 없다.
			VB_LOG(Warning, "인벤토리 로드: %s 의 수가 %d - 버린다", *Row.ItemId.ToString(), Row.Count);
			continue;
		}
		const UVBItemDefinition* Item = Resolve(Row.ItemId);
		if (!Item)
		{
			// 아이템 자산이 사라졌거나 ItemId 가 바뀐 것이다. 지켜 두면 자산·ItemId 를 되살렸을 때 돌아온다.
			VB_LOG(Warning, "인벤토리 로드: %s x%d 를 되찾지 못했다(아이템 자산·ItemId·에셋 매니저 설정을 본다) - 저장 줄은 지킨다", *Row.ItemId.ToString(), Row.Count);
			OutKept.Add(Row);
			continue;
		}
		// 같은 아이템 줄이 둘이면 합치고 상한에서 자른다 - 손대거나 깨진 슬롯이 넣는 길(AddItem)의 규칙을 건너뛰지 못하게.
		//   줄은 실제로 넣을 수가 있을 때만 만든다 - 0개 줄은 «없음 = 0» 규약(RemoveItem)과 어긋난다.
		FVBInventoryEntry* Entry = OutEntries.FindByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
		const int32 Accepted = Item->ClampToMax(Entry ? Entry->Count : 0, Row.Count);
		if (Accepted > 0)
		{
			if (!Entry)
			{
				Entry = &OutEntries.AddDefaulted_GetRef();
				Entry->Definition = Item;
				Entry->Count = 0;
			}
			Entry->Count += Accepted;
		}
		if (Accepted < Row.Count)
		{
			// 상한을 넘은 몫도 지킨다 - 상한이 다시 오르거나 가진 수가 줄면 다음 불러오기에 들어온다.
			VB_LOG(Warning, "인벤토리 로드: %s x%d 가 상한(%d)을 넘어 %d 만 넣었다 - 남은 %d 는 저장 줄로 지킨다",
				*Row.ItemId.ToString(), Row.Count, Item->MaxCount, Accepted, Row.Count - Accepted);
			FVBSavedItem& Kept = OutKept.AddDefaulted_GetRef();
			Kept.ItemId = Row.ItemId;
			Kept.Count = Row.Count - Accepted;
		}
	}
}

void UVBInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();
	List.Owner = this;
	// 칸 수는 BP 기본값이 정한다(생성자에서는 C++ 기본값밖에 안 보인다). 클라도 같은 빈 칸으로 시작해, 서버가 바꾸기 전에도 칸 줄이 그려진다.
	QuickSlots.Init(nullptr, QuickSlotCount);
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBInventoryComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 가진 것은 그 플레이어만 안다(설계 3.1절).
	DOREPLIFETIME_CONDITION(UVBInventoryComponent, List, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UVBInventoryComponent, QuickSlots, COND_OwnerOnly);
}

APlayerState* UVBInventoryComponent::GetOwningPlayerState() const
{
	return Cast<APlayerState>(GetOwner());
}

bool UVBInventoryComponent::HasWriteAuthority(const TCHAR* What) const
{
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		VB_LOG(Warning, "인벤토리 %s 을 권한 없는 쪽에서 불렀다 - 무시한다(쓰기는 서버만)", What);
		return false;
	}
	return true;
}

FVBInventoryEntry* UVBInventoryComponent::FindEntry(const UVBItemDefinition* Item)
{
	return List.Entries.FindByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
}

const FVBInventoryEntry* UVBInventoryComponent::FindEntry(const UVBItemDefinition* Item) const
{
	return List.Entries.FindByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
}

#if !UE_BUILD_SHIPPING
namespace
{
	// 개발용 콘솔 명령: 서버 월드의 모든 플레이어에게 아이템을 넣거나(+) 뺀다(-). 출시 빌드에는 없다.
	//   왜: «가득 참» 을 PIE 에서 만들려면 줍기·퀘스트·상점을 다 돌아야 했다(거즈 상한 10, 2026-10-03 서랍장 남은 몫 시험 - .sw/narrative 14.5).
	//   쓰는 법: vb.Inventory.Give Item.Medical.Gauze 10 / vb.Inventory.Give Item.Medical.Gauze -3. 넣기·빼기는 실제 길(AddItem·RemoveItem)을 탄다 - 획득 알림·상한도 그대로다.
	FAutoConsoleCommandWithWorldAndArgs GVBInventoryGiveCommand(
		TEXT("vb.Inventory.Give"),
		TEXT("Dev only. vb.Inventory.Give <ItemId tag> <count> - add (or remove when negative) items for every player on the server world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
			if (Args.Num() < 2 || !GameState || World->GetNetMode() == NM_Client)
			{
				VB_LOG(Warning, "vb.Inventory.Give: %s", Args.Num() < 2 ? TEXT("인자 둘이 필요하다 - <ItemId 태그> <수>") : TEXT("서버 게임 월드에서만 된다(PIE 중에 친다)"));
				return;
			}
			const FGameplayTag ItemId = FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false);
			const UVBItemDefinition* Item = ItemId.IsValid() ? UVBItemDefinition::FindById(ItemId) : nullptr;
			const int32 Count = FCString::Atoi(*Args[1]);
			if (!Item || Count == 0)
			{
				VB_LOG(Warning, "vb.Inventory.Give: %s", !Item ? *FString::Printf(TEXT("아이템 %s 를 못 찾았다"), *Args[0]) : TEXT("수가 0 이다"));
				return;
			}
			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(PlayerState);
				if (!Inventory)
				{
					continue;
				}
				int32 Changed = 0;
				if (Count > 0)
				{
					Changed = Inventory->AddItem(Item, Count);
				}
				else
				{
					// 빼기는 가진 만큼까지만 - RemoveItem 은 모자라면 아무것도 안 뺀다.
					const int32 ToRemove = FMath::Min(-Count, Inventory->GetCount(Item));
					Changed = (ToRemove > 0 && Inventory->RemoveItem(Item, ToRemove)) ? -ToRemove : 0;
				}
				VB_LOG(Log, "vb.Inventory.Give: %s %s %+d -> 가진 수 %d", *GetNameSafe(PlayerState), *Item->GetName(), Changed, Inventory->GetCount(Item));
			}
		}));
}
#endif
