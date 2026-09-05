// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/Abilities/VBAbilityActivationPolicy.h"
#include "VBGameplayAbility.generated.h"

class UVBAbilitySystemComponent;

/**
 * Vowbound Ability 기초 클래스
 * 모든 GA는 이 클래스를 상속해야한다!
 * CommitAbility 패턴 강제, 활성화 정책 관리
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGameplayAbility : public UGameplayAbility
{
	GENERATED_BODY()
	
public:
	UVBGameplayAbility();
	
	// 활성화 정책 (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Ability")
	EVBAbilityActivationPolicy ActivationPolicy = EVBAbilityActivationPolicy::OnInputTriggered;
	
	// 이 Ability를 트리거하는 Input Tag (Enhanced Input 연동)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Ability")
	FGameplayTag InputTag;
	
	EVBAbilityActivationPolicy GetActivationPolicy() const { return ActivationPolicy; }
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	bool bIsToggleAbility = false;
	
protected:
	// 커스텀 ASC 헬퍼
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;
	
	// AvatarActor를 캐릭터로 가져오는 헬퍼
	ACharacter* GetAvatarCharacter() const;
	
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) {}
	
	virtual void OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) override;
	
	// 히트스톱 재생(전체 월드 시간 조작)
	// RealDuration: 실제 체감 시간(초), TimeDilation: 시간 배율 (0.01 = 거의 정지)
	// 기본 인자를 두지 않는다. 값의 홈은 UVBAttackConfig 하나여야 하는데, 기본 인자는
	//   "config 없이도 호출 가능"을 뜻해 두 번째 홈이 된다. 인자 없이 부르면 컴파일이 실패해야 옳다.
	void PlayHitStop(float RealDuration, float TimeDilation);
	
	// 이 Ability 에서 재생할 AnimMontage (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Animation")
	TObjectPtr<UAnimMontage> AnimMontage;
	
	// 몽타주 재생 속도 (BP에서 조절 가능)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Animation")
	float MontagePlayRate = 1.0f;
	
private:
	// CommitAbility 자동 통과를 강제하기 위해 베이스에서 final 로 잠근다.
	// 파생 GA는 이 함수를 override 하지 말고, ExecuteAbility 를 override 해서 본문 로직을 작성할 것.
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override final;
};
