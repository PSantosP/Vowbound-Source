// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBPlayerRestoreComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVBOnRestoreCompleted);

/**
 * UVBPlayerRestoreComponent — 저장된 위치로 플레이어를 World Partition 스트리밍-안전하게 복원한다.
 *
 * 왜 필요한가:
 *   저장 좌표로 폰을 즉시 텔레포트하면 그 지역 지형이 아직 스트리밍되지 않아 바닥을 뚫고 낙하한다
 *   (기지 사례 spawn_land: z=192 스폰이 지면 90.2로 정렬되기 전 낙하). WP 셀은 비동기로 뜬다.
 *
 * 전략 (엔진 API 실측):
 *   1. 폰을 저장 XY로 텔레포트 → 폰은 PlayerController(IWorldPartitionStreamingSourceProvider)의 뷰타겟이라
 *      그 위치의 스트리밍이 자동 요청된다.
 *   2. 폰 동결(중력0 + MOVE_None + 콜리전 off)로 스트리밍 전 낙하/관통 방지.
 *   3. UWorldPartitionSubsystem::IsStreamingCompleted(Activated, ...) 를 폴링(타임아웃 폴백).
 *   4. 완료되면 아래로 라인트레이스해 실제 지면 Z 를 얻어 배치.
 *   5. 동결 해제 + OnRestoreCompleted.
 *
 * 위치: 플레이어 폰(AVBCharacter)에 부착. 서버/싱글에서 GameMode 가 BeginRestore 를 호출한다.
 * 부착 대상이 WP 미사용 맵이면 스트리밍 대기를 건너뛰고 지면 스냅만 수행.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBPlayerRestoreComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBPlayerRestoreComponent();

	// 저장된 Transform 으로 스트리밍-안전 복원을 시작한다. 소유 액터는 APawn(ACharacter) 이어야 한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Save")
	void BeginRestore(const FTransform& Target);

	// 복원 완료(지면 스냅 + 동결 해제) 시 브로드캐스트.
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Save")
	FVBOnRestoreCompleted OnRestoreCompleted;

	// 스트리밍 완료 대기 최대 시간(초). 초과 시 강제로 지면 스냅으로 진행(무한 대기 방지).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Save")
	float StreamingTimeoutSeconds = 10.0f;

	// 스트리밍 완료 폴링 주기(초). ClampMin 은 에디터 입력 하한 — 저작 단계에서 0 이 들어가는 것을 막는다.
	// 소비처(BeginRestore)의 런타임 하한은 별개로 남아 있다. 그쪽은 이 클램프가 생기기 전에 저장된
	// 값이나 코드 대입을 막는 엔진 계약 방어이지 튜닝값이 아니다(SetTimer 는 rate<=0 을 '해제'로 처리한다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Save", meta=(ClampMin="0.02"))
	float PollIntervalSeconds = 0.1f;

	// 지면 스냅 라인트레이스의 위쪽 시작 높이(cm, 저장 좌표 기준).
	// 저장 좌표가 실제 지면보다 조금 잠겨 있어도 위에서부터 훑어 내려와 지면을 찾게 한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Save")
	float GroundSnapTraceUpDistance = 200.0f;

	// 지면 스냅 라인트레이스의 아래쪽 끝 깊이(cm, 저장 좌표 기준).
	// 저장 시점보다 지형이 낮아졌거나 좌표가 공중일 때 어디까지 내려가 찾을지를 정한다.
	// 너무 짧으면 지면 미발견으로 저장 좌표 그대로 배치돼 다시 낙하한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Save")
	float GroundSnapTraceDownDistance = 500.0f;

	// 캡슐 컴포넌트를 못 얻었을 때 쓸 반높이(cm). UE 마네킹 기본값.
	// 소유자가 ACharacter 라 실제로는 항상 캡슐이 있고 이 값은 방어용이다. 캐릭터 스케일을 바꾸면 같이 바꾼다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Save")
	float FallbackCapsuleHalfHeight = 88.0f;

private:
	// 스트리밍 완료 여부를 주기적으로 확인. 완료/타임아웃 시 지면 스냅으로 마무리.
	void PollStreaming();

	// 지면으로 라인트레이스해 폰을 정렬하고 동결을 해제, 완료를 통지.
	void SnapToGroundAndFinish();

	// 폰 물리/이동/콜리전을 얼리거나(true) 되돌린다(false). 복원 중 낙하/관통 방지.
	void FreezePawn(bool bFreeze);

	// 복원 목표 Transform(폴링 완료 후 지면 스냅의 기준).
	FTransform PendingTarget;

	// 폴링 타이머 핸들 + 시작 시각(타임아웃 판정).
	FTimerHandle PollTimerHandle;
	double StreamStartTime = 0.0;

	// 동결 전 원래 GravityScale(해제 시 복원). 캐릭터가 아닐 때 대비 기본 1.
	float SavedGravityScale = 1.0f;

	// 복원 진행 중 재진입 방지.
	bool bRestoring = false;
};
