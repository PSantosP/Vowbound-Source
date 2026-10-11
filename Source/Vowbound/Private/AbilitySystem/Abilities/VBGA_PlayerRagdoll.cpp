// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#include "AbilitySystem/Abilities/VBGA_PlayerRagdoll.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Vowbound/Vowbound.h" // VB_LOG

UVBGA_PlayerRagdoll::UVBGA_PlayerRagdoll()
{
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = UVBPlayerRagdollComponent::GetRecoveryEventTag();
	AbilityTriggers.Add(Trigger);
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_GettingUp);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
}

void UVBGA_PlayerRagdoll::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	USkeletalMeshComponent* Mesh = ActorInfo ? ActorInfo->SkeletalMeshComponent.Get() : nullptr;
	UVBPlayerRagdollComponent* Ragdoll = Avatar ? Avatar->FindComponentByClass<UVBPlayerRagdollComponent>() : nullptr;
	UAnimMontage* Montage = TriggerEventData ? const_cast<UAnimMontage*>(Cast<UAnimMontage>(TriggerEventData->OptionalObject.Get())) : nullptr;
	const float StartTime = TriggerEventData ? TriggerEventData->EventMagnitude : -1.0f;
	if (!Ragdoll || !Mesh || !Mesh->GetSkeletalMeshAsset() || !Montage ||
		Montage->GetSkeleton() != Mesh->GetSkeletalMeshAsset()->GetSkeleton() ||
		!FMath::IsFinite(StartTime) || StartTime < 0.0f || StartTime >= Montage->GetPlayLength() ||
		!Ragdoll->PrepareForGetUp())
	{
		VB_LOG(Error, "Player GASP recovery rejected: %s montage=%s start=%.3f",
			*GetNameSafe(Avatar), *GetNameSafe(Montage), StartTime);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, 1.0f, NAME_None, true, 1.0f, StartTime, true);
	Task->OnCompleted.AddDynamic(this, &ThisClass::OnMontageEnded);
	Task->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageEnded);
	Task->OnCancelled.AddDynamic(this, &ThisClass::OnMontageEnded);
	Task->ReadyForActivation();
	if (!IsActive()) return;
	UAnimInstance* Anim = ActorInfo->GetAnimInstance();
	if (!Anim || !Anim->Montage_IsPlaying(Montage))
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
}

void UVBGA_PlayerRagdoll::OnMontageEnded()
{
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UVBGA_PlayerRagdoll::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive()) return;
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
	if (UVBPlayerRagdollComponent* Ragdoll = Avatar ? Avatar->FindComponentByClass<UVBPlayerRagdollComponent>() : nullptr)
		Ragdoll->FinishRecovery();
}
