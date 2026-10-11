#include "Animation/VBLookAtPOIComponent.h"
#include "Animation/VBPlayerAttentionComponent.h"
#include "AIController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBFinisherComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

UVBLookAtPOIComponent::UVBLookAtPOIComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBLookAtPOIComponent::SetLookAtActor(AActor* Target)
{
	TargetActor = Target;
	bHasLocation = false;
}

void UVBLookAtPOIComponent::SetLookAtLocation(FVector WorldLocation)
{
	TargetActor.Reset();
	TargetLocation = WorldLocation;
	bHasLocation = !WorldLocation.ContainsNaN();
}

void UVBLookAtPOIComponent::ClearLookAtTarget()
{
	TargetActor.Reset();
	bHasLocation = false;
}

bool UVBLookAtPOIComponent::ResolveTarget(FVector& OutWorldLocation) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	// 켜짐·캐릭터·메시가 없으면 사실을 모을 수 없다 - 수집의 조기 실패라 정책 밖에서 끝낸다.
	if (!bEnabled || !Character || !Character->GetMesh()) return false;
	// 무엇이 시선을 막는지는 몸 소유 정책 한 곳이 정한다(구조 정리 2단계 1항). 여기는 사실만 모은다.
	//   Dead 는 ASC 의 State.Dead 태그다 - 주시 쪽(IsActorDead)과 출처가 다르고, 바꿔 쓰지 않는다.
	VBBodyControlPolicy::FPlayerLookAtFacts Facts;
	Facts.bMeshSimulatingPhysics = Character->GetMesh()->IsAnySimulatingPhysics();
	if (const UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(GetOwner()))
	{
		Facts.bDead = ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead);
	}
	if (const UVBFinisherComponent* Finisher = Character->FindComponentByClass<UVBFinisherComponent>())
	{
		Facts.bFinisherPlaying = Finisher->IsPlaying();
	}
	if (const UVBPlayerRagdollComponent* Ragdoll = Character->FindComponentByClass<UVBPlayerRagdollComponent>())
	{
		Facts.bPlayerRagdoll = Ragdoll->IsRagdollActive();
	}
	if (const UVBGASPSmartObjectComponent* SmartObject = Character->FindComponentByClass<UVBGASPSmartObjectComponent>())
	{
		Facts.bSmartObjectInteracting = SmartObject->IsInteracting();
	}
	const UVBPlayerAttentionComponent* Attention = Character->FindComponentByClass<UVBPlayerAttentionComponent>();
	Facts.bAttentionSuppressed = Attention && Attention->IsPoseSuppressed();
	if (VBBodyControlPolicy::BlocksPlayerLookAt(Facts)) return false;
	AActor* Target = TargetActor.Get();
	if (!Target && !bHasLocation && Attention && Attention->ResolveAttention(OutWorldLocation, true)) return true;
	if (!Target && !bHasLocation && bUseLockedTarget)
	{
		if (const UVBTargetLockComponent* Lock = Character->FindComponentByClass<UVBTargetLockComponent>()) Target = Lock->GetLockedTarget();
	}
	if (Target && Target != Character)
	{
		if (const ACharacter* TargetCharacter = Cast<ACharacter>(Target); TargetCharacter && TargetCharacter->GetMesh() && TargetCharacter->GetMesh()->DoesSocketExist(TargetSocket))
			OutWorldLocation = TargetCharacter->GetMesh()->GetSocketLocation(TargetSocket);
		else OutWorldLocation = Target->GetActorLocation();
		return !OutWorldLocation.ContainsNaN();
	}
	if (bHasLocation)
	{
		OutWorldLocation = TargetLocation;
		return true;
	}
	if (Attention && Attention->ResolveAttention(OutWorldLocation, false)) return true;
	if (bUseAIFocus)
	{
		if (const AAIController* Controller = Cast<AAIController>(Character->GetController()))
		{
			OutWorldLocation = Controller->GetFocalPoint();
			return FAISystem::IsValidLocation(OutWorldLocation);
		}
	}
	return false;
}
