// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "SaveSystem/VBSaveGameTypes.h"   // FVBSavedItem - 못 찾은 저장 줄을 값으로 지킨다
#include "VBInventoryComponent.generated.h"

class UVBItemDefinition;
class UVBInventoryComponent;
class APlayerState;

/** 가진 아이템 한 줄(한 정의에 한 줄). */
USTRUCT()
struct FVBInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBItemDefinition> Definition = nullptr;

	UPROPERTY()
	int32 Count = 0;

	void PostReplicatedAdd(const struct FVBInventoryList& List);
	void PostReplicatedChange(const struct FVBInventoryList& List);
	void PreReplicatedRemove(const struct FVBInventoryList& List);
};

/** 복제되는 목록. 바뀐 줄만 보낸다(FFastArraySerializer). */
USTRUCT()
struct FVBInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVBInventoryEntry> Entries;

	// 복제 콜백이 알릴 주인. 복제하지 않는다.
	UPROPERTY(NotReplicated)
	TObjectPtr<UVBInventoryComponent> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FVBInventoryEntry, FVBInventoryList>(Entries, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FVBInventoryList> : public TStructOpsTypeTraitsBase2<FVBInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};

DECLARE_MULTICAST_DELEGATE(FVBOnInventoryChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnItemAcquired, const UVBItemDefinition*, int32);

/**
 * 가진 아이템의 주인. AVBPlayerState 에 붙는다. 설계 .sw/narrative 3.1절·10.2절·5절(주인 지도).
 *
 * 왜 PlayerState 인가: 폰이 죽고 다시 나도 가진 것은 남는다. ASC 도 이미 PlayerState 에 있다.
 * 쓰기는 서버만 한다. 클라(표시)는 요청만 보낸다 - 사용 = RequestUseItem -> ServerUseItem -> BeginUse. 확정(ConfirmUse)에서 서버가 수·조각·조건을 다시 본다.
 * 넣고 빼는 길은 이 API 하나다. 줍기·보상·상점 거래가 목록을 직접 만지면 «누가 넣었나» 를 찾을 곳이 흩어진다.
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBInventoryComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBInventoryComponent();

	// 폰·컨트롤러·PlayerState 어느 쪽을 줘도 그 플레이어의 인벤토리. 없으면 nullptr.
	static UVBInventoryComponent* FindFor(const AActor* Actor);

	int32 GetCount(const UVBItemDefinition* Item) const;
	const TArray<FVBInventoryEntry>& GetEntries() const { return List.Entries; }

	// 서버에서만. 상한에서 자르고 실제로 넣은 수를 돌려준다. 넣었으면 Event.Item.Acquired 방송 + 주인 클라에 획득 알림.
	int32 AddItem(const UVBItemDefinition* Item, int32 Count);

	// 서버에서만. 모자라면 아무것도 안 빼고 false - 반쪽 거래를 막는다.
	bool RemoveItem(const UVBItemDefinition* Item, int32 Count);

	// 표시가 부른다. 판단은 서버가 다시 한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Item")
	void RequestUseItem(const UVBItemDefinition* Item);

	// 지금 이 아이템을 쓸 수 있어 보이나(수·조각·조건). 표시는 버튼을 켜고 끄는 데, 사용 GA 는 몸을 움직이기 전 미리 보기에 쓴다.
	//   결정은 ConfirmUse 가 그 시각에 다시 한다.
	bool CanUseItem(const UVBItemDefinition* Item) const;

	// 표시가 부른다(인벤토리 화면의 «버리기»). 판단은 서버가 다시 한다. 버린 몫은 발밑에 다시 주울 수 있는 땅의 아이템으로 떨어진다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Item")
	void RequestDiscard(const UVBItemDefinition* Item, int32 Count);

	// 버릴 수 있어 보이나(표시용 - 버튼을 켜고 끄는 데만). bCanDiscard 이고 가진 것이 있으면.
	bool CanDiscard(const UVBItemDefinition* Item) const;

	// 서버에서만. 땅의 아이템을 «먼저» 만들고 성공했을 때만 뺀다 - 떨구기가 실패하면 아무것도 안 뺀다(조용히 사라지지 않게). 버렸으면 true.
	bool Discard(const UVBItemDefinition* Item, int32 Count);

	// 서버에서만. 사용의 확정 - 수·조각·조건을 다시 보고 소모 -> 효과 -> Event.Item.Used. 확정했으면 true.
	//   바로 쓰는 아이템은 BeginUse 가, 몸 동작이 있는 아이템은 사용 GA(UVBGA_UseItem)가 표식 시각에 부른다 - 두 벌로 짜지 않는다(설계 .sw/quickslot-input 6절).
	bool ConfirmUse(const UVBItemDefinition* Item);

	// 서버는 바꾼 직후, 클라는 복제가 도착했을 때.
	FVBOnInventoryChanged OnInventoryChanged;

	// ---- 퀵슬롯(설계 .sw/quickslot-input 4.1절). 칸은 «가진 것» 을 가리키는 보기라 칸의 주인도 인벤토리다(설계 7절 지도).
	//   칸은 가진 수가 0 이 돼도 비우지 않는다 - 다시 주우면 그 자리에 돌아온다. 판정은 VBQuickSlot(순수 함수) 하나다.

	const TArray<TObjectPtr<const UVBItemDefinition>>& GetQuickSlots() const { return QuickSlots; }
	const UVBItemDefinition* GetQuickSlotItem(int32 SlotIndex) const { return QuickSlots.IsValidIndex(SlotIndex) ? QuickSlots[SlotIndex].Get() : nullptr; }

	// 표시가 부른다(인벤토리 화면의 칸 버튼). 판단은 서버가 한다. Item = nullptr 이면 칸을 비운다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Item")
	void RequestAssignQuickSlot(int32 SlotIndex, const UVBItemDefinition* Item);

	// 입력이 부른다(AVBPlayerController 의 퀵슬롯 키). 서버가 칸의 아이템을 찾아 사용 길로 넘긴다 - 빈 칸이면 아무것도 안 한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Item")
	void RequestUseQuickSlot(int32 SlotIndex);

	// 칸이 바뀌었을 때. 서버는 바꾼 직후, 클라는 복제가 도착했을 때. 가진 수 변화는 OnInventoryChanged 다(칸 표시는 둘 다 듣는다).
	FVBOnInventoryChanged OnQuickSlotsChanged;

	// 이 플레이어의 화면에서만(획득 알림). 서버가 주인 클라로 보낸다.
	FVBOnItemAcquired OnItemAcquired;

	//~ IVBSaveParticipant - 인벤토리 구역(UVBSaveGame::Inventory). 문맥의 PlayerState 가 주인일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	// 복제 콜백(FVBInventoryEntry)이 부른다.
	void HandleReplicatedChange() { OnInventoryChanged.Broadcast(); }

	// 저장 줄 -> 가진 줄(ReadSave 의 판정. 월드·에셋 매니저 없이 단위 시험이 덮도록 되찾기를 인자로 받는다).
	//   같은 아이템 줄은 합치고 상한에서 자른다. 되찾지 못한 줄과 상한을 넘은 몫은 OutKept 로 간다 - 버리지 않고 다음 저장에 그대로 다시 쓴다
	//   (검증 W2·2차 2: 자산 이름을 바꾼 한 번, 상한을 낮춘 패치 한 번이 가진 것을 지우지 않게). 수가 0 이하인 줄은 버리고, 0개 줄은 만들지 않는다.
	static void ResolveSavedItems(const TArray<FVBSavedItem>& Saved, TFunctionRef<const UVBItemDefinition*(const FGameplayTag&)> Resolve,
		TArray<FVBInventoryEntry>& OutEntries, TArray<FVBSavedItem>& OutKept);

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 퀵슬롯 칸 수. 칸의 주인이 이 컴포넌트라 수도 여기 둔다 - 경제 설정이 아니다(설계 4.1절). BP 기본값이라 빌드 없이 바뀐다.
	//   키는 칸마다 하나라(AVBPlayerController::QuickSlotActions) 칸을 늘리면 입력 액션도 같이 더한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Item", meta=(ClampMin="0", ClampMax="10"))
	int32 QuickSlotCount = 4;

private:
	UPROPERTY(Replicated)
	FVBInventoryList List;

	// 칸마다 아이템 정의(빈 칸 = nullptr). 그 플레이어만 안다(가진 것과 같다).
	UPROPERTY(ReplicatedUsing=OnRep_QuickSlots)
	TArray<TObjectPtr<const UVBItemDefinition>> QuickSlots;

	UFUNCTION()
	void OnRep_QuickSlots();

	UFUNCTION(Server, Reliable)
	void ServerAssignQuickSlot(int32 SlotIndex, const UVBItemDefinition* Item);

	UFUNCTION(Server, Reliable)
	void ServerUseQuickSlot(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerUseItem(const UVBItemDefinition* Item);

	UFUNCTION(Server, Reliable)
	void ServerDiscard(const UVBItemDefinition* Item, int32 Count);

	UFUNCTION(Client, Unreliable)
	void ClientItemAcquired(const UVBItemDefinition* Item, int32 Count);

	// 서버 쪽 사용의 시작 하나(퀵슬롯·화면). 몸 동작이 없으면 바로 ConfirmUse, 있으면 폰 ASC 에 Event.Item.Use 를 보내 사용 GA 를 켠다.
	void BeginUse(const UVBItemDefinition* Item);

	APlayerState* GetOwningPlayerState() const;
	bool HasWriteAuthority(const TCHAR* What) const;
	FVBInventoryEntry* FindEntry(const UVBItemDefinition* Item);
	const FVBInventoryEntry* FindEntry(const UVBItemDefinition* Item) const;

	// 불러왔지만 가진 줄에 넣지 못한 저장 줄(되찾지 못한 줄·상한을 넘은 몫). 다음 저장에 그대로 다시 쓴다(ReadSave). 서버에만 있고 복제하지 않는다.
	//   상한을 넘은 몫은 가진 수가 상한 아래로 내려가도 바로 채우지 않는다 - 다음 불러오기 때 같은 판정(ResolveSavedItems)이 다시 넣는다.
	TArray<FVBSavedItem> KeptSavedItems;
};
