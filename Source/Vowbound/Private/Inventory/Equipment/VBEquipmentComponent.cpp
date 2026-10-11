// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Equipment/VBEquipmentComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBStatBonus.h"
#include "Inventory/Equipment/VBEquipmentRules.h"
#include "Player/VBPlayerStateLookup.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Vowbound/Vowbound.h"

UVBEquipmentComponent::UVBEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	SlotLayout = {VBGameplayTags::Equipment_Slot_Head, VBGameplayTags::Equipment_Slot_Body, VBGameplayTags::Equipment_Slot_Arms,
		VBGameplayTags::Equipment_Slot_Charm, VBGameplayTags::Equipment_Slot_Charm};
}

UVBEquipmentComponent* UVBEquipmentComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBEquipmentComponent>(Actor);
}

int32 UVBEquipmentComponent::FindSlotOf(const UVBItemDefinition* Item) const
{
	return Item ? Equipped.IndexOfByKey(Item) : INDEX_NONE;
}

void UVBEquipmentComponent::RequestEquip(const UVBItemDefinition* Item, int32 SlotIndex)
{
	ServerEquip(Item, SlotIndex);
}

void UVBEquipmentComponent::RequestUnequip(int32 SlotIndex)
{
	ServerUnequip(SlotIndex);
}

void UVBEquipmentComponent::ServerEquip_Implementation(const UVBItemDefinition* Item, int32 SlotIndex)
{
	Equip(Item, SlotIndex);
}

void UVBEquipmentComponent::ServerUnequip_Implementation(int32 SlotIndex)
{
	Unequip(SlotIndex);
}

bool UVBEquipmentComponent::Equip(const UVBItemDefinition* Item, int32 SlotIndex)
{
	if (!HasWriteAuthority(TEXT("Equip")))
	{
		return false;
	}
	// 표시가 보낸 아이템·칸을 믿지 않는다 - 가진 것인지, 칸이 맞는지 서버가 다시 본다.
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	if (!Inventory || Inventory->GetCount(Item) <= 0)
	{
		VB_LOG(Log, "장비: %s 입기 거절 - 가진 것이 없다", *GetNameSafe(Item));
		return false;
	}
	if (!GetAbilitySystem() || !EquipmentEffect)
	{
		VB_LOG(Warning, "장비: %s 입기 거절 - ASC(%s)나 EquipmentEffect(%s)가 없다. 효과 없는 장착은 거짓 상태라 입히지 않는다",
			*GetNameSafe(Item), *GetNameSafe(GetAbilitySystem()), *GetNameSafe(EquipmentEffect.Get()));
		return false;
	}
	TArray<const UVBItemDefinition*> Next;
	for (const TObjectPtr<const UVBItemDefinition>& Each : Equipped)
	{
		Next.Add(Each.Get());
	}
	int32 Slot = INDEX_NONE;
	int32 Vacated = INDEX_NONE;
	const UVBItemDefinition* Replaced = nullptr;
	const EVBEquipResult Result = VBEquipment::Equip(SlotLayout, Next, Item, SlotIndex, Slot, Replaced, Vacated);
	VB_LOG(Log, "장비: %s -> 칸 %d (%s)", *GetNameSafe(Item), Slot, VBEquipment::LexToString(Result));
	if (Result != EVBEquipResult::Changed)
	{
		return Result == EVBEquipResult::Unchanged;
	}
	// 효과를 먼저 뗀다(옮겨 오는 칸·바뀌는 칸). 그다음 칸을 바꾸고 새 효과를 건다.
	const TArray<TObjectPtr<const UVBItemDefinition>> Previous = Equipped;
	if (Vacated != INDEX_NONE)
	{
		RemoveEffect(Vacated);
	}
	RemoveEffect(Slot);
	Equipped.SetNum(SlotLayout.Num());
	for (int32 Index = 0; Index < Next.Num(); ++Index)
	{
		Equipped[Index] = Next[Index];
	}
	if (!ApplyEffect(Slot))
	{
		// 효과가 안 걸렸으면(스펙·적용 실패) 칸을 되돌린다 - 효과 없는 장착은 거짓 상태다. 옛 효과도 다시 못 걸면 그 칸을 비우고 알린다.
		VB_LOG(Warning, "장비: %s 의 효과(%s)를 걸지 못했다 - 칸 %d 를 되돌린다", *GetNameSafe(Item), *GetNameSafe(EquipmentEffect.Get()), Slot);
		Equipped = Previous;
		Equipped.SetNum(SlotLayout.Num());
		bool bDropped = false;
		for (const int32 Index : {Slot, Vacated})
		{
			if (Equipped.IsValidIndex(Index) && Equipped[Index] && !ApplyEffect(Index))
			{
				// 비운 뒤에 알린다(Unequip 과 같은 순서) - 듣는 쪽이 아직 입은 상태를 보지 않게.
				const UVBItemDefinition* Dropped = Equipped[Index];
				Equipped[Index] = nullptr;
				BroadcastEvent(VBGameplayTags::Event_Item_Unequipped, Dropped);
				bDropped = true;
			}
		}
		if (bDropped)
		{
			OnEquipmentChanged.Broadcast();
		}
		return false;
	}
	if (Replaced)
	{
		BroadcastEvent(VBGameplayTags::Event_Item_Unequipped, Replaced);
	}
	BroadcastEvent(VBGameplayTags::Event_Item_Equipped, Item);
	OnEquipmentChanged.Broadcast();
	return true;
}

bool UVBEquipmentComponent::Unequip(int32 SlotIndex)
{
	if (!HasWriteAuthority(TEXT("Unequip")) || !Equipped.IsValidIndex(SlotIndex) || !Equipped[SlotIndex])
	{
		return false;
	}
	const UVBItemDefinition* Item = Equipped[SlotIndex];
	RemoveEffect(SlotIndex);
	Equipped[SlotIndex] = nullptr;
	VB_LOG(Log, "장비: 칸 %d 의 %s 벗음", SlotIndex, *GetNameSafe(Item));
	BroadcastEvent(VBGameplayTags::Event_Item_Unequipped, Item);
	OnEquipmentChanged.Broadcast();
	return true;
}

bool UVBEquipmentComponent::ApplyEffect(int32 SlotIndex)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	const UVBItemDefinition* Item = GetEquipped(SlotIndex);
	const FVBItemFragment_Equippable* Equippable = Item ? Item->FindFragment<FVBItemFragment_Equippable>() : nullptr;
	if (!ASC || !EquipmentEffect || !Equippable)
	{
		return false;
	}
	// 지도 -> SetByCaller 채우기는 성장과 같이 쓴다(VBStatBonus, 설계 b2-growth.md 2절).
	EffectHandles.SetNum(SlotLayout.Num());
	EffectHandles[SlotIndex] = VBStatBonus::Apply(ASC, EquipmentEffect, Equippable->Stats, GetNameSafe(Item));
	return EffectHandles[SlotIndex].IsValid();
}

void UVBEquipmentComponent::RemoveEffect(int32 SlotIndex)
{
	if (!EffectHandles.IsValidIndex(SlotIndex) || !EffectHandles[SlotIndex].IsValid())
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ASC->RemoveActiveGameplayEffect(EffectHandles[SlotIndex]);
	}
	EffectHandles[SlotIndex].Invalidate();
}

void UVBEquipmentComponent::RemoveAllEffects()
{
	for (int32 Index = 0; Index < EffectHandles.Num(); ++Index)
	{
		RemoveEffect(Index);
	}
}

void UVBEquipmentComponent::HandleInventoryChanged()
{
	// 버리기·팔기로 가진 수가 0 이 된 장비는 벗긴다 - 없는 것을 입은 채 남지 않게.
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	if (!Inventory || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	// 저장을 읽히는 중이면 미룬다 - 인벤토리가 장비보다 먼저 읽혔는지에 따라 판정이 갈리지 않게 다 읽힌 뒤(HandleSaveApplied) 본다.
	if (const UVBSaveParticipantSubsystem* Registry = SaveRegistry.Get(); Registry && Registry->IsApplyingSave())
	{
		return;
	}
	for (int32 Index = 0; Index < Equipped.Num(); ++Index)
	{
		if (Equipped[Index] && Inventory->GetCount(Equipped[Index]) <= 0)
		{
			Unequip(Index);
		}
	}
}

void UVBEquipmentComponent::HandleSaveApplied()
{
	// 저장에만 있고 가진 수가 0 인 장비(인벤토리 쪽에서 못 되찾은 아이템, 손으로 고친 세이브)를 여기서 벗긴다 - 읽는 순서와 무관하다.
	HandleInventoryChanged();
}

void UVBEquipmentComponent::BroadcastEvent(const FGameplayTag& EventTag, const UVBItemDefinition* Item) const
{
	if (UVBGameEventSubsystem* Events = Item ? UVBGameEventSubsystem::Get(this) : nullptr)
	{
		FVBGameEvent Event;
		Event.EventTag = EventTag;
		Event.TargetTags.AddTag(Item->ItemId);
		Event.Instigator = GetOwningPlayerState();
		Events->Broadcast(Event);
	}
}

void UVBEquipmentComponent::OnRep_Equipped()
{
	OnEquipmentChanged.Broadcast();
}

void UVBEquipmentComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}
	Save.Equipment.Slots.Reset();
	for (const TObjectPtr<const UVBItemDefinition>& Item : Equipped)
	{
		Save.Equipment.Slots.Add(Item ? Item->ItemId : FGameplayTag());
	}
}

void UVBEquipmentComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}
	// 칸은 요청과 같은 판정(VBEquipment::Equip)으로 넣는다. 가진 것인지는 여기서 보지 않는다 - 인벤토리가 아직 안 읽혔을 수 있다.
	//   소유는 참여자가 모두 읽힌 뒤 HandleSaveApplied 가 한 번 본다(읽는 순서는 등록 순서라 정해져 있지 않다).
	RemoveAllEffects();
	TArray<const UVBItemDefinition*> Next;
	Next.Init(nullptr, SlotLayout.Num());
	const TArray<FGameplayTag>& Saved = Save.Equipment.Slots;
	VB_CLOG(Saved.Num() > SlotLayout.Num(), Warning, "장비 로드: 저장 칸 %d 개가 칸 배치 %d 개보다 많다 - 넘는 칸은 버린다(칸 배치가 줄었나)",
		Saved.Num(), SlotLayout.Num());
	for (int32 Index = 0; Index < FMath::Min(Saved.Num(), SlotLayout.Num()); ++Index)
	{
		if (!Saved[Index].IsValid())
		{
			continue;
		}
		const UVBItemDefinition* Item = UVBItemDefinition::FindById(Saved[Index]);
		int32 Slot = INDEX_NONE;
		int32 Vacated = INDEX_NONE;
		const UVBItemDefinition* Replaced = nullptr;
		const EVBEquipResult Result = Item ? VBEquipment::Equip(SlotLayout, Next, Item, Index, Slot, Replaced, Vacated) : EVBEquipResult::NotEquippable;
		VB_CLOG(Result != EVBEquipResult::Changed, Warning, "장비 로드: 칸 %d 의 %s 를 입히지 못했다(%s) - 빈 칸",
			Index, *Saved[Index].ToString(), Item ? VBEquipment::LexToString(Result) : TEXT("되찾지 못함"));
	}
	Equipped.Reset();
	for (const UVBItemDefinition* Item : Next)
	{
		Equipped.Add(Item);
	}
	for (int32 Index = 0; Index < Equipped.Num(); ++Index)
	{
		if (Equipped[Index] && !ApplyEffect(Index))
		{
			VB_LOG(Warning, "장비 로드: 칸 %d 의 %s 효과(%s)를 걸지 못했다 - 빈 칸으로 둔다(효과 없는 장착은 거짓 상태)",
				Index, *GetNameSafe(Equipped[Index].Get()), *GetNameSafe(EquipmentEffect.Get()));
			Equipped[Index] = nullptr;
		}
	}
	OnEquipmentChanged.Broadcast();
}

void UVBEquipmentComponent::InitializeComponent()
{
	Super::InitializeComponent();
	Equipped.Init(nullptr, SlotLayout.Num());
	EffectHandles.SetNum(SlotLayout.Num());
	UVBSaveParticipantSubsystem::Register(this);
	// 이어하기는 BeginPlay 보다 먼저 읽힌다(VBSaveParticipant.h) - 그 끝 신호를 놓치지 않게 등록과 같은 자리에서 듣는다.
	if (UVBSaveParticipantSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr)
	{
		SaveRegistry = Registry;
		SaveAppliedHandle = Registry->OnSaveApplied.AddUObject(this, &UVBEquipmentComponent::HandleSaveApplied);
	}
}

void UVBEquipmentComponent::UninitializeComponent()
{
	if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner()))
	{
		Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
	}
	if (UVBSaveParticipantSubsystem* Registry = SaveRegistry.Get())
	{
		Registry->OnSaveApplied.Remove(SaveAppliedHandle);
	}
	SaveRegistry.Reset();
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner()))
		{
			InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UVBEquipmentComponent::HandleInventoryChanged);
		}
	}
}

void UVBEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVBEquipmentComponent, Equipped, COND_OwnerOnly);
}

bool UVBEquipmentComponent::HasWriteAuthority(const TCHAR* What) const
{
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		VB_LOG(Warning, "장비 %s 을 권한 없는 쪽에서 불렀다 - 무시한다(쓰기는 서버만)", What);
		return false;
	}
	return true;
}

APlayerState* UVBEquipmentComponent::GetOwningPlayerState() const
{
	return Cast<APlayerState>(GetOwner());
}

UAbilitySystemComponent* UVBEquipmentComponent::GetAbilitySystem() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}
