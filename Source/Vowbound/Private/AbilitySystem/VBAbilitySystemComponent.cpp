// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/VBAbilitySystemComponent.h"


UVBAbilitySystemComponent::UVBAbilitySystemComponent()
{
	// Mixed 모드 : GE는 서버->오너만, Cue/Tag는 모든 클라이언트
	// 생성자(CDO 생성 시점)에선 SetIsReplicated 가 ensure 발화 — ByDefault 가 공식 경로 (ActorComponent.cpp::UActorComponent::SetIsReplicated)
	SetIsReplicatedByDefault(true);
	UAbilitySystemComponent::SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void UVBAbilitySystemComponent::InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor)
{
	Super::InitAbilityActorInfo(InOwnerActor, InAvatarActor);

	// 초기화 완료
	bAbilityActorInfoSet = true;
}

