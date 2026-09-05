// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // ECollisionChannel (SearchTraceChannel) — VBAttackConfig.h 와 동일 패턴
#include "Engine/DataAsset.h"
#include "VBTargetLockConfig.generated.h"

/**
 * 락온(Target Lock) + 전투 카메라 튜닝 DataAsset.
 * 무엇: VBTargetLockComponent 의 락온 탐색/해제 수치 + 전투 진입 시 카메라 프레이밍을 외부화.
 * 왜: 락온 거리/각도와 전투 카메라 느낌은 디자이너가 자주 만지는 값 — 컴포넌트 디폴트에 박혀
 *     있으면 매번 빌드/재시작 필요(Live Coding 한계). DataAsset 로 빼면 에디터에서 즉시 튜닝.
 * 가정: VBTargetLockComponent 가 TObjectPtr<UVBTargetLockConfig> 로 참조. 미할당 시 CDO 디폴트 fallback.
 * 부작용: 없음(순수 데이터). 카메라 전환 커브(UCurveFloat) 는 에셋 참조라 컴포넌트에 잔류.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBTargetLockConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ── 락온 탐색/해제 ──
	// 락온 최대 가능 거리
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float MaxLockDistance = 2000.0f;

	// 락온 자동 해제 거리(이보다 멀어지면 해제)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float LockBreakDistance = 2500.0f;

	// 화면 중앙 기준 락온 탐색 각도(반각, 도)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float LockAngle = 60.0f;

	// 타겟 검색 반경
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float TargetSearchRadius = 1500.0f;

	// 카메라 회전 보간 속도
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float InterpSpeed = 10.0f;

	// 락온 중 카메라 pitch 클램프(도) — 과거 cpp 본문 하드코딩(-35/15) 외부화
	// MinCameraPitch = 내려다보는 최대각. 근접 시 look-at 이 이 값에 걸려 탑다운이 되므로 완만하게(-20).
	// 멀리서는 자연 pitch 가 이 값까지 안 가 영향 없음 — 사실상 근접 전용 완화 레버. FEEL 튜닝 대상.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float MinCameraPitch = -20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float MaxCameraPitch = 15.0f;

	// 락온 중 수동 look 입력 감쇠 계수(0=완전차단, 1=일반과 동일). 과거 cpp 0.1f 하드코딩 외부화.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float LockOnLookDampingFactor = 0.1f;

	// 타겟 선정 점수에서 거리 점수의 가중치(방향 dot 점수 대비). 과거 cpp 0.3f 하드코딩 외부화.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float DistanceScoreWeight = 0.3f;

	// ── 락온 동작(자동) ──
	// 근접 공격 명중 시 그 적으로 자동 락온 개시 여부.
	// 무엇: PerformAttackTrace 히트 직후 첫 유효 살아있는 타겟으로 락 시도.
	// 왜: 때리는 대상엔 락이 걸려 있어야 자연스럽다(전투 감각) + 콤보 후속 스윙 모션워프 자동 급전.
	// 부작용: 이미 수동 락온 중이면 무시(카메라 뺏지 않음). 스윙당 최대 1회.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	bool bAutoLockOnHit = true;

	// bAutoLockOnGuard 와 bAutoLockOnParry 는 2026-08-09 에 여기서 나갔다.
	//   가드 -> UVBGA_Guard (그 GA 가 유일 소비처)
	//   패링 -> UVBParryConfig (패링 튜닝의 나머지가 전부 거기 있다)
	// 이 자산에 있던 이유는 "락온을 켜는 값"이라는 주제 때문이었는데, 소유권 판정 기준은 주제가 아니라
	// 누가 읽느냐다. 두 값 다 락온 컴포넌트 밖에서 읽혔고, 특히 패링은 한 기능의 튜닝이 자산 두 개로
	// 갈라져 있었다. 이관 시점 저작값은 둘 다 true 였고 새 홈의 기본값도 true 다.
	// bAutoLockOnHit 은 남는다 - 소비처가 이 컴포넌트 내부(VBTargetLockComponent.cpp:429)다.

	// ── 소프트 런지(락온 없이 첫 스윙 돌진) ──
	// 락온 획득 파라미터(MaxLockDistance/TargetSearchRadius/LockAngle)와 분리된 별도 관심사.
	// 왜 분리: 소프트 런지는 "정면·근접 첫 타 훅 다가가기"라 락온보다 좁은 콘/짧은 반경이 자연스럽다.

	// 소프트 런지 후보 탐색 반경(cm). 락온 TargetSearchRadius(1500)보다 짧게 — 근접 첫 스윙 돌진 거리만.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float SoftLungeRadius = 800.0f;

	// 소프트 런지 정면 콘 반각(도). 락온 LockAngle(60)보다 좁게 — "정면" 의도 엄격히(오조준 방지).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float SoftLungeAngle = 45.0f;

	// ── 타겟 전환 입력(락온 중 좌우 스위치) ── (VBCharacter 에서 이동 — 논리상 락온 튜닝)
	// 이 값보다 큰 마우스 X 이동에서만 좌/우 타겟 전환 트리거. 현행 1.5f 보존.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float SwitchTargetMouseThreshold = 1.5f;

	// 타겟 전환 후 쿨다운(초) — 연타 방지. 현행 0.3f 보존.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	float SwitchTargetCooldown = 0.3f;

	// 타겟 탐색(SphereOverlap) 오브젝트 채널. UEngineTypes::ConvertToObjectType 로 변환해 사용.
	// 왜: FindBestTarget/FindSideTarget/FindSoftLungeTarget 3곳 하드코딩 ECC_Pawn 외부화. 현행 보존.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|LockOn")
	TEnumAsByte<ECollisionChannel> SearchTraceChannel = ECC_Pawn;

	// ── 전투 카메라 프레이밍(락온 진입 시 전환 목표값) ──
	// 전환 소요 시간(초) — Timeline 길이
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float CameraTransitionDuration = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float CombatTargetArmLength = 350.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	FVector CombatSocketOffset = FVector(0.0f, 60.0f, 70.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float CombatCameraLagSpeed = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	float CombatFieldOfView = 70.0f;
};
