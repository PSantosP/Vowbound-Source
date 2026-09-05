// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBFunctionalTestBase.h"
#include "VBAirMantleInjectionTest.generated.h"

class UInputAction;

/**
 * AVBAirMantleInjectionTest — /pie-test 02-air-mantle-zero-input 패리티 (헤드리스)
 *
 * 유일하게 진짜 키 경로를 검증: Enhanced Input 연속 주입(Started) → HandleJumpPressed 지상 게이트
 * → 실점프 → bJumpInputHeld 1.5s 유지 → Tick 공중맨틀 → AIR 트래버설 → 자동 release(Completed)
 * → 재발동 없음. MCP 불요 — StartContinuousInputInjectionForAction 은 EnhancedInput 런타임 API.
 * 기준선: 2026-06-05 첫 공식 /pie-test 실행 (maxz=290.2, "Traversal AIR" 정확히 ×1).
 *
 * 명문화된 함정 (단순화 금지 — 02 시나리오 파일과 동일):
 *  - 시작점 벽에서 ~150cm: 지상 트레이스(75)가 미스 → tap/hold 가 실점프 유발. 공중 트레이스(100)는 접근 후 도달.
 *  - 전진 입력은 공중에서만 — 지상 전진은 속도비례 트레이스로 GROUND 트래버설이 선점.
 *  - IA 는 풀패스 필수 — NiagaraExamples 에 동명 IA 존재.
 *  - 피크 z 는 Tick 에서 표본화 (순간값 — 폴링 간격으론 놓침).
 */
UCLASS()
class AVBAirMantleInjectionTest : public AVBFunctionalTestBase
{
	GENERATED_BODY()

public:
	AVBAirMantleInjectionTest();

	virtual void StartTest() override;
	virtual void Tick(float DeltaSeconds) override;

protected:
	// 풀패스 디폴트 — 생성자에서 설정. 맵 인스턴스에서 교체 가능(EditAnywhere).
	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	TSoftObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	FVector StartLocation = FVector(-1050.0, 0.0, 90.15);

	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	double StartYaw = 180.0;

	// 점프 키 홀드 시간 — bJumpInputHeld 공중맨틀 게이트 구동 (1.5s = 증명값).
	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	float HoldSeconds = 1.5f;

	// 2.0m 벽 상단 기준 피크 z (증명값 290.2 — 여유 285).
	UPROPERTY(EditAnywhere, Category="Vowbound|Test")
	double MinPeakZ = 285.0;

private:
	void StopInjection();
	void PollCompletion();

	// GC-only 내부 참조 — 주입 지속 동안 IA 수명 보장 (bare UPROPERTY: Category 금지 규칙).
	UPROPERTY()
	TObjectPtr<UInputAction> ResolvedAction;

	bool bDriving = false;     // StartTest 완료 후에만 Tick 로직 가동
	bool bInjectionStopped = false;
	double MaxZ = 0.0;
	double Elapsed = 0.0;
	int32 DroveFrames = 0;     // 공중 드라이브 발화 횟수 — "점프 자체가 안 일어남" 진단 분리용
	TUniquePtr<FVBScopedLogCapture> LogCapture;
	FTimerHandle StopHandle;
	FTimerHandle PollHandle;
};
