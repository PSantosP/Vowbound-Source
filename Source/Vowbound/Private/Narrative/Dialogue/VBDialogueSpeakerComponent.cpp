// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Dialogue/VBDialogueSpeakerComponent.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/VBInteractableComponent.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Vowbound/Vowbound.h"

UVBDialogueSpeakerComponent::UVBDialogueSpeakerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBDialogueSpeakerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!Dialogue)
	{
		VB_LOG(Warning, "%s: 대화 자산이 비었다 - 말을 걸어도 아무것도 안 열린다", *GetNameSafe(GetOwner()));
	}
	TArray<UVBInteractableComponent*> Interactables;
	GetOwner()->GetComponents(Interactables);
	if (Interactables.IsEmpty())
	{
		VB_LOG(Warning, "%s: 상호작용 대상(UVBInteractableComponent)이 없다 - 말을 걸 길이 없다", *GetNameSafe(GetOwner()));
	}
	for (UVBInteractableComponent* Interactable : Interactables)
	{
		Interactable->OnInteracted.AddUniqueDynamic(this, &UVBDialogueSpeakerComponent::HandleInteracted);
	}
}

void UVBDialogueSpeakerComponent::HandleInteracted(AActor* InteractingActor)
{
	// 서버에서만 온다(UVBInteractableComponent::NotifyInteracted).
	UVBDialogueComponent* DialogueComponent = UVBDialogueComponent::FindFor(InteractingActor);
	if (!DialogueComponent)
	{
		VB_LOG(Warning, "%s: 말을 건 쪽(%s)에 대화 컴포넌트가 없다(플레이어 컨트롤러에 UVBDialogueComponent)",
		       *GetNameSafe(GetOwner()), *GetNameSafe(InteractingActor));
		return;
	}
	const APawn* Pawn = Cast<APawn>(InteractingActor);
	const FVBRuleContext Context = FVBRuleContext::Make(this, Pawn ? Pawn->GetPlayerState() : nullptr);
	if (!VBRules::EvaluateAll(OpenConditions, Context))
	{
		VB_LOG(Log, "%s: 여는 조건이 거짓 - 지금은 말을 받지 않는다", *GetNameSafe(GetOwner()));
		return;
	}
	DialogueComponent->StartDialogue(Dialogue, GetOwner());
}
