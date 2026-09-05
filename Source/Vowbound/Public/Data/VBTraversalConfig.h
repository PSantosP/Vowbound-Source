// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h" // ECollisionChannel
#include "VBTraversalConfig.generated.h"

/**
 * UVBTraversalConfig
 *
 * 트래버설 전방 트레이스 + Step4.1 ActionType 판정 임계값 DataAsset.
 * GASP CBP_SandboxCharacter::TryTraversalAction 의 인라인 리터럴을 데이터로 외부화한 것
 * (2026-06-01 GASP 라이브 스크린샷에서 추출한 정확값 — Mantle_GASP_Analysis_2026-06-01.md §C).
 *
 * 판정 규칙(GASP exact):
 *  - In Range = ObstacleHeight(양끝 포함), 비교 = ObstacleDepth(임계 ObstacleDepthThreshold)
 *  - Vault : HasFrontLedge ∧ NOT HasBackFloor ∧ Height∈[Vault] ∧ Depth<임계
 *  - Hurdle: HasFrontLedge ∧ HasBackFloor  ∧ Height∈[Hurdle] ∧ Depth<임계 ∧ BackLedgeHeight>MinBackLedgeHeight
 *  - Mantle: HasFrontLedge ∧ Height∈[Mantle] ∧ Depth>=임계 (chooser가 ≤150 Mantle / 150-275 Climb 자산 분기)
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBTraversalConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// === 전방 트레이스 (속도 비례 — GASP GetTraversalForwardTraceDistance 1:1) ===
	// GASP CBP 그래프(2026-06-04 라이브 추출): MapRangeClamped(UnrotateVector(Velocity, ActorRot).X, 0→500, 75→350).
	// WHY: 정지/걷기(전방속도 0~)는 75cm 코앞에서만, 최고속(500)은 350cm 앞에서 미리 발동해야
	//      달리는 기세 그대로 파쿠르로 이어진다. 고정 거리(구 100cm)는 달려도 코앞 발동 → "너무 가까이
	//      가야 발동" 증상(user 보고 2026-06-04). 전방 "로컬" 성분(X)만 쓰므로 후진/측면 이동은 거리를 안 늘림.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float TraceForwardSpeedMin = 0.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float TraceForwardSpeedMax = 500.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float TraceForwardDistanceMin = 75.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float TraceForwardDistanceMax = 350.0f;

	// 공중 맨틀(점프 키 홀드, Vowbound 확장) 전용 전방 트레이스 거리 — 고정값.
	// WHY: 속도비례 거리(run 시 350cm)는 "달리는 기세→지상 발동" 용이고, 진입 run-up 애니가
	//      그 거리를 소화한다는 전제다. 공중엔 run-up 이 없어 같은 공식을 쓰면 점프 직후 3m+ 밖
	//      벽을 잡고 MotionWarping 이 공중을 가로질러 끌고 감("멀리서 점프만 눌러도 날아가 올라감"
	//      user 보고 2026-06-04). 충돌 직전(run 500cm/s 기준 ~0.2s)에만 잡도록 짧게 고정.
	//      GASP 대응값 없음 — 공중 트래버설 자체가 Vowbound 확장.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float AirMantleTraceDistance = 100.0f;

	// 전방/공간/상단/하향 트레이스 채널 (GASP: Visibility)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// === Step4.1 판정 (GASP exact) ===
	// ObstacleDepth 임계: < 값 → 얇음(Vault/Hurdle), >= 값 → 두꺼움(Mantle)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Decision")
	float ObstacleDepthThreshold = 59.0f;

	// Vault: Height∈[Min,Max] ∧ Depth<임계 ∧ NOT HasBackFloor
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Vault")
	float VaultHeightMin = 50.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Vault")
	float VaultHeightMax = 125.0f;

	// Hurdle: Height∈[Min,Max] ∧ Depth<임계 ∧ HasBackFloor ∧ BackLedgeHeight>MinBackLedgeHeight
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Hurdle")
	float HurdleHeightMin = 50.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Hurdle")
	float HurdleHeightMax = 125.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Hurdle")
	float HurdleMinBackLedgeHeight = 50.0f;

	// Mantle: Height∈[Min,Max] ∧ Depth>=임계 (chooser가 ≤150 Mantle / 150-275 Climb)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Mantle")
	float MantleHeightMin = 50.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Mantle")
	float MantleHeightMax = 275.0f;

	// === 렛지 하향 프로브 (TraceFloorBelowLedge) ===
	// 프로브 시작점을 렛지 면에서 띄우는 여유(cm). 수평(법선 방향)과 수직 시작 높이에 같은 값으로 들어간다.
	// 왜 필요한가: 캡슐 스윕을 표면에 딱 붙여 시작하면 첫 프레임이 이미 관통 상태라 엔진이 시작 지점 히트를
	//   돌려주고 바닥을 못 찾는다. 스킨 두께만큼 밖에서 시작해야 한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float LedgeProbeSkin = 2.0f;

	// 뒤 렛지 아래 바닥 탐색 깊이에 더하는 여유(cm). 실제 깊이 = ObstacleHeight - 캡슐 반높이 + 이 값.
	// GASP TryTraversalAction 원본 수치. 여유가 없으면 뒤쪽 지면이 앞쪽보다 조금만 낮아도
	//   바닥 미발견으로 판정돼 Hurdle 이 Vault 로 잘못 분류된다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float BackFloorProbeExtraDepth = 50.0f;

	// 공중 맨틀에서 벽 실높이를 재는 앞면 하향 프로브 깊이에 더하는 여유(cm). 깊이 = MantleHeightMax + 이 값.
	// 왜 위 값과 분리하는가: 위쪽은 GASP 원본의 뒤쪽 발동 판정용이고 이쪽은 Vowbound 확장(공중 맨틀)의
	//   애니 선택용이다. 측정 목적이 달라 함께 움직일 이유가 없다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Trace")
	float FrontFloorProbeExtraDepth = 50.0f;

	// === 서버 검증 ===
	// 클라가 보낸 렛지/워프 좌표가 서버 캐릭터에서 떨어져 있어도 되는 최대 거리(cm). 초과하면 트래버설을 거부한다.
	// 이것은 튜닝값이 아니라 안티치트 상한이다. 정당한 파쿠르의 최대 도달거리보다 넉넉히 크고
	//   임의 텔레포트는 막을 만큼 작아야 한다. 실측 상한은 대략 sqrt(TraceForwardDistanceMax^2 + 높이차^2) 로 400cm 대다.
	// 일부러 파생값(TraceForwardDistanceMax + MantleHeightMax 등)으로 만들지 않았다. 그렇게 하면 맨틀 높이를
	//   올리는 순간 서버 검증이 조용히 느슨해진다 - 연출 노브가 보안 임계를 움직이면 안 된다.
	//   TraceForwardDistanceMax 나 MantleHeightMax 를 크게 올릴 때는 이 값도 사람이 직접 올릴 것.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal|Network")
	float MaxPlausibleLedgeDistance = 600.0f;
};
