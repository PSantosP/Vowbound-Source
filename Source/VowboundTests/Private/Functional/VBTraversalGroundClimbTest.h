// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBFunctionalTestBase.h"
#include "VBTraversalGroundClimbTest.generated.h"

/**
 * AVBTraversalGroundClimbTest — /pie-test 01-traversal-ground-climb 패리티 (헤드리스)
 *
 * GROUND 트래버설 전체 체인(트레이스→chooser→몽타주→워프→복구)을 입력 레이어 없이 검증.
 * 직접 호출이 입력 게이트를 우회하는 것은 설계 의도 — 키 경로는 AVBAirMantleInjectionTest 가 담당.
 * 기준선: 2026-06-05 첫 공식 /pie-test 실행 (z=290.3, GROUND 텔레메트리 ×1).
 */
UCLASS()
class AVBTraversalGroundClimbTest : public AVBFunctionalTestBase
{
	GENERATED_BODY()

public:
	AVBTraversalGroundClimbTest();

	virtual void StartTest() override;

protected:
	// 증명 좌표: 2.0m 벽 전방, -X 방향. z=90.15 = 정착 지상 z (PlayerStart z 사용 금지 — proven pitfall).
	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	FVector StartLocation = FVector(-1130.0, 0.0, 90.15);

	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	double StartYaw = 180.0;

	// 2.0m 벽 상단 z≈290 — 미달이면 클라임 실패로 판정.
	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	double MinTopZ = 270.0;

	// MM 인덱스 웜업 retry-once 지연 (proven rule: 재시작 후 1차 False 허용, 2차 False = 진짜 FAIL).
	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	float RetryDelaySec = 2.0f;

private:
	// 트리거 시도 — 실패 시 1회 한정 재시도 타이머 예약.
	void AttemptTraversal();
	// 0.5s 간격 완료 폴링: 몽타주 종료 + z>MinTopZ + 텔레메트리 ≥1 → Succeeded.
	void PollCompletion();

	bool bRetried = false;
	TUniquePtr<FVBScopedLogCapture> LogCapture;
	FTimerHandle RetryHandle;
	FTimerHandle PollHandle;
};
