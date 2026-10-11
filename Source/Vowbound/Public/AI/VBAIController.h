// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "VBAIController.generated.h"

class UAISenseConfig_Sight;
class UVBEnemyConfig;

/**
 * Vowbound 적 AI 컨트롤러
 * Sight Perception으로 감지 -> Blackboard 업데이트 -> BT 실행
 */
UCLASS()
class VOWBOUND_API AVBAIController : public AAIController
{
	GENERATED_BODY()

public:
	AVBAIController();

	// 대상이 사망했을 때 그를 노리던 모든 AI 의 블랙보드 타겟을 즉시 비운다.
	// 지각 시스템은 사망 개념이 없다 — 시체도 계속 보이므로 stimulus 가 살아 있어 타겟이 스스로 풀리지 않고,
	// 적이 시체를 계속 때린다(공격 GA 는 State.Dead 대상을 트레이스에서 걸러 데미지는 0이지만 연출이 남는다).
	// 순회 로직을 AI 쪽에 두는 이유: 사망한 액터가 AI 내부 구조를 알 필요가 없게 하기 위함.
	static void NotifyActorDied(const UObject* WorldContextObject, AActor* DeadActor);

protected:
	virtual void OnPossess(APawn* InPawn) override;

	// 행동 트리 시작. 조우 문(UVBEncounterGateComponent)이 열릴 때도 여기로 온다.
	void StartBehavior();

	// 조우 보류(시네마틱 장면 동안)가 바뀌면 행동 트리를 멈추고 잇는다.
	void HandleEncounterHeldChanged(bool bHeld);

	// 빙의가 풀리거나 컨트롤러가 사라질 때 조우 문 구독을 푼다.
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 조우 문 구독(OnPossess 가 건다). 핸들로 풀어야 다시 빙의할 때 처리기가 겹치지 않는다.
	void UnbindEncounterGate();
	TWeakObjectPtr<class UVBEncounterGateComponent> BoundEncounterGate;
	FDelegateHandle EncounterHeldHandle;
	FDelegateHandle EncounterWokenHandle;

protected:
	virtual void BeginPlay() override;
	
	// Behaviour Tree (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|AI")
	TObjectPtr<UBehaviorTree> DefaultBehaviorTree;
	
	// BlackboardKey 이름 (BB에셋과 일치해야함)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|AI")
	FName TargetActorKeyName = "TargetActor";

	// 접근 정지 거리 키. 서비스(UVBBTService_UpdateCombat)가 0.2초마다 쓰지만, 계획이 바뀐 «그 순간» 에도 써야 한다 -
	//   MoveTo 는 시작할 때 이 키를 한 번 읽고, 토큰 부여 -> 데코레이터 재시작 -> MoveTo 시작이 서비스의 다음 주기보다 먼저 온다
	//   (design .sw/boss/design.md §12.11, 2026-09-17: 계획 245 인데 MoveTo 가 이전 값 320 으로 «이미 도착» 판정).
	//   루트 셀렉터의 서비스는 bCallTickOnSearchStart 가 켜져 있어도 부모가 이미 활성이라 재시작 때 안 돈다(BTService::NotifyParentActivation).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|AI")
	FName AttackRangeKeyName = "AttackRange";

public:
	// 접근 정지 거리를 블랙보드에 지금 쓴다. 전투 컴포넌트가 계획을 세우거나 비울 때 부른다. 블랙보드가 없으면 아무것도 안 한다.
	void PushAttackStartDistance(float Distance);

	// Sight Sense 설정 (Constructor에서 생성, GC-only — exposure 키워드 없이 UPROPERTY())
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;
	
	// Perception 감지 콜백
	UFUNCTION()
	void OnTargetPerceptionInfoUpdated(const FActorPerceptionUpdateInfo& UpdateInfo);
	
private:
	// 시야 수치를 SightConfig 에 쓰고 ConfigureSense 로 반영한다.
	// 생성자 시딩과 OnPossess 적용이 같은 절차를 밟아야 두 경로가 어긋나지 않으므로 함수로 뽑았다.
	void ApplyPerceptionConfig(const UVBEnemyConfig* Config);
};
