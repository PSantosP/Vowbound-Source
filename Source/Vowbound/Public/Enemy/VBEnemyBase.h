// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Character.h"
#include "Character/VBTargetable.h"
#include "Character/VBTeamTypes.h"
#include "VBEnemyBase.generated.h"

class UVBAbilitySystemComponent;
class UVBHealthAttributeSet;
class UVBCombatAttributeSet;
class UVBReputationAttributeSet;
class UGameplayEffect;
class UVBEnemyConfig;
class UVBHitFlashComponent;
class UVBEnemyCombatComponent;

/**
 * 모든 적의 기초 클래스
 * ASC를 Character가 직접 소유 (NPC: Respawn 없음)
 */
UCLASS(Abstract)
class VOWBOUND_API AVBEnemyBase : public ACharacter, public IAbilitySystemInterface, public IVBTargetable,
                                  public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	//~ Begin IGenericTeamAgentInterface
	// 소속은 적 종류별 config 가 소유한다 - 같은 세력끼리는 Friendly 라 서로 지각하지 않는다.
	// GetEnemyConfig() 는 미할당 시 CDO 를 돌려주므로 절대 null 이 아니다.
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface

	AVBEnemyBase(const FObjectInitializer& ObjectInitializer);
	
	// IAbilitySystemInterface 구현
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	
	// 기본 Ability 부여 목록 (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GAS")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;
	
	// 초기 스탯 설정용 GE (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GAS")
	TSubclassOf<UGameplayEffect> InitStatsEffect;
	
	virtual bool IsTargetable() const override;
	// 락온 카메라가 조준할 '올린 지점'(가슴). 구현은 .cpp — 소켓/본 조회 + actor+Z 폴백(ZeroVector 반환 금지).
	virtual FVector GetTargetMarkerLocation() const override;
	bool IsDead() const;

	// 락온 카메라가 조준할 마커 본/소켓(가슴 높이). 스켈레톤별 자동 스케일 — UE 마네킹 기본 상체 본.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|LockOn")
	FName MarkerBoneName = FName("spine_03");

	// 위 본/소켓 부재 시 폴백: 캡슐 중심(GetActorLocation) 기준 Z 상승(cm). 가슴 높이대(+40~60).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|LockOn")
	float MarkerFallbackHeight = 50.0f;
protected:
	virtual void BeginPlay() override;
	
	// ASC (Character 직접 소유)
	UPROPERTY(VisibleAnywhere, Category="Vowbound|GAS")
	TObjectPtr<UVBAbilitySystemComponent> AbilitySystemComponent;
	
	// AttributeSet (ASC가 라이프타임 관리, GC-only — exposure 키워드 없이 UPROPERTY())
	UPROPERTY()
	TObjectPtr<UVBHealthAttributeSet> HealthAttributeSet;

	UPROPERTY()
	TObjectPtr<UVBCombatAttributeSet> CombatAttributeSet;

	// 피격 플래시 상태 소유자(MID/타이머). GameplayCue 가 FindComponentByClass 로 찾아 위임한다.
	// BP 추가가 아니라 C++ 생성인 이유: 신규 적 BP 마다 배선이 빠질 수 있고 그 누락은 코드 리뷰에 보이지 않는다.
	// 큐에서 lazy 생성하지 않는 이유: 하필 피격 순간에 RegisterComponent 비용을 얹게 된다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|GameFeel")
	TObjectPtr<UVBHitFlashComponent> HitFlashComponent;

	// 그룹 전투 조율 상태(역할/토큰/슬롯의 조회 창구). HitFlashComponent 와 같은 이유로 C++ 생성이다 -
	// 신규 적 BP 마다 배선이 빠질 수 있고 그 누락은 "이 적만 협조하지 않는다"로만 나타나 눈에 안 띈다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|AI")
	TObjectPtr<UVBEnemyCombatComponent> CombatComponent;

	// 일반 사망 시 명성 변동 GE (GE_ReputationChange와 동일 GE 사용)
	// BP_InfectedEnemy 등에서 설정
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	TSubclassOf<UGameplayEffect> DefaultDeathReputationEffect;

	// 이동/사망 튜닝값 DataAsset. 미할당 시 UVBEnemyConfig CDO 디폴트가 fallback (GetEnemyConfig).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Config")
	TObjectPtr<UVBEnemyConfig> EnemyConfig;

	// EnemyConfig 안전 접근자 — 미할당이면 클래스 CDO 디폴트 반환(절대 null 아님).
	// public 인 이유: AVBAIController 가 빙의한 폰의 시야 값을 읽는다. 필드 자체는 protected 를 유지해
	//   외부는 getter 만 보게 한다 - 널 안전이 getter 에만 있기 때문이다.
public:
	const UVBEnemyConfig* GetEnemyConfig() const;

	// 전투 조율 컴포넌트. BTService 는 폰에서 FindComponentByClass 로 찾지만(폰 타입에 묶이지 않기 위해),
	// 적 코드 안에서는 이 getter 를 쓴다 - 컴포넌트가 항상 존재한다는 사실이 여기서만 보장되기 때문이다.
	UVBEnemyCombatComponent* GetCombatComponent() const { return CombatComponent; }
protected:


	// 사망 처리 (서버에서만 호출)
	virtual void HandleDeath();

	// 래그돌/콜리전 전환을 모든 Net Role에 동시 적용 (서버 HandleDeath에서 호출)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHandleDeathCosmetic();

	void HandleDeathCosmetic();
	
	// 사망 후 정리 (래그돌 후 제거)
	void DestroyAfterDeath();

private:
	// Health Attribute 변경 감지 -> 사망 판정
	void OnHealthChanged(const struct FOnAttributeChangeData& Data);
	
	// 일반 사망 시 Instigator에게 명성 패널티 적용
	void ApplyDefaultDeathReputation();
};
