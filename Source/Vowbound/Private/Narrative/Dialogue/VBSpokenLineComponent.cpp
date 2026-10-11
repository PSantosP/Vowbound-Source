// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Dialogue/VBSpokenLineComponent.h"

#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Vowbound/Vowbound.h"

UVBSpokenLineComponent::UVBSpokenLineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UVBSpokenLineComponent* UVBSpokenLineComponent::FindFor(const APlayerState* PlayerState)
{
	const APlayerController* Controller = PlayerState ? PlayerState->GetPlayerController() : nullptr;
	return Controller ? Controller->FindComponentByClass<UVBSpokenLineComponent>() : nullptr;
}

float UVBSpokenLineComponent::ResolveSeconds(const FVBSpokenLine& Line) const
{
	if (Line.Duration > 0.0f)
	{
		return Line.Duration;
	}
	return FMath::Max(MinLineSeconds, Line.Text.ToString().Len() * SecondsPerCharacter);
}

void UVBSpokenLineComponent::SpeakLine(const FVBSpokenLine& Line)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		VB_LOG(Warning, "UVBSpokenLineComponent::SpeakLine: 서버에서만 부른다 - 버린다");
		return;
	}
	ClientSpeakLine(Line);
}

void UVBSpokenLineComponent::ClientSpeakLine_Implementation(const FVBSpokenLine& Line)
{
	const float Seconds = ResolveSeconds(Line);
	VB_LOG(Log, "줄: %s (%.1f초)", *Line.Text.ToString(), Seconds);
	if (Line.Voice)
	{
		UGameplayStatics::PlaySound2D(this, Line.Voice);
	}
	OnLineStarted.Broadcast(Line, Seconds);
}
