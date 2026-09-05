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
	virtual void BeginPlay() override;
	
	// Behaviour Tree (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|AI")
	TObjectPtr<UBehaviorTree> DefaultBehaviorTree;
	
	// BlackboardKey 이름 (BB에셋과 일치해야함)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|AI")
	FName TargetActorKeyName = "TargetActor";

public:
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
