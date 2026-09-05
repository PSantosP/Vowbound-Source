// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_MedicalAbilityBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "GameFramework/Character.h"
#include "Vowbound/Vowbound.h"


void UVBGA_MedicalAbilityBase::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                               const FGameplayAbilityActivationInfo ActivationInfo,
                                               const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo()))
	{
		VBChar->NotifyCombatStimulus(); // 의료 어빌리티도 발도 없는 전투 자극 (DEC-006 ①)
	}
	
	// LineTrace로 타겟 탐색 + GE 적용
	PerformMedicalTrace(ActorInfo);

	// 어빌리티 종료
	// 추후 몽타주 재생 시 -> MontageEnd에서 EndAbility
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

void UVBGA_MedicalAbilityBase::PerformMedicalTrace(const FGameplayAbilityActorInfo* ActorInfo)
{
	// [서버 권위] GE 적용(즉사 데미지 + 명성 변동)은 서버에서만. 베이스가 ServerInitiated 라
	// 소유 클라에서도 ExecuteAbility→여기로 들어오는데, 가드 없으면 listen-server host 에서
	// 이중 적용 위험. 형제 GA(MeleeAttackBase/Dodge)와 동일 규약 (verify 2026-06-04 지적).
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	ACharacter* AvatarCharacter = GetAvatarCharacter();
	if (!AvatarCharacter || !MedicalConfig || !MedicalConfig->MedicalDamageEffectClass)
	{
		return;
	}

	// LineTrace 설정 (단일 대상)
	const FVector Start = AvatarCharacter->GetActorLocation() + DefaultTraceHeightOffset; // 캐릭터 중심 높이 보정
	
	const FVector Forward = AvatarCharacter->GetActorForwardVector();
	const FVector End = Start + (Forward * MedicalConfig->MedicalRange);
	
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(AvatarCharacter);
	
	FHitResult HitResult;
	const bool bHit = AvatarCharacter->GetWorld()->LineTraceSingleByChannel(
		HitResult, Start, End, MedicalConfig->HitTraceChannel, QueryParams);
	
#if ENABLE_DRAW_DEBUG
	DrawDebugLine(AvatarCharacter->GetWorld(), Start, End,
		bHit ? FColor::Green : FColor::Yellow, false, 1.0f);
	
	if (bHit)
	{
		DrawDebugSphere(AvatarCharacter->GetWorld(), HitResult.ImpactPoint,
			20.0f, 8, FColor::Green, false, 1.0f);
		
	}
#endif
	
	if (!bHit || !HitResult.GetActor())
	{
		VB_LOG(Log, "%s: 타겟 없음", *GetName());
		return;
	}
	
	AActor* HitActor = HitResult.GetActor();
	// 대상 ASC 확인
	UAbilitySystemComponent* TargetASC =
		UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	
	if (!TargetASC)
	{
		VB_LOG(Log, "%s : %s에 ASC 없음", *GetName(), *HitActor->GetName());
		return;
	}
	
	// 빈사 상태 확인
	if (!IsTargetLowHealth(TargetASC))
	{
		VB_LOG(Log, "%s: %s 빈사 상태 아님 (의료 행위 불가)", *GetName(), *HitActor->GetName());
		return;
	}
	
	// 힐링 GE 적용 (SetByCaller로 힐량 전달)
	FGameplayEffectSpecHandle DmgSpec =
		MakeOutgoingGameplayEffectSpec(MedicalConfig->MedicalDamageEffectClass, GetAbilityLevel());
	
	if (DmgSpec.IsValid())
	{
		// MedicalActionTag 자식에서 설정
		DmgSpec.Data.Get()->AddDynamicAssetTag(MedicalConfig->MedicalActionTag);

		DmgSpec.Data.Get()->SetSetByCallerMagnitude(
			VBGameplayTags::Data_MedicalAmount, MedicalConfig->BaseMedicalAmount);
		
		TargetASC->ApplyGameplayEffectSpecToSelf(*DmgSpec.Data.Get());
		VB_LOG(Log, "%s: %s 의료 행위 적용!", *GetName(), *HitActor->GetName());
	}
	
	// 명성 증가 GE 적용 (자기 자신에게)
	if (MedicalConfig->ReputationEffectClass)
	{
		FGameplayEffectSpecHandle RepSpec =
			MakeOutgoingGameplayEffectSpec(MedicalConfig->ReputationEffectClass, GetAbilityLevel());
		
		if (RepSpec.IsValid())
		{
			RepSpec.Data.Get()->SetSetByCallerMagnitude(
				VBGameplayTags::Data_ReputationChange, MedicalConfig->ReputationChangeOnMedical);
			
			// 명성은 자기 자신의 AttributeSet
			if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
			{
				OwnerASC->ApplyGameplayEffectSpecToSelf(*RepSpec.Data.Get());
				VB_LOG(Log, "%s : 명성 + %.0f", *GetName(), MedicalConfig->ReputationChangeOnMedical);
			}
		}
	}
}


bool UVBGA_MedicalAbilityBase::IsTargetLowHealth(UAbilitySystemComponent* TargetASC) const
{
	if (!TargetASC)
	{
		return false;
	}

	bool bFound = false;
	const float Health = TargetASC->GetGameplayAttributeValue(
		UVBHealthAttributeSet::GetHealthAttribute(), bFound);

	if (!bFound)
	{
		return false;
	}

	const float MaxHealth = TargetASC->GetGameplayAttributeValue(
		UVBHealthAttributeSet::GetMaxHealthAttribute(), bFound);

	if (!bFound || MaxHealth <= 0.0f)
	{
		return false;
	}

	const float HealthRatio = Health / MaxHealth;
	return HealthRatio <= MedicalConfig->LowHealthThreshold && Health > 0.0f;
}