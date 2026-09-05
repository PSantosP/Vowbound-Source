// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "VBBTService_UpdateCombat.generated.h"

class UVBEnemyCombatComponent;

/**
 * 서비스 노드 인스턴스별 메모리. 전투 컴포넌트 조회 결과를 캐시한다.
 * 왜 필요한가: FindComponentByClass 는 소유 컴포넌트 배열 선형 탐색이라, 주기마다 적 수만큼
 *   되풀이할 이유가 없다. 트리 자산은 AI 여러 기가 공유하므로 캐시는 노드가 아니라 여기 있어야 한다.
 */
struct FVBUpdateCombatMemory
{
	TWeakObjectPtr<UVBEnemyCombatComponent> CombatComponent;
};

/**
 * 코디네이터의 결정을 블랙보드로 옮기는 유일한 통로 (프로젝트 최초의 BTService).
 *
 * 무엇: 블랙보드의 TargetActor 를 진실로 삼아 매 주기 UVBEnemyCombatComponent 에 반영하고,
 *       그 결과(역할/토큰/슬롯/거리)를 블랙보드에 되쓴다. AI 포커스도 여기서 준다.
 * 왜 서비스인가: 태스크는 실행 흐름에 묶여 있어 "어느 분기에 있든 항상 도는 관측"을 표현할 수 없다.
 *       진입/이탈 이벤트가 아니라 주기적 조정이라야 이벤트를 놓치는 경우가 구조적으로 사라진다.
 * 왜 컨트롤러가 아닌가: AVBAIController 가 진입/이탈을 부르면 해제 경로가 둘(지각 콜백 / 사망 통지)이 되어
 *       한쪽이 빠진다. BB 를 진실로 삼는 멱등 조정이면 진입점이 하나다.
 * Tick: 신규 틱 소스가 아니다 - BT 컴포넌트가 이미 도는 틱에 얹힌다. 주기는 노드의 Interval 이 정한다.
 * 부작용: 컴포넌트의 코디네이터 등록 상태 변경, 블랙보드 4키 쓰기, AI 포커스 설정/해제.
 */
UCLASS()
class VOWBOUND_API UVBBTService_UpdateCombat : public UBTService
{
	GENERATED_BODY()

public:
	UVBBTService_UpdateCombat(const FObjectInitializer& ObjectInitializer);

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticServiceDescription() const override;

	// 노드 인스턴스 메모리 3종.
	// 사실 관계(UE 5.8 엔진 소스 확인): WeakObjectPtr.h 의 UE_WEAKOBJECTPTR_ZEROINIT_FIX 가 1 이고
	//   InvalidWeakObjectIndex 가 0, FWeakObjectPtr 기본 생성자가 = default 다. 즉 이 버전에서는
	//   0 바이트가 곧 유효한 null 이라 생성자를 안 돌려도 동작한다.
	// 그럼에도 직접 돌리는 이유: 그 안전성이 엔진 매크로 하나에 달려 있고 우리는 그것을 소유하지 않는다.
	//   매크로가 뒤집히거나 이 구조체에 non-trivial 멤버가 하나 늘면 조용히 깨지는데, 그때 증상은
	//   "가끔 컴포넌트를 못 찾는다"로만 나타난다. 값싼 명시가 조용한 의존보다 낫다.
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;

	// 블랙보드 키 선택자. 이름을 C++ 상수로 박지 않는 이유: 키 이름은 자산 저작이고,
	//   상수로 박으면 블랙보드에서 키를 하나 고칠 때 빌드가 필요해진다.

	// 읽기 전용. 이 키가 이 서비스에게는 전투의 유일한 진실이다.
	UPROPERTY(EditAnywhere, Category="Vowbound|AI")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, Category="Vowbound|AI")
	FBlackboardKeySelector CombatRoleKey;

	UPROPERTY(EditAnywhere, Category="Vowbound|AI")
	FBlackboardKeySelector SlotLocationKey;

	// 진단 전용. 트리는 이 값으로 분기하지 않는다 - 분기하는 순간 임계값이 트리에도 생겨
	//   AttackCommitDistance 의 홈이 둘이 된다. 거리 판단의 결과는 CombatRole 이 이미 나른다.
	//   PIE 가 user 전용이라 관측 지점을 자산에 남겨 두는 것이 특히 중요하다(AI Debug 로 눈으로 본다).
	UPROPERTY(EditAnywhere, Category="Vowbound|AI")
	FBlackboardKeySelector DistanceToTargetKey;

	// 접근 태스크가 어디서 멈춰야 하는지. 값의 홈은 UVBEnemyConfig::AttackCommitDistance 이고
	//   이 키는 운반 수단일 뿐이다(SlotLocationKey 가 코디네이터의 슬롯을 나르는 것과 같다).
	// 왜 트리에 상수로 두지 않는가: 무기/적을 하나 더 만들 때마다 트리를 고쳐야 하고,
	//   같은 거리가 역할 판정(C++)과 정지 판정(자산) 두 곳에서 결정된다.
	// 이것은 DistanceToTargetKey 의 '진단 전용' 규칙을 어기지 않는다 - 그쪽은 관측값을 내보내
	//   트리가 임계값을 새로 갖는 것을 막는 얘기고, 이쪽은 임계값 자체를 자산에서 내려보낸다.
	UPROPERTY(EditAnywhere, Category="Vowbound|AI")
	FBlackboardKeySelector AttackRangeKey;
};
