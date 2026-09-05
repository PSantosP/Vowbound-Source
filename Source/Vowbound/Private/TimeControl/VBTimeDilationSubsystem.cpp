// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "TimeControl/VBTimeDilationSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h" // MinGlobalTimeDilation (clamp 진단)
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Vowbound/Vowbound.h"

UVBTimeDilationSubsystem* UVBTimeDilationSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}
	// dilation 은 per-world → world 서브시스템을 그 world 에서 직접 얻는다(GI 아님).
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return World->GetSubsystem<UVBTimeDilationSubsystem>();
	}
	return nullptr;
}

void UVBTimeDilationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	AppliedScale = 1.0f; // 월드 시작은 정상 속도
}

void UVBTimeDilationSubsystem::Deinitialize()
{
	// 방어적 복원: 남은 요청/타이머 정리 + stale dilation 방지(월드 소멸 시).
	if (UWorld* World = GetWorld())
	{
		for (TPair<FName, FVBTimeDilationRequest>& Pair : ActiveRequests)
		{
			World->GetTimerManager().ClearTimer(Pair.Value.ExpiryTimer);
		}
		if (AppliedScale != 1.0f)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
		}
	}
	ActiveRequests.Empty();
	AppliedScale = 1.0f;
	Super::Deinitialize();
}

void UVBTimeDilationSubsystem::PushTimed(FName Handle, float Scale, float RealDuration)
{
	// duration 이 없으면 무기한처럼 도는 것을 막고 Push 로 위임(명시적 Pop 필요).
	if (RealDuration <= 0.0f)
	{
		Push(Handle, Scale);
		return;
	}

	// 전제: Scale > 0 (실시간 만료 증명은 D=Scale 가정). Scale<=0(완전 정지/프리즈)이면 GameDelay=0 이라
	// 다음 틱에 즉시 self-expire → RealDuration 을 못 버틴다. 프리즈-프레임은 게임시간 타이머로 만료 불가라
	// 별도 real-time 만료 메커니즘이 필요(미래 작업). 현 유일 클라(히트스톱 0.01)는 무관. 방어적으로 floor.
	Scale = FMath::Max(Scale, KINDA_SMALL_NUMBER);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClearExistingTimer(Handle); // 같은 handle 재요청이면 기존 타이머 먼저 정리

	FVBTimeDilationRequest Request;
	Request.Scale = Scale;

	// 실시간 만료: 타이머는 게임시간(dilated) 누적이므로, RealDuration 실초 후 발화하려면
	// GameDelay = RealDuration * Scale 로 건다. 지배(min-scale) 요청은 D=Scale 이라
	// real_elapsed = GameDelay/D = (RealDuration*Scale)/Scale = RealDuration. (현 큐 Duration*TimeDilation 과 동일 취지)
	const float GameDelay = RealDuration * Scale;
	World->GetTimerManager().SetTimer(
		Request.ExpiryTimer,
		FTimerDelegate::CreateUObject(this, &UVBTimeDilationSubsystem::OnRequestExpired, Handle),
		GameDelay,
		false);

	ActiveRequests.Add(Handle, Request); // 같은 handle 이면 교체(중첩 아님)
	Resolve();
}

void UVBTimeDilationSubsystem::Push(FName Handle, float Scale)
{
	ClearExistingTimer(Handle);

	FVBTimeDilationRequest Request;
	Request.Scale = Scale; // ExpiryTimer 무효 = 무기한(Pop 까지 유지)
	ActiveRequests.Add(Handle, Request);
	Resolve();
}

void UVBTimeDilationSubsystem::Pop(FName Handle)
{
	if (ActiveRequests.Contains(Handle))
	{
		ClearExistingTimer(Handle);
		ActiveRequests.Remove(Handle);
		Resolve();
	}
}

void UVBTimeDilationSubsystem::OnRequestExpired(FName Handle)
{
	ActiveRequests.Remove(Handle);
	Resolve();
}

void UVBTimeDilationSubsystem::ClearExistingTimer(FName Handle)
{
	if (FVBTimeDilationRequest* Request = ActiveRequests.Find(Handle))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(Request->ExpiryTimer);
		}
	}
}

void UVBTimeDilationSubsystem::Resolve()
{
	// min-wins: 가장 느린 활성 요청이 승. 비었으면 정상(1.0). 결과는 항상 요청된 scale 중 하나(clamp 안전).
	float Target = 1.0f;
	for (const TPair<FName, FVBTimeDilationRequest>& Pair : ActiveRequests)
	{
		Target = FMath::Min(Target, Pair.Value.Scale);
	}

	if (Target != AppliedScale)
	{
		if (UWorld* World = GetWorld())
		{
			// resolved 값이 WorldSettings 하한 아래면 엔진이 clamp → AppliedScale 이 실제 dilation 과 desync.
			// 현재 유일 클라(히트스톱 0.01)는 기본 하한 안이라 도달 불가 — 미래 caller(불릿타임/프리즈) 진단용.
			if (const AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				if (Target < WorldSettings->MinGlobalTimeDilation)
				{
					VB_LOG(Warning, "TimeDilation %.4f 가 WorldSettings 하한(%.4f) 아래 — 엔진 clamp, AppliedScale desync 가능",
						Target, WorldSettings->MinGlobalTimeDilation);
				}
			}
			UGameplayStatics::SetGlobalTimeDilation(World, Target);
			// World 유효할 때만 갱신 — null 이면 미적용이라 AppliedScale 을 올리면 이후 재적용을 놓친다(desync 방지).
			AppliedScale = Target;
		}
	}
}

void UVBTimeDilationSubsystem::SetActorDilation(AActor* Actor, float Scale)
{
	// 문서 컨벤션 — 지금 배선된 곳 없음. per-actor 배율(플레이어 빠름/적 느림)의 미래 진입점.
	if (Actor)
	{
		Actor->CustomTimeDilation = Scale;
	}
}
