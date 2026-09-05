// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_WeaponSummon.generated.h"

/**
 * VBGA_WeaponSummon
 * 
 * 무기 소환/수납 GA (GameplayAbility). Input_WeaponSummon 태그로 트리거되어
 * 현재 무기 상태에 따라 소환 또는 수납을 결정한다.
 * 
 * 의도:
 *  - Enhanced Input → GAS 파이프라인의 "무기 전환" 진입점
 *  - VBCharacter가 WSC를 직접 조작하지 않고 GA 경유로 추상화
 * 
 * 전제:
 *  - ASC가 InitAbilityActorInfo 완료 상태 (BeginPlay 이후)
 *  - OwnerCharacter에 UVBWeaponStateComponent가 부착되어 있을 것
 *  - NetExecutionPolicy=ServerOnly (베이스에서 기본값 상속)
 * 
 * 부작용:
 *  - WSC->SummonWeapon(Type) 또는 SheatheWeapon() 호출
 *  - LocomotionState 및 CurrentWeaponType 복제 속성 변경
 *  - ASC에 State_Armed, State_Armed.{Weapon} 루즈 태그 추가/제거
 */
UCLASS()
class VOWBOUND_API UVBGA_WeaponSummon : public UVBGameplayAbility
{
	GENERATED_BODY()
	
public:
	UVBGA_WeaponSummon();
	
protected:
	/**
	 * ExecuteAbility
	 *
	 * 무기 소환/수납 실제 실행 지점. ActivateAbility→CommitAbility 이후 호출된다.
	 *
	 * 의도:
	 *  - TriggerEventData 또는 OwnerCharacter의 요청 타입을 기준으로
	 *    현재 상태(Unarmed/Armed)에 따라 Summon vs Sheathe 분기
	 *
	 * 전제:
	 *  - HasAuthority() 서버 컨텍스트 (NetExecutionPolicy=ServerInitiated)
	 *  - OwnerCharacter->GetWeaponStateComponent() 유효
	 *
	 * 부작용:
	 *  - WSC Server RPC 트리거 → LocomotionState/CurrentWeaponType 복제값 변경
	 *  - EndAbility 보장 (성공/실패 양쪽 경로)
	 */
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
