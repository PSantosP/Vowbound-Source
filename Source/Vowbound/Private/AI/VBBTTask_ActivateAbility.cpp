// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AI/VBBTTask_ActivateAbility.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AI/VBEnemyCombatComponent.h" // GetAttackInputTag - 태그 해석의 홈
#include "Vowbound/Vowbound.h"

UVBBTTask_ActivateAbility::UVBBTTask_ActivateAbility()
{
	NodeName = "Activate Ability";

	// bWaitForAbilityEnd 경로가 ASC 델리게이트와 대기 상태를 노드에 들고 있어야 한다.
	// 템플릿 공유 노드면 AI 여러 기가 같은 상태를 밟으므로 인스턴싱한다(BTTask_PlayAnimation 선례).
	// bWaitForAbilityEnd=false 경로의 동작은 이 플래그와 무관하다 - 상태를 쓰지 않기 때문이다.
	bCreateNodeInstance = true;
}

FGameplayTag UVBBTTask_ActivateAbility::ResolveAbilityTag(const APawn* Pawn) const
{
	// 노드 설정이 우선이다 - "이 노드에서는 반드시 이 어빌리티"라는 스크립트 연출을 표현할 자리.
	if (AbilityTag.IsValid())
	{
		return AbilityTag;
	}
	// 비어 있으면 이 적의 전투 컴포넌트에 묻는다. 자산을 여기서 직접 읽지 않는 것이 요점이다 -
	// 코디네이터의 준비 판정(UVBEnemyCombatComponent::IsReadyToAttack)이 같은 함수를 쓰므로
	// '준비를 묻는 어빌리티'와 '발동하는 어빌리티'가 같은 답에서 나온다.
	// 종전에는 여기가 자산을 직접 읽어, 두 소비자가 조용히 다른 GA 를 가리킬 수 있었다(감사 C3).
	if (const UVBEnemyCombatComponent* CombatComponent =
			Pawn ? Pawn->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr)
	{
		return CombatComponent->GetAttackInputTag();
	}
	return FGameplayTag();
}

EBTNodeResult::Type UVBBTTask_ActivateAbility::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 1. Pawn 가져오기
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController)
	{
		return EBTNodeResult::Failed;
	}

	APawn* Pawn = AIController->GetPawn();
	if (!Pawn)
	{
		return EBTNodeResult::Failed;
	}

	// 2. ASC 가져오기 (획득의 홈은 UVBAbilitySystemStatics 와 같은 엔진 정본)
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	if (!ASC)
	{
		return EBTNodeResult::Failed;
	}

	// 3. 발동할 InputTag 해석 (노드 -> 아키타입 자산 2단 사다리)
	const FGameplayTag ResolvedTag = ResolveAbilityTag(Pawn);
	if (!ResolvedTag.IsValid())
	{
		VB_LOG(Warning, "BT ActivateAbility : InputTag 미지정 - 노드의 AbilityTag 도 %s 의 AttackAbilityInputTag 도 비었다",
		       *Pawn->GetName());
		return EBTNodeResult::Failed;
	}

	// 4. 스펙 조회 (순회와 ABILITYLIST 잠금의 홈은 UVBAbilitySystemStatics 하나)
	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, ResolvedTag);
	if (!Spec)
	{
		VB_LOG(Warning, "BT ActivateAbility : InputTag '%s' 매칭 Ability 없음", *ResolvedTag.ToString());
		return EBTNodeResult::Failed;
	}
	// 반환 포인터는 배열 원소를 가리킨다. 활성화가 배열을 흔들 수 있으므로 핸들만 복사해 두고 버린다.
	const FGameplayAbilitySpecHandle SpecHandle = Spec->Handle;

	if (!bWaitForAbilityEnd)
	{
		const bool bSucceeded = ASC->TryActivateAbility(SpecHandle);
		VB_LOG(Log, "BT ActivateAbility : InputTag %s -> %s",
		       *ResolvedTag.ToString(), bSucceeded ? TEXT("성공") : TEXT("실패"));
		return bSucceeded ? EBTNodeResult::Succeeded : EBTNodeResult::Failed;
	}

	// 대기 경로. 구독을 활성화보다 '먼저' 건다 - 어빌리티가 같은 스택에서 즉시 끝나는 경로가 실재하고,
	// 나중에 걸면 그 종료를 통째로 놓쳐 태스크가 영영 InProgress 로 남는다.
	WaitingSpecHandle     = SpecHandle;
	CachedOwnerComp       = &OwnerComp;
	BoundASC              = ASC;
	bWaitingForAbilityEnd = true;
	bEndedDuringExecute   = false;
	bLastEndWasCancelled  = false;
	AbilityEndedHandle    = ASC->OnAbilityEnded.AddUObject(this, &UVBBTTask_ActivateAbility::OnAbilityEnded);

	bInsideExecute = true;
	const bool bSucceeded = ASC->TryActivateAbility(SpecHandle);
	bInsideExecute = false;

	VB_LOG(Log, "BT ActivateAbility : InputTag %s -> %s (종료 대기)",
	       *ResolvedTag.ToString(), bSucceeded ? TEXT("성공") : TEXT("실패"));

	if (!bSucceeded)
	{
		UnbindAbilityEnded();
		bWaitingForAbilityEnd = false;
		return EBTNodeResult::Failed;
	}

	if (bEndedDuringExecute)
	{
		// 활성화와 같은 스택에서 이미 끝났다. FinishLatentTask 는 부를 수 없으므로 반환값으로 답한다.
		return (bLastEndWasCancelled && !bSucceedOnCancel) ? EBTNodeResult::Failed : EBTNodeResult::Succeeded;
	}

	return EBTNodeResult::InProgress;
}

void UVBBTTask_ActivateAbility::OnAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (!bWaitingForAbilityEnd || EndedData.AbilitySpecHandle != WaitingSpecHandle)
	{
		return; // 다른 어빌리티의 종료이거나 이미 처리한 종료다.
	}

	bWaitingForAbilityEnd = false;
	bLastEndWasCancelled  = EndedData.bWasCancelled;
	UnbindAbilityEnded();

	if (bInsideExecute)
	{
		// ExecuteTask 가 아직 반환하지 않았다. 여기서 끝내면 시작하지도 않은 latent 태스크를 끝내는 셈이다.
		bEndedDuringExecute = true;
		return;
	}

	if (UBehaviorTreeComponent* OwnerComp = CachedOwnerComp.Get())
	{
		const EBTNodeResult::Type Result = (bLastEndWasCancelled && !bSucceedOnCancel)
			                                   ? EBTNodeResult::Failed
			                                   : EBTNodeResult::Succeeded;
		FinishLatentTask(*OwnerComp, Result);
	}
}

void UVBBTTask_ActivateAbility::UnbindAbilityEnded()
{
	if (!AbilityEndedHandle.IsValid())
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->OnAbilityEnded.Remove(AbilityEndedHandle);
	}
	AbilityEndedHandle.Reset();
	BoundASC.Reset();
}

EBTNodeResult::Type UVBBTTask_ActivateAbility::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 진행 중인 어빌리티를 취소하지는 않는다 - 역할이 바뀌었다고 스윙을 끊는 것은 별개의 게임 결정이다.
	// 여기서 하는 일은 이 노드가 더 이상 그 종료를 듣지 않게 만드는 것 하나다.
	UnbindAbilityEnded();
	bWaitingForAbilityEnd = false;
	return EBTNodeResult::Aborted;
}

void UVBBTTask_ActivateAbility::OnTaskFinished(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
                                               EBTNodeResult::Type TaskResult)
{
	// 성공/실패/중단이 전부 여기로 수렴한다. AbortTask 와 이중으로 해제해도 안전하다(핸들 무효 검사).
	UnbindAbilityEnded();
	bWaitingForAbilityEnd = false;
	CachedOwnerComp.Reset();
	Super::OnTaskFinished(OwnerComp, NodeMemory, TaskResult);
}

FString UVBBTTask_ActivateAbility::GetStaticDescription() const
{
	const FString TagText = AbilityTag.IsValid() ? AbilityTag.ToString() : TEXT("(EnemyConfig)");
	return bWaitForAbilityEnd
		       ? FString::Printf(TEXT("Activate InputTag: %s (wait for end)"), *TagText)
		       : FString::Printf(TEXT("Activate InputTag: %s"), *TagText);
}
