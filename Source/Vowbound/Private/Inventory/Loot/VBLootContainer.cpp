// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Loot/VBLootContainer.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Loot/VBLootContainerSubsystem.h"
#include "Inventory/Loot/VBLootRules.h"
#include "Inventory/Loot/VBLootTable.h"
#include "Math/RandomStream.h"
#include "Vowbound/Vowbound.h"

void AVBLootContainer::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}
	VB_CLOG(!Table, Warning, "%s: 보상표(Table)가 비었다 - 열어도 아무것도 없다", *GetName());
	// 남은 몫은 «닫았다 다시 열기» 로 받는다. 일회용인데 닫기 동작이 없으면 방향별 저작 대상(bUseSidePlane)에서는 상호작용이 조용히 끝나
	//   남은 몫을 못 받는 서랍장이 된다(UVBGA_Interact 의 Tap 분기). 저작 사고라 말한다.
	VB_CLOG(bOneShot && !Interactable->GetConfig()->GetFrontMotion(/*bActivating=*/false).CharacterMontage, Warning,
	        "%s: 일회용 서랍장인데 설정 %s 에 닫기 동작이 없다 - 남은 몫이 있을 때 닫았다 다시 열 수 없을 수 있다",
	        *GetName(), *GetNameSafe(Interactable->GetConfig()));
	Interactable->OnCompletionApplied.AddUniqueDynamic(this, &AVBLootContainer::HandleCompletionApplied);
	if (UVBLootContainerSubsystem* Contents = UVBLootContainerSubsystem::Get(this))
	{
		ContentsLoadedHandle = Contents->OnContentsLoaded.AddUObject(this, &AVBLootContainer::HandleContentsLoaded);
	}
	// 바탕 BeginPlay 가 켜짐에 맞춰 잠금을 이미 봤다. 그때 남은 몫이 먼저 불러와져 있었으면 거기서 맞았고,
	//   아니면 불러오기 신호(HandleContentsLoaded)가 다시 본다.
}

void AVBLootContainer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBLootContainerSubsystem* Contents = UVBLootContainerSubsystem::Get(this))
	{
		Contents->OnContentsLoaded.Remove(ContentsLoadedHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool AVBLootContainer::IsOneShotSpent() const
{
	// 기록이 없으면 아직 안 굴렸다 - 다 쓰인 것이 아니다. 처음 여는 순간(켜짐이 바뀌어 잠금을 볼 때)은 아직 안 굴린 때다.
	const UVBLootContainerSubsystem* Contents = UVBLootContainerSubsystem::Get(this);
	const TArray<FVBSavedItem>* Remaining = Contents ? Contents->Find(GetContentsKey()) : nullptr;
	return Remaining && Remaining->IsEmpty();
}

void AVBLootContainer::HandleCompletionApplied(AActor* InteractingActor)
{
	// 이번 상호작용이 닫기였으면 주지 않는다 - 바탕 소품이 이미 상태를 뒤집었으므로 지금 켜짐 = 이번이 열기.
	if (!HasAuthority() || !Interactable->IsActivated())
	{
		return;
	}
	UVBLootContainerSubsystem* Contents = UVBLootContainerSubsystem::Get(this);
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(InteractingActor);
	if (!Contents || !Inventory)
	{
		VB_LOG(Warning, "%s: 내용물을 못 준다 - %s", *GetName(), !Contents ? TEXT("남은 몫 서브시스템이 없다") : TEXT("연 쪽에 인벤토리가 없다"));
		return;
	}

	const FName Key = GetContentsKey();
	TArray<FVBSavedItem> Remaining;
	if (const TArray<FVBSavedItem>* Found = Contents->Find(Key))
	{
		Remaining = *Found;
	}
	else if (Table)
	{
		// 처음 열기 - 한 번만 굴린다. 이 결과가 기록되므로 저장·이어하기 뒤에도 다시 굴리지 않는다.
		const APawn* Pawn = Cast<APawn>(InteractingActor);
		FVBRuleContext Context = FVBRuleContext::Make(this, Pawn ? Pawn->GetPlayerState() : nullptr);
		Context.Counterpart = this;
		FRandomStream Stream(FMath::Rand());
		for (const FVBLootDrop& Drop : VBLoot::Roll(*Table, Context, Stream))
		{
			// ItemId 가 빈 아이템은 저장 줄로 못 적는다(아이템 자산 검증이 잡는 저작 사고) - 말하고 뺀다.
			VB_CLOG(!Drop.Item->ItemId.IsValid(), Warning, "%s: %s 에 ItemId 가 없어 남은 몫으로 적지 못한다 - 뺀다", *GetName(), *Drop.Item->GetName());
			if (Drop.Item->ItemId.IsValid())
			{
				FVBSavedItem& Row = Remaining.AddDefaulted_GetRef();
				Row.ItemId = Drop.Item->ItemId;
				Row.Count = Drop.Count;
			}
		}
	}

	const int32 Before = Remaining.Num();
	const int32 Given = VBLoot::GiveStacks(Remaining, [Inventory, this](const FVBSavedItem& Row)
	{
		const UVBItemDefinition* Item = UVBItemDefinition::FindById(Row.ItemId);
		VB_CLOG(!Item, Warning, "%s: 남은 몫의 %s 를 되찾지 못했다 - 그 줄은 남긴다", *GetName(), *Row.ItemId.ToString());
		return Item ? Inventory->AddItem(Item, Row.Count) : 0;
	});
	Contents->SetRemaining(Key, Remaining);
	VB_LOG(Log, "%s: 내용물 %d 개를 줬다 - 남은 줄 %d/%d", *GetName(), Given, Remaining.Num(), Before);
	RefreshOneShotLock();
}

void AVBLootContainer::HandleContentsLoaded()
{
	RefreshOneShotLock();
}

FName AVBLootContainer::GetContentsKey() const
{
	const FName SaveKey = Interactable->GetSaveKey();
	return SaveKey.IsNone() ? GetFName() : SaveKey;
}
