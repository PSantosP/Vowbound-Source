// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBShopStateSubsystem.generated.h"

/**
 * 상점 재고의 주인: (상점 ID, 아이템 ID) -> 판 수. 설계 .sw/narrative 3.4절·13.4절.
 *
 * 남은 수가 아니라 판 수를 드는 이유: 남은 수 = 상점 정의의 Stock - 판 수(FVBShopEntry::GetRemaining). 저작자가 재고를 늘리면
 *   옛 슬롯에도 그대로 들어간다. 남은 수를 저장하면 옛 슬롯은 옛 재고에 묶인다.
 * 월드 서브시스템인 이유: 상점 주인 NPC 가 스트리밍으로 내려가도 판 수가 남아야 한다. 상호작용 상태(UVBInteractableSubsystem)와 같은 자리다.
 * 복제하지 않는 이유: 재고를 알아야 하는 클라는 그 상점을 연 사람뿐이고, 그에게는 거래 보기(FVBShopView)로 간다.
 * 쓰기는 서버만(거래 = UVBShopComponent::ServerBuy). 저장 참여자 - 판 수 구역(UVBSaveGame::Shops).
 */
UCLASS()
class VOWBOUND_API UVBShopStateSubsystem : public UWorldSubsystem, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	static UVBShopStateSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	int32 GetSold(FGameplayTag ShopId, FGameplayTag ItemId) const;

	// 서버에서만. 거래가 끝난 뒤 판 수를 늘린다.
	void AddSold(FGameplayTag ShopId, FGameplayTag ItemId, int32 Count);

	//~ IVBSaveParticipant - 세계의 것이라 문맥의 플레이어와 무관하게 통째로 쓰고 읽는다.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

private:
	struct FStockKey
	{
		FGameplayTag ShopId;
		FGameplayTag ItemId;

		bool operator==(const FStockKey& Other) const { return ShopId == Other.ShopId && ItemId == Other.ItemId; }
		friend uint32 GetTypeHash(const FStockKey& Key) { return HashCombine(GetTypeHash(Key.ShopId), GetTypeHash(Key.ItemId)); }
	};

	bool HasWriteAuthority() const;

	// 태그는 UObject 가 아니라 GC 가 볼 것이 없다 - UPROPERTY 가 아니어도 된다.
	TMap<FStockKey, int32> Sold;
};
