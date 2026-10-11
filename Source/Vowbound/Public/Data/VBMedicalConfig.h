// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Engine/DataAsset.h"
#include "Data/VBReachRules.h"   // FVBReachRules - 값으로 들고 있다
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
	
	// 빈사 판정(문턱)과 탐색 채널은 2026-09-11 에 여기서 나갔다.
	//  - 문턱 -> UVBFinisherWindowConfig::HealthThreshold. 창을 여는 것은 피해자이므로 값도 피해자 쪽에 있어야 한다.
	//  - 탐색 채널 -> UVBTargetLockConfig::SearchTraceChannel. 대상 탐색이 라인 트레이스에서
	//    「락온 우선 + 전방 원뿔」로 바뀌면서 콘 탐색의 채널을 그대로 쓴다.
	// MedicalRange 는 남는다 - 「의료 행위가 닿는 거리」는 여전히 의료 쪽 질문이다.
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	TSubclassOf<UGameplayEffect> MedicalDamageEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	TSubclassOf<UGameplayEffect> ReputationEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	FGameplayTag MedicalActionTag;

	// 처형 대상의 «자리» 조건. 전문은 위 구조체 주석에.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Medical")
	FVBReachRules FinisherTargetRules;
	
};
