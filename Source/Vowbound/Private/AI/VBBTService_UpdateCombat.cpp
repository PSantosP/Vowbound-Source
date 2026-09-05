// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AI/VBBTService_UpdateCombat.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h" // ResolveSelectedKey 가 완전 타입을 요구한다
#include "GameFramework/Pawn.h"
#include "AI/VBCombatTypes.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Data/VBEnemyConfig.h" // AttackCommitDistance - 접근 정지 거리의 홈
#include "AbilitySystem/VBAbilitySystemStatics.h"

UVBBTService_UpdateCombat::UVBBTService_UpdateCombat(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	NodeName = "Update Combat Coordination";

	// TickNode 만 구현했다는 사실을 엔진에 알린다. 이 매크로가 없으면 bNotifyTick 이 꺼진 채로 남아
	// 서비스가 조용히 아무것도 하지 않는다.
	INIT_SERVICE_NODE_NOTIFY_FLAGS();

	// 관측 주기의 기본값. 최종 홈은 트리 자산의 노드 프로퍼티다 - 엔진이 거기서 직접 읽는다.
	Interval        = 0.2f;
	RandomDeviation = 0.0f;

	// 선택자가 받을 수 있는 키 타입을 제한한다. 잘못된 타입의 키를 꽂아 조용히 무동작이 되는 것을 막는다.
	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, TargetActorKey),
	                               AActor::StaticClass());
	// 열거형 키는 자산 enum(UBlackboardKeyType_Enum)과 네이티브 enum(UBlackboardKeyType_NativeEnum)
	// 두 표현이 있다. 어느 쪽으로 저작하든 꽂히도록 둘 다 허용한다.
	CombatRoleKey.AddEnumFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, CombatRoleKey),
	                            StaticEnum<EVBCombatRole>());
	CombatRoleKey.AddNativeEnumFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, CombatRoleKey),
	                                  TEXT("EVBCombatRole"));
	SlotLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, SlotLocationKey));
	DistanceToTargetKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, DistanceToTargetKey));
	AttackRangeKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UVBBTService_UpdateCombat, AttackRangeKey));
}

void UVBBTService_UpdateCombat::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	// 선택자를 실제 블랙보드 자산의 키 ID 로 해석한다. 이것을 빼먹으면 SelectedKeyName 은 맞는데
	// 내부 ID 가 무효라 읽기/쓰기가 전부 조용히 실패한다.
	if (const UBlackboardData* BlackboardAsset = GetBlackboardAsset())
	{
		TargetActorKey.ResolveSelectedKey(*BlackboardAsset);
		CombatRoleKey.ResolveSelectedKey(*BlackboardAsset);
		SlotLocationKey.ResolveSelectedKey(*BlackboardAsset);
		DistanceToTargetKey.ResolveSelectedKey(*BlackboardAsset);
		AttackRangeKey.ResolveSelectedKey(*BlackboardAsset);
	}
	else
	{
		TargetActorKey.InvalidateResolvedKey();
		CombatRoleKey.InvalidateResolvedKey();
		SlotLocationKey.InvalidateResolvedKey();
		DistanceToTargetKey.InvalidateResolvedKey();
		AttackRangeKey.InvalidateResolvedKey();
	}
}

void UVBBTService_UpdateCombat::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	AAIController* AIController = OwnerComp.GetAIOwner();
	APawn*         Pawn         = AIController ? AIController->GetPawn() : nullptr;
	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!AIController || !Pawn || !Blackboard)
	{
		return;
	}

	// 컴포넌트 조회는 첫 틱에 한 번만 한다. 캐시가 풀리는 경우는 둘 - 컴포넌트가 파괴됐거나(약참조 null),
	//   컨트롤러가 다른 폰을 빙의했거나. 후자는 약참조로는 안 잡히므로 소유자를 직접 대조한다.
	FVBUpdateCombatMemory*   Memory         = CastInstanceNodeMemory<FVBUpdateCombatMemory>(NodeMemory);
	UVBEnemyCombatComponent* CombatComponent = Memory ? Memory->CombatComponent.Get() : nullptr;
	if (CombatComponent && CombatComponent->GetOwner() != Pawn)
	{
		CombatComponent = nullptr;
	}
	if (!CombatComponent)
	{
		CombatComponent = Pawn->FindComponentByClass<UVBEnemyCombatComponent>();
		if (Memory)
		{
			Memory->CombatComponent = CombatComponent;
		}
	}
	if (!CombatComponent)
	{
		return;
	}

	// 사망 게이트. 적 BT 는 DeathCleanupDelay 동안 계속 돈다 - 시체가 그룹에 남아 토큰을 쥐거나
	// 슬롯을 차지하지 않게 한다. 포커스도 끊어야 시체가 계속 플레이어 쪽으로 돌려 세워지지 않는다.
	if (UVBAbilitySystemStatics::IsActorDead(Pawn))
	{
		CombatComponent->LeaveCombat();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		return;
	}

	AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName));

	// 블랙보드가 진실이다. 멱등이라 같은 값을 매 주기 넘겨도 기존 결정이 보존된다.
	CombatComponent->SetCombatTarget(Target);

	if (!Target)
	{
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		Blackboard->SetValueAsEnum(CombatRoleKey.SelectedKeyName, static_cast<uint8>(EVBCombatRole::Idle));
		return;
	}

	// AVBEnemyBase 생성자가 bUseControllerDesiredRotation=true 로 두었으므로 컨트롤러의 포커스가
	// 곧 폰의 회전 목표다. 포커스를 안 주면 적은 이동 방향만 보고, 슬롯에서 대기하는 적이 플레이어에게
	// 등을 돌린다. 회전 속도는 UVBEnemyConfig::RotationRate 가 이미 데이터로 갖고 있다.
	AIController->SetFocus(Target, EAIFocusPriority::Gameplay);

	// 접근 정지 거리. 이 적의 아키타입 자산이 정한 값을 그대로 내보낸다.
	// 왜 매 주기 쓰는가: 적마다 자산이 다르고 트리는 하나다. 한 번만 쓰면 다른 아키타입의 적이
	//   앞서 쓴 값을 물려받는다 - 블랙보드는 AI 인스턴스마다 따로지만 이 값은 폰이 바뀌면 낡는다.
	if (const UVBEnemyConfig* CombatConfig = CombatComponent->GetCombatConfig())
	{
		Blackboard->SetValueAsFloat(AttackRangeKey.SelectedKeyName, CombatConfig->AttackCommitDistance);
	}

	Blackboard->SetValueAsEnum(CombatRoleKey.SelectedKeyName, static_cast<uint8>(CombatComponent->GetRole()));
	Blackboard->SetValueAsVector(SlotLocationKey.SelectedKeyName, CombatComponent->GetSlotLocation());
	// 수평 거리. 계단/경사에서 수직차가 값을 튀게 만들면 진단값으로서 쓸모가 없어진다(역할 판정과 같은 기준).
	// 블랙보드 Float 키는 float 이라 명시 축소 변환한다(암묵 변환은 경고 -> 빌드 실패).
	Blackboard->SetValueAsFloat(DistanceToTargetKey.SelectedKeyName,
	                            static_cast<float>(FVector::Dist2D(Pawn->GetActorLocation(), Target->GetActorLocation())));
}

uint16 UVBBTService_UpdateCombat::GetInstanceMemorySize() const
{
	return sizeof(FVBUpdateCombatMemory);
}

void UVBBTService_UpdateCombat::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
                                                 EBTMemoryInit::Type InitType) const
{
	// placement new 로 생성자를 돌린다. UE 5.8 에서는 zero 초기화만으로도 유효한 null 이지만
	//   그것이 엔진 매크로(UE_WEAKOBJECTPTR_ZEROINIT_FIX)에 달린 사실이라 명시적으로 짓는다.
	//   상세 근거는 헤더 주석에 있다.
	new (NodeMemory) FVBUpdateCombatMemory();
}

void UVBBTService_UpdateCombat::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
                                              EBTMemoryClear::Type CleanupType) const
{
	CastInstanceNodeMemory<FVBUpdateCombatMemory>(NodeMemory)->~FVBUpdateCombatMemory();
}

FString UVBBTService_UpdateCombat::GetStaticServiceDescription() const
{
	return FString::Printf(TEXT("Target: %s -> Role/Token/Slot"), *TargetActorKey.SelectedKeyName.ToString());
}
