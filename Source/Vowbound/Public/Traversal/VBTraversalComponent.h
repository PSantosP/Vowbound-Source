// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Traversal/VBTraversalTypes.h"
#include "Animation/AnimMontage.h" // FOnMontageEnded
#include "VBTraversalComponent.generated.h"

class AVBCharacter;
class UVBTraversalConfig;
class UChooserTable;
class UPrimitiveComponent;

/**
 * UVBTraversalComponent
 *
 * GASP CBP_SandboxCharacter::TryTraversalAction / PerformTraversalAction / UpdateWarpTargets 를
 * C++ 로 1:1 포팅(서버 권위 래퍼 추가). VBCharacter 에 부착.
 *
 * 흐름(GASP 그대로):
 *  TryTraversalAction(소유 클라) → 전방/공간/상단/하향 트레이스 + ActionType 판정(UVBTraversalConfig)
 *    → InteractionTransform 전달 → CHT 평가 → MotionMatch → 몽타주/시작시간 선택
 *    → ServerPerformTraversal(RPC) → MulticastPerformTraversal → 전원 PerformTraversalAction(Flying+Warp+몽타주)
 *
 * deviation(승인됨): GASP 싱글 BP → Vowbound 서버권위(Server_/Multicast_), C++ 포팅. 로직은 1:1.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBTraversalComponent();

	/** IA_Jump 진입점. 트래버설 수행 시 true(점프 억제), 불가 시 false(기존 점프 진행).
	 *  bAirMantleOnly=true: 공중(점프 키 홀드) 자동 체크 모드 — Mantle(/Climb)만 허용, Vault/Hurdle 제외.
	 *  (Vowbound 확장 — GASP 는 IA_Jump 에 IsMovingOnGround 게이트가 있어 공중 트래버설이 없음.
	 *   user 승인 2026-06-04: "점프 중 벽 딛고 올라가기", 발동 = 점프 키 홀드 or 공중 재입력) */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Traversal")
	bool TryTraversalAction(bool bAirMantleOnly = false);

	FORCEINLINE bool IsDoingTraversal() const { return bDoingTraversalAction; }

protected:
	virtual void BeginPlay() override;

	// 트레이스/판정 임계값 (GASP 인라인 리터럴의 데이터화).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal")
	TObjectPtr<UVBTraversalConfig> Config;

	// CHT_VB_TraversalAnims (struct 컨텍스트=FVBTraversalChooserParams). T6에서 할당.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal")
	TObjectPtr<UChooserTable> TraversalChooserTable;

	// ABP PoseSearchHistoryCollector 노드 태그 (MotionMatch query 생성). GASP MotionMatch 와 동일 "PoseHistory".
	// NAME_None 이면 MotionMatch 가 history 를 못 찾아 SelectedAnim=null → 트래버설 미발동(일반 점프).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal")
	FName PoseHistoryName = TEXT("PoseHistory");

	// UVBTraversalConfig 안전 접근(미할당 시 CDO 디폴트=GASP exact).
	const UVBTraversalConfig* GetConfig() const;

	// Step1-3: 전방→LevelBlock→GetLedgeTransforms→공간/높이/깊이/바닥 트레이스. 채워진 Result 반환(front 유효 시 true).
	// bAirMode=true: 공중 맨틀 체크 — 전방 트레이스를 속도비례(최대 350) 대신 AirMantleTraceDistance 고정 단거리로.
	bool PerformTraversalCheck(FVBTraversalCheckResult& OutResult, bool bAirMode = false) const;

	// Step4.1: 조건/파라미터로 ActionType 결정 (UVBTraversalConfig).
	EVBTraversalActionType DecideActionType(const FVBTraversalCheckResult& R) const;

	// === 서버 권위 래퍼 ===
	// WithValidation: 악성 패킷(null 몽타주/범위밖 ActionType/NaN 위치) 조기 차단. _Implementation 은
	// 추가로 클라가 보낸 렛지 좌표를 서버 캐릭터 위치 기준 거리 상한으로 검증(임의 워프 텔레포트 차단).
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerPerformTraversal(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPerformTraversal(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	// Step5.5 / PerformTraversalAction: Flying 전환 + 장애물 콜리전 무시 + 워프 + 몽타주 재생.
	void PerformTraversalAction(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	// UpdateWarpTargets: FrontLedge(항상) / BackLedge(Hurdle·Vault) / BackFloor(Hurdle) MotionWarping 타겟.
	void UpdateWarpTargets(const FVBTraversalCheckResult& Result, UAnimMontage* Montage);

	// 몽타주 블렌드아웃 "시작" 콜백 — bInterrupted(=BP_NotifyState_MontageBlendOut 의 조기 Montage_Stop 등)면
	// 즉시 복구. GASP PlayMontage OnInterrupted 대응: GASP 는 조기 탈출 순간 Walking 복귀해 블렌드 중에도
	// 지상 가속이 걸린다. VB 가 종료(블렌드아웃 완료) 시점에만 복구하던 동안의 ~0.3s Flying 공백이
	// "올라온 뒤 멈칫"의 원인 (user 보고 2026-06-04).
	void OnTraversalMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	// 몽타주 완전 종료 콜백 — 자연 종료(!bInterrupted)만 여기서 복구. GASP PlayMontage OnCompleted 대응.
	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 공통 복구: MovementMode 복귀(Vault→Falling, 그 외→Walking — GASP Select 핀 리터럴) +
	// 콜리전 복구 + 워프 타겟 제거 + DoingTraversal 해제. 두 콜백 중 먼저 온 쪽만 실행(가드).
	void RecoverFromTraversal();

	// capsule 트레이스 헬퍼(TraceChannel 사용).
	bool CapsuleSweep(const FVector& Start, const FVector& End, FHitResult& OutHit) const;

	// 렛지에서 LedgeNormal 방향 (CapsuleRadius+2) 수평 오프셋 후 하향 캡슐 스윕으로 바닥 탐색.
	// 왜 오프셋: 캡슐(직경≈68)이 얇은 벽(depth 30 등)을 감싸 시작부터 관통 → 벽 측면을 floor 로
	//   오인식(BackLedgeHeight 과소측정 → Hurdle/Vault 오탈락) 방지 — Step3.6 버그 교훈.
	// Z: Start 는 렛지 위 +HalfH+2, End 는 렛지 아래 -DownDepth — 깊이는 호출처가 결정
	//   (back-floor: ObstacleHeight-HalfH+50 / air front-floor: MantleHeightMax+50, GASP 수치 그대로).
	bool TraceFloorBelowLedge(const FVector& LedgePoint, const FVector& LedgeNormal, float DownDepth, FHitResult& OutFloorHit) const;

private:
	UPROPERTY()
	TWeakObjectPtr<AVBCharacter> OwnerCharacter;

	bool bDoingTraversalAction = false;

	// 트래버설 중 콜리전 무시한 장애물(종료 시 복구).
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> IgnoredObstacle;

	// 복구 시 MovementMode 분기용 — 이번 트래버설의 ActionType (Vault→Falling, 그 외→Walking).
	EVBTraversalActionType LastActionType = EVBTraversalActionType::None;

	FOnMontageEnded MontageEndedDelegate;
	FOnMontageBlendingOutStarted MontageBlendingOutDelegate;
};
