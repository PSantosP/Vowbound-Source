// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Animation/VBANS_ComboWindow.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Vowbound/Vowbound.h"

UVBANS_ComboWindow::UVBANS_ComboWindow()
{
}

void UVBANS_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	if (!MeshComp)
	{
		return;
	}
	
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
	{
		return;
	}
	
	UAbilitySystemComponent* ASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OwnerActor);
	
	if (!ASC)
	{
		return;
	}
	
	FGameplayEventData EventData;
	EventData.EventTag = VBGameplayTags::Event_Combo_WindowOpen;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		OwnerActor,
		VBGameplayTags::Event_Combo_WindowOpen,
		EventData);
	
	VB_LOG(Log, "AN_ComboWindow: 공격 콤보 윈도우 오픈 (%s)", *OwnerActor->GetName());
}

void UVBANS_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	
	if (!MeshComp)
	{
		return;
	}
	
	AActor* OwnerActor = MeshComp->GetOwner();
	if (!OwnerActor)
	{
		return;
	}
	
	UAbilitySystemComponent* ASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OwnerActor);
	
	if (!ASC)
	{
		return;
	}
	
	FGameplayEventData EventData;
	EventData.EventTag = VBGameplayTags::Event_Combo_WindowClose;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
		OwnerActor,
		VBGameplayTags::Event_Combo_WindowClose,
		EventData);
	
	VB_LOG(Log, "AN_ComboWindow: 공격 콤보 윈도우 닫기 (%s)", *OwnerActor->GetName());
}

FString UVBANS_ComboWindow::GetNotifyName_Implementation() const
{
	return TEXT("Combo Window");
}
