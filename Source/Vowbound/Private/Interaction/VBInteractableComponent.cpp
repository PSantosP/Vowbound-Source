// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractableComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Interaction/VBInteractableConfig.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Interaction/VBInteractableSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Vowbound/Vowbound.h"

UVBInteractableComponent::UVBInteractableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

const UVBInteractableConfig* UVBInteractableComponent::GetConfig() const
{
	return Config ? Config.Get() : GetDefault<UVBInteractableConfig>();
}

void UVBInteractableComponent::SetInteractionEnabled(bool bEnabled)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	bInteractionEnabled = bEnabled;
}

FText UVBInteractableComponent::GetActionText() const
{
	return ActionTextOverride.IsEmpty() ? GetConfig()->ActionText : ActionTextOverride;
}

void UVBInteractableComponent::SetActionTextOverride(const FText& InText)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	ActionTextOverride = InText;
}

void UVBInteractableComponent::SetActivated(bool bNewActivated, bool bInstant)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bActivated == bNewActivated)
	{
		return;
	}
	bActivated = bNewActivated;
	// OnRep 은 권한에서 안 불린다. 싱글은 리슨 호스트라 손으로 알리지 않으면 소품이 안 움직인다.
	OnActivationChanged.Broadcast(bActivated, bInstant);
}

void UVBInteractableComponent::OnRep_Activated()
{
	OnActivationChanged.Broadcast(bActivated, /*bInstant=*/false);
}

FName UVBInteractableComponent::GetSaveKey() const
{
	if (!bSaveState || !GetOwner())
	{
		return NAME_None;
	}
	return SaveId.IsNone() ? GetOwner()->GetFName() : SaveId;
}

FVBRuleContext UVBInteractableComponent::MakeRuleContext(const AActor* InteractingActor) const
{
	const APawn* Pawn = Cast<APawn>(InteractingActor);
	FVBRuleContext Context = FVBRuleContext::Make(this, Pawn ? Pawn->GetPlayerState() : nullptr);
	Context.Counterpart = GetOwner();
	return Context;
}

bool UVBInteractableComponent::PassesUseConditions(const AActor* InteractingActor) const
{
	return UseConditions.IsEmpty() || VBRules::EvaluateAll(UseConditions, MakeRuleContext(InteractingActor));
}

void UVBInteractableComponent::ApplyRefused(AActor* InteractingActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	VB_LOG(Log, "상호작용 거절(조건 거짓): %s.%s <- %s", *GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(InteractingActor));
	if (!RefusedEffects.IsEmpty())
	{
		VBRules::ApplyAll(RefusedEffects, MakeRuleContext(InteractingActor));
	}
}

bool UVBInteractableComponent::NotifyInteracted(AActor* InteractingActor)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	// 완료 직전에 조건을 다시 본다(.sw/prologue-opening 14.2). 완료 길 넷(탭 즉시·동작 확정·홀드·자동)이 모두 여기로 모여서
	//   이 한 자리가 넷을 다 막는다. 시작 때 참이었어도 동작 도중에 거짓이 됐으면 완료 신호·사건·효과를 내지 않는다.
	if (!PassesUseConditions(InteractingActor))
	{
		VB_LOG(Log, "상호작용 완료 거절(조건 거짓): %s.%s <- %s", *GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(InteractingActor));
		return false;
	}
	VB_LOG(Log, "상호작용 완료: %s.%s <- %s", *GetNameSafe(GetOwner()), *GetName(), *GetNameSafe(InteractingActor));
	OnInteracted.Broadcast(InteractingActor);

	const APawn* Pawn = Cast<APawn>(InteractingActor);
	APlayerState* InstigatorState = Pawn ? Pawn->GetPlayerState() : nullptr;

	// 퀘스트가 들을 사건(.sw/narrative 12.4). 표지가 없는 대상(대부분의 문·스위치)은 보내지 않는다.
	if (!EventTags.IsEmpty())
	{
		if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
		{
			FVBGameEvent Event;
			Event.EventTag = VBGameplayTags::Event_Interaction_Completed;
			Event.TargetTags = EventTags;
			Event.Instigator = InstigatorState;
			Events->Broadcast(Event);
		}
	}

	// 완료 효과(.sw/narrative 14.4)는 사건 «뒤» 다 - «찬장을 열어라 -> 안의 약을 구하라» 에서 효과가 낸 아이템 사건이 먼저 가면
	//   둘째 단계가 서기 전에 지나가 버린다.
	if (!CompletionEffects.IsEmpty())
	{
		VBRules::ApplyAll(CompletionEffects, MakeRuleContext(InteractingActor));
	}

	OnCompletionApplied.Broadcast(InteractingActor);
	return true;
}

void UVBInteractableComponent::BeginPlay()
{
	Super::BeginPlay();
	// 선택적 컴포넌트가 비활성/미설정이면 후보 등록도 하지 않는다.
	if (!Config && !bInteractionEnabled) return;

	// 미할당은 동작한다(CDO 기본값) - 그래서 조용히 넘어가면 «문이 왜 다른 문과 다르게 굴지» 로만 보인다.
	if (!Config)
	{
		VB_LOG(Warning, "상호작용 대상 %s.%s 에 Config 가 없다 - 기본값(탭, 200cm, 문구 없음)으로 동작한다",
		       *GetNameSafe(GetOwner()), *GetName());
	}

	if (UVBInteractableSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UVBInteractableSubsystem>() : nullptr)
	{
		Subsystem->Register(this);
	}
}

void UVBInteractableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBInteractableSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UVBInteractableSubsystem>() : nullptr)
	{
		Subsystem->Unregister(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UVBInteractableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UVBInteractableComponent, bInteractionEnabled);
	DOREPLIFETIME(UVBInteractableComponent, ActionTextOverride);
	DOREPLIFETIME(UVBInteractableComponent, bActivated);
}
