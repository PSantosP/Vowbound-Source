// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h" // FTimerHandle
#include "VBHitFlashComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UVBHitFlashConfig;
struct FGameplayCueParameters;

/**
 * UVBHitFlashComponent
 * 피격 순간 메시 전체를 순간 발광시키는 오버레이 플래시의 상태 소유자.
 *
 * 왜 컴포넌트인가: GameplayCueNotify_Static 은 CDO 로 실행되어 인스턴스 상태(MID/타이머)를 가질 수 없다.
 *   큐는 얇게 위임만 하고, 상태는 피격자 본인이 든다.
 *
 * 왜 tick 이 없는가: 감쇠는 전적으로 GPU 가 계산한다(머티리얼이 View.RealTime - FlashStartTime 을 읽는다).
 *   CPU 는 피격 순간에 시작시각/색/강도만 심는다. 덕분에 AVBEnemyBase 의 bCanEverTick=false 를 건드리지 않는다.
 *
 * 왜 소유자를 ACharacter 로만 보는가: 플레이어(AVBCharacter)와 적(AVBEnemyBase)은 공통 VB 베이스가 없다.
 *   둘 다에 붙을 수 있어야 하므로 구체 클래스가 아닌 ACharacter::GetMesh() 계약에만 의존한다.
 *
 * 부작용: 대상 스켈레탈 메시의 OverlayMaterial 을 교체한다(렌더 상태 재생성).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBHitFlashComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBHitFlashComponent();

	// 플래시 시작/재시작. 큐(UVBGC_HitFlash)가 유일한 호출자.
	// 재트리거는 중첩이 아니라 교체다 — 단일 타이머를 다시 세팅하고 시작시각을 지금으로 덮는다.
	void TriggerFlash(const FGameplayCueParameters& Parameters);

	// 오버레이 즉시 해제 + 타이머 정리. AVBEnemyBase::HandleDeathCosmetic 이 래그돌 전환 직전에 호출한다.
	void ClearFlash();

protected:
	// 플래시 표현 튜닝 DataAsset. 미할당 시 UVBHitFlashConfig CDO 디폴트가 fallback (GetHitFlashConfig).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel")
	TObjectPtr<UVBHitFlashConfig> HitFlashConfig;

	// HitFlashConfig 안전 접근자 — 미할당이면 클래스 CDO 디폴트 반환(절대 null 아님).
	const UVBHitFlashConfig* GetHitFlashConfig() const;

private:
	// 소유자를 ACharacter 로 보고 GetMesh() 를 돌려준다. 구체 VB 클래스에 의존하지 않는 유일한 경로.
	USkeletalMeshComponent* ResolveTargetMesh() const;

	// 플래시 창 만료 콜백. 실시간으로 재검증하고, 아직이면 잔여만큼 재무장한다.
	void OnFlashExpired();

	// 프로필 머티리얼당 1개. 오버레이는 슬롯 무관하게 전 섹션을 덮으므로 섹션 수만큼 만들 필요가 없다.
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> FlashMID;

	// FlashMID 를 만들어 낸 원본. 프로필이 바뀌면(태그 오버라이드) MID 를 다시 만들어야 하므로 비교용으로 든다.
	UPROPERTY()
	TObjectPtr<UMaterialInterface> FlashSourceMaterial;

	// 단일 타이머. 같은 핸들로 SetTimer 하면 엔진이 기존 것을 먼저 clear 하므로 연타해도 중첩되지 않는다.
	FTimerHandle FlashTimerHandle;

	// 플래시가 끝나야 하는 실시각(World->GetRealTimeSeconds 축). 만료 재검증의 기준.
	double FlashEndRealTime = 0.0;
};
