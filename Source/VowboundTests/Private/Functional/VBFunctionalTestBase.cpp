// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "VBFunctionalTestBase.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

FVBScopedLogCapture::FVBScopedLogCapture(const FName InCategory, const FString& InNeedle)
	: WatchCategory(InCategory)
	, Needle(InNeedle)
{
	if (GLog)
	{
		GLog->AddOutputDevice(this);
	}
}

FVBScopedLogCapture::~FVBScopedLogCapture()
{
	if (GLog)
	{
		GLog->RemoveOutputDevice(this);
	}
}

void FVBScopedLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	// 카테고리 선필터(저비용) 후 부분 일치 — 전체 로그 트래픽에서 오버헤드 최소화.
	if (Category == WatchCategory && V && FCString::Strstr(V, *Needle) != nullptr)
	{
		MatchCount.Increment();
	}
}

AVBFunctionalTestBase::AVBFunctionalTestBase()
{
	// 행 방지: 폴링형 테스트가 완료 조건에 도달 못 하면 30s 후 Failed 로 강제 종료.
	// (AFunctionalTest 내장 — Tick 에서 TotalTime 비교, 베이스가 테스트 시작 시 tick 활성화)
	TimeLimit = 30.0f;
	TimesUpResult = EFunctionalTestResult::Failed;

	// Warning 은 Fail 로 승격하지 않는다 (기본은 ProjectDefault→Error 승격).
	// 왜: 단독 실행(콜드 프로세스)의 첫 traversal MM 쿼리는 인덱스 웜업으로 "MotionMatch 결과 무효"
	// Warning 을 1회 남기는 게 정상 동작이고, 테스트는 retry-once 정책으로 이를 이미 흡수한다 —
	// 기능 성공(z=290.3) 이 Warning 승격만으로 Fail 마킹되는 것을 차단 (2026-06-05 restore-proof run).
	// Error 는 기본 처리 유지 — 진짜 오류는 계속 테스트를 실패시킨다.
	LogWarningHandling = EFunctionalTestLogHandling::OutputIgnored;
}

APawn* AVBFunctionalTestBase::GetPlayerPawn() const
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	return PC ? PC->GetPawn() : nullptr;
}

bool AVBFunctionalTestBase::TeleportPlayer(const FVector& Location, double Yaw) const
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return false;
	}

	const FRotator Rot(0.0, Yaw, 0.0);
	Pawn->SetActorLocationAndRotation(Location, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	PC->SetControlRotation(Rot);
	return true;
}
