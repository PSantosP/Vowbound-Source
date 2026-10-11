// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBEquipmentComponent.generated.h"

class APlayerState;
class UAbilitySystemComponent;
class UGameplayEffect;
class UVBItemDefinition;

DECLARE_MULTICAST_DELEGATE(FVBOnEquipmentChanged);

/**
 * 입은 장비의 주인. AVBPlayerState 에 붙는다 - 인벤토리·ASC 와 같은 자리라 폰이 죽고 다시 나도 장비와 효과가 남는다.
 * 설계 .sw/remaining-features/b1-equipment.md.
 *
 * 장착한 것도 인벤토리 수에 남는다(가진 것을 «입은» 것). 가진 수가 0 이 되면(버리기·팔기) 그 칸을 저절로 벗긴다.
 * 효과 = GE 하나(EquipmentEffect)를 칸마다 걸고, 아이템의 능력치 지도(FVBItemFragment_Equippable::Stats)를 SetByCaller 로 싣는다.
 *   GE 의 SetByCaller 중 아이템 지도에 없는 것은 0 - 빠진 태그 경고 없이 무변화. 지도에 있는데 GE 에 없는 능력치는 경고한다(조용히 버려지지 않게).
 * 쓰기는 서버만 한다. 클라(표시)는 요청만 보낸다. 칸 판정은 VBEquipment(순수) 하나다 - 요청과 불러오기가 같이 탄다.
 * ASC 가 없으면 입히지 않는다 - 효과 없는 장착은 거짓 상태다.
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBEquipmentComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBEquipmentComponent();

	// 폰·컨트롤러·PlayerState 어느 쪽을 줘도 그 플레이어의 장비. 없으면 nullptr.
	static UVBEquipmentComponent* FindFor(const AActor* Actor);

	const TArray<FGameplayTag>& GetSlotLayout() const { return SlotLayout; }
	const UVBItemDefinition* GetEquipped(int32 SlotIndex) const { return Equipped.IsValidIndex(SlotIndex) ? Equipped[SlotIndex].Get() : nullptr; }
	int32 FindSlotOf(const UVBItemDefinition* Item) const;

	// 표시가 부른다. SlotIndex 가 음수면 서버가 칸을 고른다(맞는 빈 칸 먼저).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Item")
	void RequestEquip(const UVBItemDefinition* Item, int32 SlotIndex = -1);

	UFUNCTION(BlueprintCallable, Category="Vowbound|Item")
	void RequestUnequip(int32 SlotIndex);

	// 서버에서만. 입었으면(또는 이미 그 칸이면) true.
	bool Equip(const UVBItemDefinition* Item, int32 SlotIndex);
	bool Unequip(int32 SlotIndex);

	// 서버는 바꾼 직후, 클라는 복제가 도착했을 때.
	FVBOnEquipmentChanged OnEquipmentChanged;

	//~ IVBSaveParticipant - 장비 구역(UVBSaveGame::Equipment). 문맥의 PlayerState 가 주인일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 칸 배치 - 칸마다 칸 종류. 기본 = 머리·몸·팔·장신구·장신구(user 결정 2026-10-06). BP 기본값이라 빌드 없이 바뀐다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Item", meta=(Categories="Equipment.Slot"))
	TArray<FGameplayTag> SlotLayout;

	// 칸마다 거는 효과(무한, 능력치마다 SetByCaller Data.Stat.* 덧셈 - 스킬 트리와 같은 GE). 비면 아무것도 입히지 않고 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Item")
	TSubclassOf<UGameplayEffect> EquipmentEffect;

private:
	// 칸마다 입은 아이템(빈 칸 = nullptr). 그 플레이어만 안다.
	UPROPERTY(ReplicatedUsing=OnRep_Equipped)
	TArray<TObjectPtr<const UVBItemDefinition>> Equipped;

	UFUNCTION()
	void OnRep_Equipped();

	UFUNCTION(Server, Reliable)
	void ServerEquip(const UVBItemDefinition* Item, int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void ServerUnequip(int32 SlotIndex);

	// 서버 전용. 칸마다 건 효과의 핸들.
	TArray<FActiveGameplayEffectHandle> EffectHandles;

	bool ApplyEffect(int32 SlotIndex);
	void RemoveEffect(int32 SlotIndex);
	void RemoveAllEffects();
	void HandleInventoryChanged();
	void HandleSaveApplied();
	void BroadcastEvent(const FGameplayTag& EventTag, const UVBItemDefinition* Item) const;
	bool HasWriteAuthority(const TCHAR* What) const;
	APlayerState* GetOwningPlayerState() const;
	UAbilitySystemComponent* GetAbilitySystem() const;

	FDelegateHandle InventoryChangedHandle;

	// 저장을 읽히는 동안에는 소유 판정을 미뤘다가 다 읽힌 뒤(OnSaveApplied) 한 번 한다 - UVBSaveParticipantSubsystem::IsApplyingSave 주석의 규약.
	TWeakObjectPtr<class UVBSaveParticipantSubsystem> SaveRegistry;
	FDelegateHandle SaveAppliedHandle;
};
