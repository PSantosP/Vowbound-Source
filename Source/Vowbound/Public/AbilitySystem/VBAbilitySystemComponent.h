// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "VBAbilitySystemComponent.generated.h"

/*
 * Vowbound 전용 커스텀 ASC
 * 역할: Ability 부여/활성화/이벤트 관리
 * 소유: AVBPlayerState (플레이어), AActor (NPC)
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBAbilitySystemComponent : public UAbilitySystemComponent
{
	GENERATED_BODY()

public:
	UVBAbilitySystemComponent();
	
	// ASC 초기화 여부
	bool IsAbilityActorInfoSet() const { return bAbilityActorInfoSet; }

	// ActorInfo 설정 시 플래그 업데이트
	virtual void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
protected:
	// InitAbilityActorInfo 호출 시 true
	bool bAbilityActorInfoSet = false;
};
