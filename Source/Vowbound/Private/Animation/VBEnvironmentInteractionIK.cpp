#include "Animation/VBEnvironmentInteractionIK.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBInteractionIK.h"
#include "Components/SkeletalMeshComponent.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBToggleProp.h"
#include "Interaction/VBPushableComponent.h"
#include "Interaction/VBPushableConfig.h"

void VBEnvironmentInteractionIK::Update(UAnimInstance* AnimInstance, FVBInteractionIKRuntime& InOutRuntime)
{
	check(IsInGameThread());
	if (!AnimInstance || InOutRuntime.bEnabled) return;
	const AActor* Owner = AnimInstance->GetOwningActor();
	const UVBInteractorComponent* Interactor = Owner ? Owner->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	const UVBInteractableComponent* Target = Interactor ? Interactor->GetMotionTarget() : nullptr;
	if (const UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target))
	{
		const UVBPushableConfig* Settings = Pushable->GetPushConfig();
		UAnimMontage* Montage = Settings ? Settings->ActivateMotion.CharacterMontage.Get() : nullptr;
		const FName Section = Montage && AnimInstance->Montage_IsPlaying(Montage)
			? AnimInstance->Montage_GetCurrentSection(Montage) : NAME_None;
		if (InOutRuntime.PreviousPushMontage.Get() == Montage && !Section.IsNone()
			&& !InOutRuntime.PreviousPushSection.IsNone() && Section != InOutRuntime.PreviousPushSection)
		{
			AnimInstance->RequestSlotGroupInertialization(Montage->GetGroupName(), Settings->SectionBlendTime);
		}
		InOutRuntime.PreviousPushMontage = Montage;
		InOutRuntime.PreviousPushSection = Section;
		FTransform Left, Right;
		float Weight = 0.0f;
		if (Pushable->GetHandTargets(AnimInstance, Left, Right, Weight))
		{
			const FTransform MeshWorld = AnimInstance->GetOwningComponent()->GetComponentTransform();
			InOutRuntime.Left.Target = Left.GetRelativeTransform(MeshWorld);
			InOutRuntime.Right.Target = Right.GetRelativeTransform(MeshWorld);
			InOutRuntime.Left.Alpha = InOutRuntime.Right.Alpha = Weight;
			InOutRuntime.bEnabled = true;
		}
		return;
	}
	InOutRuntime.PreviousPushMontage.Reset();
	InOutRuntime.PreviousPushSection = NAME_None;
	const AVBToggleProp* Prop = IsValid(Target) ? Cast<AVBToggleProp>(Target->GetOwner()) : nullptr;
	const FVBInteractionMotion* Motion = Interactor ? Interactor->GetActiveMotion() : nullptr;
	const USkeletalMeshComponent* Mesh = AnimInstance->GetOwningComponent();
	if (!Prop || !Motion || !Mesh || !Motion->CharacterMontage
		|| !AnimInstance->Montage_IsPlaying(Motion->CharacterMontage)) return;
	const float Time = AnimInstance->Montage_GetPosition(Motion->CharacterMontage);
	const FTransform MeshToWorld = Mesh->GetComponentTransform();
	auto FillHand = [&](const FVBEnvironmentHandContact& Contact, FVBInteractionIKHand& Hand)
	{
		const float Weight = Contact.GetWeight(Time);
		FTransform BoneToWorld;
		if (Weight <= 0.0f || !Prop->GetInteractionBoneTransform(Contact.PropBone, BoneToWorld)) return;
		const FTransform WorldTarget = Contact.HandTarget * BoneToWorld;
		if (WorldTarget.ContainsNaN()) return;
		Hand.Target = WorldTarget.GetRelativeTransform(MeshToWorld);
		Hand.Alpha = Weight;
	};
	FillHand(Motion->LeftContact, InOutRuntime.Left);
	FillHand(Motion->RightContact, InOutRuntime.Right);
	InOutRuntime.bEnabled = InOutRuntime.Left.Alpha > 0.0f || InOutRuntime.Right.Alpha > 0.0f;
}
