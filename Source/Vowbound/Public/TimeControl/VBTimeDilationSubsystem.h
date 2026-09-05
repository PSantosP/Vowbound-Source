// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // FTimerHandle
#include "Subsystems/WorldSubsystem.h"
#include "VBTimeDilationSubsystem.generated.h"

/**
 * 활성 전역 dilation 요청 하나.
 * 왜 struct: scale + 만료 타이머를 handle 로 묶어 idempotent 하게 교체/제거한다.
 */
USTRUCT()
struct FVBTimeDilationRequest
{
	GENERATED_BODY()

	// 이 요청이 원하는 전역 시간 배율 (1.0 = 정상, 0.01 = 거의 정지).
	float Scale = 1.0f;

	// timed 요청의 자동 만료 타이머. 무기한(Push) 요청이면 무효 핸들.
	FTimerHandle ExpiryTimer;
};

/**
 * 시간 제어 조정 계층 (프로젝트 첫 UWorldSubsystem).
 * 무엇: 모든 '전역' 시간 dilation write 의 단일 소유자. 여러 시스템(히트스톱/미래 불릿타임/슬로모)이
 *       SetGlobalTimeDilation 을 직접 호출하면 서로 덮어쓰는(clobber) 문제를 handle 기반 요청 스택으로 해소.
 * 왜 UWorldSubsystem: dilation 은 per-world 상태 — 월드와 함께 생성/소멸해 레벨 전환 시 stale dilation 이 없다.
 * 해소 정책: min-wins(가장 느린 활성 요청이 승). 결과는 항상 요청된 scale 중 하나라 WorldSettings clamp 안전.
 * 만료: SetTimer 는 게임시간(dilated) 기준이라, timed 요청은 GameDelay=RealDuration*Scale 로 걸어
 *       실제 wall-clock RealDuration 후 발화하게 한다(real_elapsed=GameDelay/D=(RD*Scale)/Scale=RD).
 * 범위: SP/host. SetGlobalTimeDilation 은 서버 권위·복제 안 됨 — 히트스톱 큐는 각 클라 로컬 실행이라 정합. MP 범위 밖.
 * 부작용: SetGlobalTimeDilation 호출(변화 시만). 신규 복제/RPC 없음.
 */
UCLASS()
class VOWBOUND_API UVBTimeDilationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 어디서나 접근하는 정적 헬퍼. WorldContext 로 UWorld 를 찾아 그 world 의 서브시스템을 반환.
	UFUNCTION(BlueprintPure, Category="Vowbound|TimeControl", meta=(WorldContext="WorldContextObject"))
	static UVBTimeDilationSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// 지정 실시간(초) 동안만 전역 dilation 요청. 만료 시 조정계층이 자동 제거. (히트스톱 경로)
	// 같은 Handle 재요청 = 자기 자신 교체(중첩 아님) → duration 연장.
	UFUNCTION(BlueprintCallable, Category="Vowbound|TimeControl")
	void PushTimed(FName Handle, float Scale, float RealDuration);

	// 무기한 전역 dilation 요청. 명시적 Pop 까지 유지. (미래 불릿타임 GA 활성/종료 경로)
	UFUNCTION(BlueprintCallable, Category="Vowbound|TimeControl")
	void Push(FName Handle, float Scale);

	// 해당 Handle 요청 제거 후 재해소. 없는 Handle 이면 무동작.
	UFUNCTION(BlueprintCallable, Category="Vowbound|TimeControl")
	void Pop(FName Handle);

	// [문서 컨벤션 — 지금 어디에도 연결 안 됨] per-actor 배율의 미래 진입점.
	// 플레이어 빠름/적 느림은 전역 dilation 위에 곱해지는 Actor->CustomTimeDilation 로 구현될 예정.
	// YAGNI: 현재 미사용. 불릿타임/위치타임 구현 시 여기서 확장한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|TimeControl")
	static void SetActorDilation(AActor* Actor, float Scale);

private:
	// 활성 요청. min-wins 로 해소. Handle 로 idempotent 교체. (UObject ref 없어 UPROPERTY 불요)
	TMap<FName, FVBTimeDilationRequest> ActiveRequests;

	// 마지막으로 실제 적용한 배율. 변화 시에만 SetGlobalTimeDilation 호출(중복 write 방지).
	float AppliedScale = 1.0f;

	// 활성 요청의 min(없으면 1.0)을 계산해, 변했을 때만 적용.
	void Resolve();

	// Handle 만료 콜백(타이머). 요청 제거 후 Resolve.
	void OnRequestExpired(FName Handle);

	// Handle 의 기존 만료 타이머를 clear(있으면). 교체/제거 전 호출.
	void ClearExistingTimer(FName Handle);
};
