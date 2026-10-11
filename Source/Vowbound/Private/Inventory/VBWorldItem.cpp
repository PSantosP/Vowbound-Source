// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBWorldItem.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interaction/VBInteractableComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBItemSettings.h"
#include "Vowbound/Vowbound.h"

AVBWorldItem::AVBWorldItem()
{
	PrimaryActorTick.bCanEverTick = false;
	// 주웠나(상호작용 대상의 켜짐)가 복제돼야 클라에서도 사라진다.
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionProfileName(TEXT("NoCollision"));
	RootComponent = Mesh;

	Interactable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(Mesh);
}

void AVBWorldItem::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// 모습의 주인은 아이템 정의다 - 놓인 자리마다 메시를 고르지 않는다. 정의에 메시가 없으면 이 액터(BP)의 것을 둔다.
	if (Item && !Item->WorldMesh.IsNull())
	{
		Mesh->SetStaticMesh(Item->WorldMesh.LoadSynchronous());
	}
}

void AVBWorldItem::BeginPlay()
{
	Super::BeginPlay();
	if (!Item)
	{
		VB_LOG(Warning, "%s: 아이템이 비었다 - 주워도 아무것도 안 들어온다", *GetName());
	}
	Interactable->OnInteracted.AddUniqueDynamic(this, &AVBWorldItem::HandleInteracted);
	Interactable->OnActivationChanged.AddUniqueDynamic(this, &AVBWorldItem::HandleActivationChanged);
	// 시작 상태는 신호가 안 온다(OnActivationChanged 주석) - 직접 읽는다.
	ApplyPickedUp(Interactable->IsActivated());
}

void AVBWorldItem::HandleInteracted(AActor* InteractingActor)
{
	// 서버에서만 온다(UVBInteractableComponent::NotifyInteracted).
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(InteractingActor);
	if (!Inventory || !Item || Interactable->IsActivated())
	{
		VB_LOG(Warning, "%s: 줍기 실패 - %s", *GetName(), !Inventory ? TEXT("주운 쪽에 인벤토리가 없다") : TEXT("아이템이 비었거나 이미 주웠다"));
		return;
	}
	const int32 Added = Inventory->AddItem(Item, Count);
	Count -= Added;
	if (Count <= 0)
	{
		// 켜짐 = 주움. 저장·복제·숨기기가 모두 이 한 값에서 나온다.
		Interactable->SetActivated(true);
	}
	else
	{
		// 가득 차서 일부만 들어갔다. 남은 수는 저장되지 않는다(설계 10.3절 - 기록만).
		VB_LOG(Log, "%s: %d 개만 들어갔다 - %d 개 남음", *GetName(), Added, Count);
	}
}

void AVBWorldItem::HandleActivationChanged(bool bActivated, bool bInstant)
{
	ApplyPickedUp(bActivated);
}

void AVBWorldItem::ApplyPickedUp(bool bPickedUp)
{
	SetActorHiddenInGame(bPickedUp);
	if (HasAuthority())
	{
		Interactable->SetInteractionEnabled(!bPickedUp);
	}
}

AVBWorldItem* AVBWorldItem::SpawnDrop(const AActor* Anchor, const UVBItemDefinition* InItem, int32 InCount, int32 SpreadIndex)
{
	UWorld* World = Anchor ? Anchor->GetWorld() : nullptr;
	if (!World || !Anchor->HasAuthority() || !InItem || InCount <= 0)
	{
		VB_LOG(Warning, "떨구기 거절: %s %s x%d", !World ? TEXT("놓을 자리(월드)가 없다") : !Anchor->HasAuthority() ? TEXT("서버가 아니다") : TEXT("아이템이 비었거나 수가 0 이하다"),
		       *GetNameSafe(InItem), InCount);
		return nullptr;
	}
	const UVBItemSettings* Settings = GetDefault<UVBItemSettings>();
	// 이미 메모리에 있으면 경로만 풀고 끝난다(소프트 포인터가 먼저 Get 을 본다). 줍기를 하나도 놓지 않은 맵에서는 첫 떨구기 때 한 번 동기 로드가 일어난다 [비용 안 쟀다].
	//   떨군 줍기에 수명·개수 상한은 없다 - 넘침은 거즈(상한 10)에서만, 병사 드롭 25% 로 생겨 지금은 쌓일 길이 좁다(2026-10-03 커밋 검증 지적, 한계로 둔다).
	UClass* DropClass = Settings->WorldItemClass.LoadSynchronous();
	if (!DropClass)
	{
		VB_LOG(Warning, "떨구기 거절: 프로젝트 설정 Vowbound Items 의 WorldItemClass 가 비었다 - %s x%d 를 버린다", *GetNameSafe(InItem), InCount);
		return nullptr;
	}

	// 바닥 = 고정·움직이는 세계 물체(WorldStatic·WorldDynamic - 움직이는 발판·문 위에서 죽어도 그 위에 놓인다). 사람(Pawn)은 안 본다. 죽은 몸 자신은 뺀다.
	FCollisionObjectQueryParams Floors;
	Floors.AddObjectTypesToQuery(ECC_WorldStatic);
	Floors.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBWorldItemDrop), false, Anchor);
	FHitResult Hit;

	// 첫 줄은 가운데, 다음 줄부터 원 위에 황금각(137.5도)으로 돌려 놓는다 - 몇 줄이 나오든 앞 줄과 겹치지 않는다.
	//   가운데에서 그 자리까지 막히면(벽에 붙어 죽었다) 벌리지 않고 가운데에 놓는다 - 벽 속에서 아래로 쏘면 벽 너머 바닥에 떨어진다.
	const FVector Center = Anchor->GetActorLocation();
	FVector Location = Center;
	if (SpreadIndex > 0)
	{
		const float Angle = FMath::DegreesToRadians(137.5f * SpreadIndex);
		const FVector Spread = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * Settings->DropSpreadRadius;
		if (!World->LineTraceSingleByObjectType(Hit, Center, Spread, Floors, Params))
		{
			Location = Spread;
		}
	}
	// 액터 위치는 캡슐 가운데라 공중이다 - 아래로 바닥을 찾는다.
	if (World->LineTraceSingleByObjectType(Hit, Location, Location - FVector(0.0f, 0.0f, Settings->DropGroundTraceDistance), Floors, Params))
	{
		Location = Hit.ImpactPoint;
	}
	else
	{
		VB_LOG(Warning, "떨구기: %s 아래 %.0fcm 안에 바닥이 없다 - 그 자리(공중)에 놓는다", *Anchor->GetName(), Settings->DropGroundTraceDistance);
	}

	const FTransform Transform(FRotator(0.0f, Anchor->GetActorRotation().Yaw, 0.0f), Location);
	AVBWorldItem* Drop = World->SpawnActorDeferred<AVBWorldItem>(DropClass, Transform, nullptr, nullptr,
	                                                             ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Drop)
	{
		VB_LOG(Warning, "떨구기: %s 를 만들지 못했다 - %s x%d 를 버린다", *DropClass->GetName(), *GetNameSafe(InItem), InCount);
		return nullptr;
	}
	// 지연 스폰 - 아이템·수와 저장 끄기를 OnConstruction(메시)과 BeginPlay(상호작용 등록) 전에 넣는다.
	Drop->Item = InItem;
	Drop->Count = InCount;
	Drop->Interactable->SetSaveState(false);
	Drop->FinishSpawning(Transform);
	// 메시는 기준점이 가운데일 수 있다(BP_WorldItem = 0.2 배 큐브, 기준점 가운데 - 맵에 놓인 줍기는 그래서 바닥 위 10cm 에 있다, 2026-10-03 에디터 리드백).
	//   메시가 정해진 뒤(OnConstruction) 경계의 아랫면을 바닥에 맞춘다 - 아이템마다 메시가 달라도 같은 규칙이다. 충돌이 꺼진 메시라 비충돌 포함으로 잰다.
	const FBox Bounds = Drop->GetComponentsBoundingBox(/*bNonColliding=*/true);
	if (Bounds.IsValid)
	{
		Drop->AddActorWorldOffset(FVector(0.0f, 0.0f, Location.Z - Bounds.Min.Z));
	}
	VB_LOG(Log, "떨구기: %s x%d -> %s (%s 자리)", *InItem->GetName(), InCount, *Drop->GetName(), *Anchor->GetName());
	return Drop;
}
