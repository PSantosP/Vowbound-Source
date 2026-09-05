// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "Data/VBMedicalConfig.h"
#include "VBGA_MedicalAbilityBase.generated.h"

/**
 * VBGA_MedicalAbilityBase
 *
 * 의료 행위(정화/처치) GA의 기초 클래스. Purify/Execute가 공통으로 사용.
 *
 * 의도:
 *  - 빈사 상태(State.Combat.LowHealth) 적 탐색 + 즉사 데미지 + 명성 변화 파이프라인
 *  - Vowbound의 "의료 자원으로 싸우는" 핵심 테마를 GA로 구현
 *  - 파생(Purify/Execute)은 명성 증감 방향만 다름
 *
 * 전제:
 *  - MedicalConfig (UVBMedicalConfig) 할당
 *  - 대상이 State.Combat.LowHealth 태그 보유 (IsTargetLowHealth 체크)
 *  - NetExecutionPolicy=ServerInitiated (베이스 디폴트) — 클라에서도 ExecuteAbility 가 돌므로
 *    GE 적용 경로(PerformMedicalTrace)는 IsNetAuthority 가드 필수 (2026-06-04 docstring 정정)
 *
 * 부작용:
 *  - LineTrace로 전방 빈사 대상 탐색
 *  - GE 적용 (즉사 데미지)
 *  - Reputation 변동 (Purify=+, Execute=-)
 *  - HitStop 재생
 */
UCLASS(Abstract)
class VOWBOUND_API UVBGA_MedicalAbilityBase : public UVBGameplayAbility
{
	GENERATED_BODY()
protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo, 
		const FGameplayAbilityActivationInfo ActivationInfo, 
		const FGameplayEventData* TriggerEventData) override;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	TObjectPtr<UVBMedicalConfig> MedicalConfig;
	
	// 향후 소켓 기반으로 변경
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	FVector DefaultTraceHeightOffset = FVector(0.0f, 0.0f, 50.0f);
	
	// LineTrace로 전방 대상 탐색 -> GE 적용
	void PerformMedicalTrace(const FGameplayAbilityActorInfo* ActorInfo);
	
	// 빈사 상태인지 확인
	bool IsTargetLowHealth(UAbilitySystemComponent* TargetASC) const;
};
