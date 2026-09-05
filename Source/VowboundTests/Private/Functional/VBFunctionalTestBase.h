// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FunctionalTest.h"
#include "Misc/OutputDevice.h"
#include "HAL/ThreadSafeCounter.h"
#include "VBFunctionalTestBase.generated.h"

/**
 * FVBScopedLogCapture
 *
 * /pie-test 의 로그 스코프 어설션(tail -n +MARK | grep)의 인프로세스 등가물.
 * 왜: 헤드리스(-game)에선 외부 프로세스가 로그 마크를 운영할 수 없음 → GLog 출력 디바이스로
 * 직접 계수한다. 생성 시 등록, 소멸 시 해제 (RAII — 테스트 실패/타임아웃 경로에서도 누수 없음).
 * 스레드 안전: UE 로그는 임의 스레드에서 올 수 있어 FThreadSafeCounter 사용.
 */
class FVBScopedLogCapture : public FOutputDevice
{
public:
	FVBScopedLogCapture(const FName InCategory, const FString& InNeedle);
	virtual ~FVBScopedLogCapture() override;

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

	int32 Count() const { return MatchCount.GetValue(); }

private:
	FName WatchCategory;       // 감시 카테고리 (예: "LogVB")
	FString Needle;            // 라인 내 부분 일치 문자열 (예: "Traversal AIR")
	FThreadSafeCounter MatchCount;
};

/**
 * AVBFunctionalTestBase
 *
 * Vowbound 펑셔널 테스트 공통 베이스 — /pie-test 시나리오의 SETUP 등가 헬퍼 제공.
 * TimeLimit 기본 30s(행 방지), 타임아웃 = Failed (AFunctionalTest 내장 메커니즘 사용).
 */
UCLASS(Abstract)
class AVBFunctionalTestBase : public AFunctionalTest
{
	GENERATED_BODY()

public:
	AVBFunctionalTestBase();

protected:
	// 플레이어 0 폰. 없으면 nullptr (호출처가 FinishTest(Failed) 처리).
	APawn* GetPlayerPawn() const;

	// /pie-test SETUP 등가: 폰 텔레포트 + 폰/컨트롤러 yaw 동시 정렬.
	// 왜 ControlRotation 포함: 트래버설 전방 트레이스는 폰 방향 기준 — 컨트롤러만 돌면 폰이 따라돌며 흔들림.
	bool TeleportPlayer(const FVector& Location, double Yaw) const;
};
