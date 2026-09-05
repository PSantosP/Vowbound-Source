// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Engine/EngineTypes.h" // ECollisionChannel (HitTraceChannel) — VBAttackConfig.h 와 동일 패턴
#include "Engine/DataAsset.h"
#include "VBMedicalConfig.generated.h"

/**
 * 의료 Ability 설정 DataAsset.
 * 범위, 회복량, 평판, 변화 등을 에디터에서 관리
 */
UCLASS()
class VOWBOUND_API UVBMedicalConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()
	
public:
	// 의료 사거리
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	float MedicalRange = 300.0f;
	
	// 기본 의료 행위량 (SetByCaller에 전달하는 값)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	float BaseMedicalAmount = 30.0f;
	
	// 처치 시 명성 변동량
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	float ReputationChangeOnMedical = 10.0f;
	
	// 빈사 판정 체력 비율 (이하일 때 처치 가능)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	float LowHealthThreshold = 0.3f;

	// 의료 대상 탐색 LineTrace 채널. 왜: AttackConfig::HitTraceChannel 과 동일 패턴 — Medical 만 하드코딩 ECC_Pawn 이었음.
	// 현행 동작 보존(ECC_Pawn 디폴트).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	TEnumAsByte<ECollisionChannel> HitTraceChannel = ECC_Pawn;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	TSubclassOf<UGameplayEffect> MedicalDamageEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	TSubclassOf<UGameplayEffect> ReputationEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	FGameplayTag MedicalActionTag;
	
};
