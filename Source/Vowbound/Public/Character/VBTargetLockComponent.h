// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/TimelineComponent.h"
#include "Curves/CurveFloat.h"
#include "VBTargetLockComponent.generated.h"


class AVBCharacter;
class APlayerController;
class UVBTargetLockConfig;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBTargetLockComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBTargetLockComponent();

	// 락온 토글(On/Off) — 클라 진입점. 로컬 카메라로 타겟 탐색 후 서버 RPC로 반영.
	// Remote Client 지원의 핵심: 서버의 PlayerCameraManager는 remote 캐릭터에 대해 tick 안 되므로
	// 카메라 기반 탐색은 반드시 클라에서 실행해야 한다.
	void ToggleLockOn();
	// 좌/우 타겟 전환 + = 오른쪽 | - = 왼쪽. 클라 진입점.
	void SwitchTarget(float AxisValue);

	// Server RPC — 클라가 탐색한 target을 서버에 반영 (validate + SetLockOn)
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetLockOn(AActor* Target);

	// Server RPC — 락온 해제
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerClearLockOn();

	// 현재 락온 대상 반환
	AActor* GetLockedTarget() const;
	// IsLockedOn()
	bool IsLockedOn() const;
	// 락온 해제 진입점. 호출처가 서버면 바로 처리, 클라면 ServerClearLockOn RPC로 라우팅.
	void ClearLockOn();

	// 서버 전용 — 근접 공격 명중 시 자동 락온 진입점. PerformAttackTrace(서버 권위)에서 호출.
	// 권위/설정(bAutoLockOnHit)/미락온 정책/타겟 유효성 가드 후 private SetLockOn 재사용.
	// 신규 복제/RPC 없음 — 서버가 이미 도는 지점이라 LockedTarget 복제 + OnRep 이 소유 클라 카메라를 구동한다.
	void TryAutoLockOnTarget(AActor* Target);

	// 소유 클라 진입점 - 카메라 기준으로 대상을 찾아 락온한다. ToggleLockOn 과 달리 해제하지 않는다.
	// 왜 별도 함수인가: ToggleLockOn 은 이미 락온 중이면 해제해 버린다. 가드처럼 "걸기만" 원하는
	//   호출자가 그걸 쓰면 가드를 올릴 때마다 락이 켜졌다 꺼진다.
	// 왜 클라인가: 원격 클라의 서버측 PlayerCameraManager 는 tick 이 안 돼 stale 하다(ToggleLockOn 과 같은 계약).
	UFUNCTION(BlueprintCallable, Category="Vowbound|LockOn")
	void TryLockOnFromLocalView();

	// 넷-세이프 소프트 런지 타겟 탐색(락온 없이 첫 스윙 돌진용). 액터 forward 기준 정면 콘 최고점수 픽.
	// 왜 액터 forward: 서버 권위 PerformAttackTrace 스윕과 동일 기준 → 서버·소유클라 결정론적 동일 결과(카메라 금지).
	// const: 상태 변이 없음(워프 전용, 하드락 LockedTarget 안 건드림). 없으면 nullptr.
	AActor* FindSoftLungeTarget() const;

	// LockConfig 안전 접근자 — 미할당이면 클래스 CDO 디폴트 반환(절대 null 아님).
	// public 이유: VBCharacter(HandleLook 감쇠) 등 외부 소비자가 락온 튜닝값을 읽는 단일 경로.
	const UVBTargetLockConfig* GetTargetLockConfig() const;
	
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	// UI 연동용
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetLockChanged, AActor*, TargetActor);
	UPROPERTY(BlueprintAssignable, Category="Vowbound|LockOn")
	FOnTargetLockChanged OnTargetLocked;
	UPROPERTY(BlueprintAssignable, Category="Vowbound|LockOn")
	FOnTargetLockChanged OnTargetUnlocked;
	
protected:
	virtual void BeginPlay() override;
	
	// 락온 + 전투 카메라 튜닝 DataAsset. 미할당 시 CDO 디폴트 fallback (GetTargetLockConfig).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	TObjectPtr<UVBTargetLockConfig> LockConfig;

	// 카메라 전환 커브 (에셋 참조 — 튜닝값 아님, 컴포넌트 잔류)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Camera")
	TObjectPtr<UCurveFloat> CameraTransitionCurve;


	UPROPERTY()
	TWeakObjectPtr<AVBCharacter> OwnerCharacter;
	UPROPERTY()
	TWeakObjectPtr<APlayerController> OwnerController;

private:
	// 카메라 전환
	FTimeline CameraTransitionTimeline;
	float DefaultTargetArmLength = 0.0f;
	FVector DefaultSocketOffset = FVector::ZeroVector;
	float DefaultCameraLagSpeed = 0.0f;
	float DefaultFieldOfView = 0.0f;
	
	UFUNCTION()
	void OnCameraTransitionUpdate(float Alpha);

	// 카메라 전방 기준 최적 target (초기 락온용) — 로컬 카메라 기반
	AActor* FindBestTarget();
	// 좌/우 입력 방향에 가장 가까운 target (스위치용) — 로컬 카메라 기반
	AActor* FindSideTarget(float AxisValue);
	bool IsTargetValid(AActor* Actor) const;

	// 서버 전용 — 락온 상태 실제 변경. ServerSetLockOn/Clear에서만 호출.
	void SetLockOn(AActor* Target);

	// 서버 전용 — 락온 시/해제 시 gameplay 효과 (ASC 태그, CombatMode, Sprint 중지)
	void ApplyLockOnGameplayEffects();
	void RemoveLockOnGameplayEffects();
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	UFUNCTION()
	void OnRep_LockedTarget();
	
	
	UPROPERTY(ReplicatedUsing=OnRep_LockedTarget)
	TObjectPtr<AActor> LockedTarget;
};
