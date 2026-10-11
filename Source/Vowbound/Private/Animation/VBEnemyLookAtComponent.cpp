#include "Animation/VBEnemyLookAtComponent.h"

#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBFinisherComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Enemy/VBEnemyBase.h"
#include "Net/UnrealNetwork.h"

UVBEnemyLookAtComponent::UVBEnemyLookAtComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickInterval = 0.1f;
	bUseLockedTarget = false;
	bUseAIFocus = false; // Only replicated gameplay focus; a MoveTo destination is not a gaze target.
	SetIsReplicatedByDefault(true);
}

void UVBEnemyLookAtComponent::BeginPlay()
{
	Super::BeginPlay();
	const bool bAuthority = GetOwner() && GetOwner()->HasAuthority();
	SetComponentTickEnabled(bAuthority);
	if (bAuthority) UpdateFocusActor();
}

void UVBEnemyLookAtComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (GetOwner() && GetOwner()->HasAuthority()) UpdateFocusActor();
}

void UVBEnemyLookAtComponent::UpdateFocusActor()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
	AActor* NewFocus = AI ? AI->GetFocusActorForPriority(EAIFocusPriority::Gameplay) : nullptr;
	if (!IsValid(NewFocus) || UVBAbilitySystemStatics::IsActorDead(NewFocus)) NewFocus = nullptr;
	if (FocusActor != NewFocus)
	{
		FocusActor = NewFocus;
		OnRep_FocusActor();
	}
}

void UVBEnemyLookAtComponent::OnRep_FocusActor()
{
	if (IsValid(FocusActor)) SetLookAtActor(FocusActor);
	else ClearLookAtTarget();
}

bool UVBEnemyLookAtComponent::ResolveTarget(FVector& OutWorldLocation) const
{
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner());
	if (!Enemy) return false;
	// 적 전용 차단은 몸 소유 정책 한 곳이 정한다(구조 정리 2단계 1항). 아래의 대상 사망 검사와 부모 POI 의 차단은
	//   정책 밖에 그대로 이어진다 - 이 판정을 적 시선 전체의 마지막 문으로 쓰지 않는다.
	VBBodyControlPolicy::FEnemyLookAtFacts Facts;
	Facts.bDead = Enemy->IsDead();
	Facts.bRecoverableRagdoll = Enemy->IsRecoverableRagdollActive();
	Facts.bFinisherScene = UVBFinisherComponent::IsActorInScene(Enemy);
	if (const UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
	{
		Facts.bStaggered = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Staggered);
		Facts.bDowned = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Downed);
		Facts.bHitReacting = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_HitReacting);
		Facts.bAttacking = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
	}
	if (VBBodyControlPolicy::BlocksEnemyLookAt(Facts)) return false;
	if (IsValid(FocusActor) && UVBAbilitySystemStatics::IsActorDead(FocusActor)) return false;
	return Super::ResolveTarget(OutWorldLocation);
}

void UVBEnemyLookAtComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBEnemyLookAtComponent, FocusActor);
}
