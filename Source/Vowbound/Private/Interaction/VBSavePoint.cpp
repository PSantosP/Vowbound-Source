// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBSavePoint.h"

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Vowbound/Vowbound.h"

AVBSavePoint::AVBSavePoint()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Marker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Marker"));
	Marker->SetupAttachment(RootComponent);

	Interactable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(RootComponent);
}

void AVBSavePoint::BeginPlay()
{
	Super::BeginPlay();

	if (Interactable->GetConfig()->Mode != EVBInteractMode::Auto)
	{
		VB_LOG(Warning, "%s: 설정 Mode 가 Auto 가 아니다 - 다가가도 저장되지 않고 눌러야 저장된다", *GetName());
	}
	if (HasAuthority())
	{
		Interactable->OnInteracted.AddUniqueDynamic(this, &AVBSavePoint::HandleInteracted);
	}
}

void AVBSavePoint::HandleInteracted(AActor* InteractingActor)
{
	APawn* Pawn = Cast<APawn>(InteractingActor);
	UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this);
	if (!Pawn || !Save)
	{
		VB_LOG(Warning, "%s: 저장 못 함 - 폰 %s, 저장 서브시스템 %s", *GetName(),
		       Pawn ? TEXT("있음") : TEXT("없음"), Save ? TEXT("있음") : TEXT("없음"));
		return;
	}
	VB_LOG(Log, "%s: 저장 포인트 - 자동 저장", *GetName());
	Save->SaveCurrentGame(Pawn);
}
