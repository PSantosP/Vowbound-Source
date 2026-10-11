// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBWeaponPickup.h"

#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBInteractableComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Vowbound/Vowbound.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBWeaponPickup"

AVBWeaponPickup::AVBWeaponPickup()
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

void AVBWeaponPickup::BeginPlay()
{
	Super::BeginPlay();
	Interactable->OnInteracted.AddUniqueDynamic(this, &AVBWeaponPickup::HandleInteracted);
	Interactable->OnActivationChanged.AddUniqueDynamic(this, &AVBWeaponPickup::HandleActivationChanged);
	// 시작 상태는 신호가 안 온다(UVBInteractableComponent::OnActivationChanged 주석) - 직접 읽는다.
	ApplyPickedUp(Interactable->IsActivated());
}

void AVBWeaponPickup::HandleInteracted(AActor* InteractingActor)
{
	// 서버에서만 온다(UVBInteractableComponent::NotifyInteracted).
	if (Interactable->IsActivated())
	{
		return;
	}
	AVBCharacter* Character = Cast<AVBCharacter>(InteractingActor);
	UVBWeaponStateComponent* Weapons = Character ? Character->GetWeaponStateComponent() : nullptr;
	if (!Weapons)
	{
		VB_LOG(Warning, "%s: 무기 줍기 실패 - 집은 쪽에 무기 상태가 없다(%s)", *GetName(), *GetNameSafe(InteractingActor));
		return;
	}

	const EVBWeaponGrantResult Result = Weapons->GrantWeapon(Weapon);
	if (Result == EVBWeaponGrantResult::Failed)
	{
		// 소진하지 않는다 - 다시 집을 수 있다. 이유는 GrantWeapon 이 경고했다.
		return;
	}

	// 소진 먼저. 효과가 저장(체크포인트)을 걸면 그 저장에 «주움» 이 들어가야 이어하기 뒤 다시 안 나온다.
	Interactable->SetActivated(true);

	if (Result == EVBWeaponGrantResult::NewlyOwned && !AcquiredEffects.IsEmpty())
	{
		FVBRuleContext Context = FVBRuleContext::Make(this, Character->GetPlayerState());
		Context.Counterpart = this;
		VBRules::ApplyAll(AcquiredEffects, Context);
	}
	VB_LOG(Log, "%s: 무기 줍기 - %s", *GetName(),
	       Result == EVBWeaponGrantResult::NewlyOwned ? TEXT("새로 얻음, 획득 효과 적용") : TEXT("이미 가짐, 효과 없음"));
}

void AVBWeaponPickup::HandleActivationChanged(bool bActivated, bool bInstant)
{
	ApplyPickedUp(bActivated);
}

void AVBWeaponPickup::ApplyPickedUp(bool bPickedUp)
{
	SetActorHiddenInGame(bPickedUp);
	if (HasAuthority())
	{
		Interactable->SetInteractionEnabled(!bPickedUp);
	}
}

#if WITH_EDITOR
EDataValidationResult AVBWeaponPickup::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (Weapon == EVBWeaponType::None)
	{
		Context.AddError(LOCTEXT("NoWeapon", "무기가 None 이다 - 집어도 아무것도 안 가진다."));
		Result = EDataValidationResult::Invalid;
	}
	if (!VBRules::ValidateStructArray(AcquiredEffects, FVBEffect::StaticStruct(), LOCTEXT("AcquiredWhere", "획득 효과"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
