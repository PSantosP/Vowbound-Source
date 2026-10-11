// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_MedicalAbilityBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "GameFramework/Character.h"
#include "Vowbound/Vowbound.h"


bool UVBGA_MedicalAbilityBase::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                  const FGameplayAbilityActorInfo* ActorInfo,
                                                  const FGameplayTagContainer*     SourceTags,
                                                  const FGameplayTagContainer*     TargetTags,
                                                  FGameplayTagContainer*           OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}

	// 대상이 없으면 여기서 떨어진다 -> CommitAbility 가 안 돌아 쿨다운이 남는다.
	const bool bHasTarget = FindMedicalTarget(ActorInfo) != nullptr;

	// 떨어지는 것을 말한다. 안 그러면 「F 를 눌렀는데 아무 일도 없다」가 유일한 증상이고,
	//   ExecuteAbility 안에 계측을 두어 봐야 활성화 자체가 안 되므로 그 줄은 영영 안 찍힌다.
	//   2026-09-13 에 처음 그 자리에 넣었다가 이리로 옮겼다 - 안전망이 도달 못 하는 곳에 있으면
	//   조용한 것보다 나쁘다(설명하는 척만 한다).
	// 활성화 시도마다 한 번이라 입력 한 번에 한 줄이다. 로그가 죽지 않는다.
	if (!bHasTarget)
	{
		VB_LOG(Log, "%s: 활성화 거부 - 사거리(%.0f) 안에서 «처형 창에 든 적» 을 못 찾았다. "
		            "락온 대상도 전방 원뿔도 비었다",
		       *GetName(), MedicalConfig ? MedicalConfig->MedicalRange : -1.0f);
	}

	return bHasTarget;
}

void UVBGA_MedicalAbilityBase::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                               const FGameplayAbilityActivationInfo ActivationInfo,
                                               const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo()))
	{
		VBChar->NotifyCombatStimulus(); // 의료 어빌리티도 발도 없는 전투 자극 (DEC-006 ①)
	}
	
	// 대상 탐색(락온 우선 + 전방 원뿔) + GE 적용
	PerformMedicalTrace(ActorInfo);

	// 어빌리티 종료
	// 추후 몽타주 재생 시 -> MontageEnd에서 EndAbility
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

AActor* UVBGA_MedicalAbilityBase::FindMedicalTarget(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// GetAvatarCharacter() 를 쓰지 않는다. 그것은 CurrentActorInfo 를 읽는데, 활성화 전(CanActivateAbility)에는
	// 아직 설정돼 있지 않다. 이 함수는 활성화 전 판정에도 불리므로 인자로 받은 것만 쓴다.
	ACharacter* AvatarCharacter = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!AvatarCharacter || !MedicalConfig)
	{
		return nullptr;
	}

	// 판정 본체는 UVBAbilitySystemStatics::FindFinisherTarget 에 있다.
	//
	// 왜 여기서 옮겼나(2026-09-19): 같은 판정의 소비자가 셋이 됐다 — Execute · Purify(둘 다 이 경로)와
	//   UVBGA_LightAttack(공격 버튼으로 처형에 들어가는 새 경로). 셋째가 어빌리티 계층 밖이라
	//   protected 멤버로는 못 쓴다. 규칙 12 그대로 세 번째에서 뽑았다.
	// 이 함수가 남는 이유: 사거리(MedicalConfig->MedicalRange)는 어빌리티 설정의 것이다.
	//   값은 여기 두고 판정만 넘긴다 - 값까지 옮기면 같은 숫자가 두 곳에 산다.
	return UVBAbilitySystemStatics::FindFinisherTarget(AvatarCharacter, MedicalConfig->MedicalRange,
	                                                   MedicalConfig->FinisherTargetRules);
}

void UVBGA_MedicalAbilityBase::PerformMedicalTrace(const FGameplayAbilityActorInfo* ActorInfo)
{
	// 찾기 + 적용. 즉시 갈래(정화·연출 없는 처형)가 쓰는 경로다.
	// 연출 갈래는 시작할 때 잡은 대상을 들고 ApplyMedicalEffects 를 직접 부른다 - 그쪽 주석 참조.
	ApplyMedicalEffects(ActorInfo, FindMedicalTarget(ActorInfo));
}

void UVBGA_MedicalAbilityBase::ApplyMedicalEffects(const FGameplayAbilityActorInfo* ActorInfo, AActor* HitActor)
{
	// [서버 권위] GE 적용(즉사 데미지 + 명성 변동)은 서버에서만. 베이스가 ServerInitiated 라
	// 소유 클라에서도 ExecuteAbility→여기로 들어오는데, 가드 없으면 listen-server host 에서
	// 이중 적용 위험. 형제 GA(MeleeAttackBase/Dodge)와 동일 규약 (verify 2026-06-04 지적).
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	if (!HitActor)
	{
		VB_LOG(Log, "%s: 타겟 없음", *GetName());
		return;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC || !MedicalConfig || !MedicalConfig->MedicalDamageEffectClass)
	{
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
