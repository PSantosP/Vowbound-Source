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

	// Paired animations have their own replication/lifetime. Exactly one producer owns the slots.
	bool AcquireExternalMontageOwnership(UObject* InOwner, uint8 PreviousPlayInstanceId);
	uint8 GetMontagePlayInstanceId() const { return GetRepAnimMontageInfo().PlayInstanceId; }
	void ReleaseExternalMontageOwnership(UObject* InOwner);
	virtual float PlayMontage(UGameplayAbility* AnimatingAbility, FGameplayAbilityActivationInfo ActivationInfo,
		UAnimMontage* Montage, float InPlayRate, FName StartSectionName = NAME_None,
		float StartTimeSeconds = 0.0f) override;
protected:
	virtual void OnRep_ReplicatedAnimMontage() override;
	friend class FVBFinisherLifecycleTest;
	TWeakObjectPtr<UObject> ExternalMontageOwner;
	uint8 SupersededPlayInstanceId = 0;
	bool bHasSupersededMontage = false;
	bool bDeferredExternalMontageRep = false;

	// InitAbilityActorInfo 호출 시 true
	bool bAbilityActorInfoSet = false;
};
