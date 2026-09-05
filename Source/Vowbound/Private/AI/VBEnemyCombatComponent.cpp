// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AI/VBEnemyCombatComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h" // UGameplayAbility::GetCooldownGameplayEffect 의 반환 타입
#include "GameFramework/Actor.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "Vowbound/Vowbound.h" // VB_LOG

UVBEnemyCombatComponent::UVBEnemyCombatComponent()
{
	// 이 컴포넌트는 관측도 조율도 하지 않는다. 관측은 BTService 주기, 조율은 코디네이터 타이머가 한다.
	// 틱을 켜면 적 수만큼 아무것도 안 하는 틱이 생긴다(GDD 32 GT 6ms 예산).
	PrimaryComponentTick.bCanEverTick = false;

	// SetIsReplicated 를 호출하지 않는다. 역할/토큰/슬롯은 순수 서버 결정 상태이고 클라가 읽을 이유가 없다.
}

void UVBEnemyCombatComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// 조율은 서버 결정이다. 클라에서 구독하면 로컬 태그 변화로 없는 토큰을 반납하려 든다.
		return;
	}

	if (UAbilitySystemComponent* ASC = GetOwnerASC())
	{
		// 토큰 반납의 유일한 정상 경로. 공격 GA 가 어떤 이유로 끝나든(완료/중단/취소/경직/사망)
		// 엔진이 ActivationOwnedTags 를 제거하므로 모든 종료 경로가 이 콜백 하나로 수렴한다.
		AttackingTagHandle = ASC->RegisterGameplayTagEvent(
			                        VBGameplayTags::State_Combat_Attacking,
			                        EGameplayTagEventType::NewOrRemoved)
		                        .AddUObject(this, &UVBEnemyCombatComponent::OnAttackingTagChanged);
	}
}

void UVBEnemyCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AttackingTagHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			ASC->UnregisterGameplayTagEvent(AttackingTagHandle,
			                                VBGameplayTags::State_Combat_Attacking,
			                                EGameplayTagEventType::NewOrRemoved);
		}
		AttackingTagHandle.Reset();
	}

	// 그룹에 시체/파괴된 멤버가 남지 않게 한다. 남아도 코디네이터가 다음 주기에 걷어내지만,
	// 그 사이 재계산 한 번이 이미 죽은 멤버를 후보로 세는 것을 피한다.
	LeaveCombat();

	Super::EndPlay(EndPlayReason);
}

UAbilitySystemComponent* UVBEnemyCombatComponent::GetOwnerASC() const
{
	// 획득 방법을 여기 하나로 고정한다. BeginPlay 와 EndPlay 가 다른 방법을 쓰면 구독/해제가 어긋난다.
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

void UVBEnemyCombatComponent::SetCombatTarget(AActor* NewTarget)
{
	UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this);

	if (CombatTarget.Get() != NewTarget)
	{
		// 타겟이 바뀌었다: 이전 그룹에서 먼저 빠진다(한 멤버가 두 그룹에 동시에 들지 않게).
		// 등록 해제는 CombatTarget 을 키로 쓰므로 갱신보다 반드시 먼저다.
		if (Coordinator)
		{
			Coordinator->UnregisterCombatant(this);
		}
		CombatTarget = NewTarget;
	}

	if (!NewTarget)
	{
		return; // 이탈 완료. 위 등록 해제가 그룹에서 뺐다.
	}

	// 같은 타겟을 매 주기 다시 넘겨도 안전하다 - 등록이 멱등이라 기존 결정이 보존된다.
	// 매번 호출하는 것이 요점이다: 타겟이 죽어 그룹이 해체된 뒤 다시 살아나는 경로(재타겟)에서
	// "한 번 등록했으니 됐다"는 가정이 조용히 깨지는 것을 구조적으로 막는다.
	if (Coordinator)
	{
		Coordinator->RegisterCombatant(this, NewTarget);
	}
}

void UVBEnemyCombatComponent::LeaveCombat()
{
	// 공격 에지 래치를 함께 지운다. 안 지우면 공격 중 타겟이 바뀌었을 때 새 그룹에서 하강 에지가 찍혀,
	// 쥐지도 않은 토큰에 대해 ReleaseAttackToken 이 돈다. 그러면 그 멤버의 상태가 초기화되고
	// 그룹 재배분이 근거 없이 한 번 더 일어나며, "토큰 반납" 로그가 허위 지속시간과 함께 남는다.
	bWasAttacking = false;
	SetCombatTarget(nullptr);
}

EVBCombatRole UVBEnemyCombatComponent::GetRole() const
{
	if (const UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		return Coordinator->GetAssignedRole(this);
	}
	return EVBCombatRole::Idle;
}

bool UVBEnemyCombatComponent::HasAttackToken() const
{
	if (const UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		return Coordinator->GetHeldTokenCost(this) > 0;
	}
	return false;
}

FVector UVBEnemyCombatComponent::GetSlotLocation() const
{
	if (const UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		return Coordinator->GetAssignedSlotLocation(this);
	}
	const AActor* Owner = GetOwner();
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

bool UVBEnemyCombatComponent::IsReadyToAttack() const
{
	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		return false;
	}

	const FGameplayTag AttackInputTag = GetAttackInputTag();
	if (!AttackInputTag.IsValid())
	{
		// 태그 미저작이면 이 적은 공격 어빌리티를 못 고른다. 여기서 경고하지 않는 이유는
		// 이 함수가 코디네이터 재계산 주기(0.5초)마다 불려 같은 사실을 무한 반복하기 때문이다.
		// 같은 부재를 부팅 시 ValidateAttackAbilityContract 가 1회 경고한다.
		//
		// 종전 주석은 "BT 태스크가 이미 경고한다"고 적었으나 사실과 반대였다(2026-08-20 감사에서 정정).
		// BT 태스크는 토큰의 하류다 - 이 함수가 false 를 돌려주면 토큰이 안 나오고, 역할이 Surrounder 로
		// 고정되어 BT 의 공격 분기에 애초에 도달하지 못한다. 상류의 침묵을 하류가 대신 외칠 수 없다.
		return false;
	}

	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, AttackInputTag);
	if (!Spec || !Spec->Ability)
	{
		return false;
	}

	// 인스턴스가 있으면 그것을, 없으면 CDO 를 묻는다(엔진 InternalTryActivateAbility 와 같은 선택).
	// 삼항에 바로 넣지 않는 이유: GetPrimaryInstance() 는 생 포인터인데 Spec->Ability 는 TObjectPtr 라
	//   공통 타입이 정해지지 않는다(C2445). 지역변수로 받아 타입을 하나로 맞춘다.
	//   부수 이득으로 GetPrimaryInstance() 를 두 번 부르지 않는다.
	const UGameplayAbility* PrimaryInstance = Spec->GetPrimaryInstance();
	const UGameplayAbility* AbilityToQuery  = PrimaryInstance ? PrimaryInstance : Spec->Ability.Get();
	return AbilityToQuery->CanActivateAbility(Spec->Handle, ASC->AbilityActorInfo.Get());
}

FGameplayTag UVBEnemyCombatComponent::GetAttackInputTag() const
{
	// 홈은 아키타입 자산 하나다. 하나뿐인 트리가 여러 아키타입을 태우므로 태그가 트리에 있으면
	// 아키타입을 더할 때 트리를 통째로 복제해야 한다(VBEnemyConfig.h 의 판단).
	return GetCombatConfig()->AttackAbilityInputTag; // GetCombatConfig 는 절대 null 아님
}

bool UVBEnemyCombatComponent::IsAttacking() const
{
	return UVBAbilitySystemStatics::IsAttacking(GetOwnerASC());
}

bool UVBEnemyCombatComponent::IsTelegraphing() const
{
	return UVBAbilitySystemStatics::IsTelegraphing(GetOwnerASC());
}

void UVBEnemyCombatComponent::ValidateAttackAbilityContract() const
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// 어빌리티 부여는 서버에서만 일어난다. 클라에서 물으면 항상 '없다'가 나와 거짓 경고가 된다.
		return;
	}

	const FGameplayTag AttackInputTag = GetAttackInputTag();
	if (!AttackInputTag.IsValid())
	{
		VB_LOG(Warning, "%s: EnemyConfig 의 AttackAbilityInputTag 가 비었다 - 이 적은 영원히 공격 후보가 "
		                "되지 않는다(역할이 Surrounder 로 고정되어 BT 공격 분기에 도달조차 못 하므로 "
		                "다른 로그는 한 줄도 남지 않는다)", *Owner->GetName());
		return;
	}

	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC)
	{
		VB_LOG(Warning, "%s: ASC 가 없어 공격 어빌리티 계약을 검증할 수 없다", *Owner->GetName());
		return;
	}

	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, AttackInputTag);
	if (!Spec)
	{
		VB_LOG(Warning, "%s: AttackAbilityInputTag '%s' 에 맞는 어빌리티가 부여되지 않았다 - 이 적은 "
		                "영원히 공격하지 않는다. DefaultAbilities 에 든 GA 의 InputTag 와 맞춰라",
		       *Owner->GetName(), *AttackInputTag.ToString());
		return;
	}

	// 쿨다운이 공격보다 짧으면 간격을 만들지 못한다.
	// 왜 값을 여기서 고르지 않는가: 적정 쿨다운은 공격 몽타주 길이에 종속이고 얼마나 쉬어야 하는지는
	//   FEEL 이다. 숫자를 코드가 정하면 콘텐츠가 바뀔 때마다 틀린다. 대신 틀린 조합이 부팅 시
	//   이름을 갖게 만든다 - CommitAbility 는 쿨다운을 공격 '시작'에 걸므로, 몽타주가 그보다 길면
	//   쿨다운이 스윙 도중에 풀려 같은 적이 곧바로 다시 때릴 수 있다.
	// 그룹 간격 자체는 코디네이터의 예고 게이트가 잡는다 - 이 검사는 개별 적의 연타를 막는 쪽이다.
	const UGameplayAbility* AbilityCDO = Spec->Ability.Get();
	const UGameplayEffect*  CooldownGE = AbilityCDO ? AbilityCDO->GetCooldownGameplayEffect() : nullptr;
	if (!CooldownGE)
	{
		VB_LOG(Log, "%s: 공격 어빌리티에 쿨다운 GE 가 없다 - 연타를 막는 것은 어택 토큰 회전뿐이다",
		       *Owner->GetName());
	}
}

const UVBEnemyConfig* UVBEnemyCombatComponent::GetCombatConfig() const
{
	if (const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner()))
	{
		return Enemy->GetEnemyConfig(); // 미할당이면 CDO - 절대 null 아님
	}
	// 적이 아닌 액터에 이 컴포넌트가 붙은 경우. CDO 디폴트로 답해 호출부의 null 분기를 없앤다.
	return GetDefault<UVBEnemyConfig>();
}

void UVBEnemyCombatComponent::OnAttackingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount > 0)
	{
		// 상승 에지에서만 통지한다. 카운트가 1 -> 2 로 오르는 경로(재트리거)에서 시계를 다시 찍으면
		// 공격 구간 상한이 갱신돼 안전망이 늘어난다.
		if (!bWasAttacking)
		{
			bWasAttacking = true;
			// 임대 시계의 시작점이 여기다. 부여 시각으로 재면 MaxTokenHoldSeconds 가 공격이 아니라
			// 접근 시간을 잰다 - 2026-08-20 실측에서 만료가 공격 0.2~0.5초 지점에 터져
			// 스윙 중인 멤버의 토큰 슬롯을 비웠고 동시 공격 상한이 붕괴했다.
			if (UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
			{
				Coordinator->NotifyAttackStarted(this);
			}
		}
		return;
	}

	if (!bWasAttacking)
	{
		// 상승 없이 내려온 첫 콜백은 무시한다. 등록 시점에 카운트가 0이면 콜백이 한 번 들어올 수 있고,
		// 그것을 반납으로 읽으면 쥐지도 않은 토큰에 대해 ReleaseAttackToken 이 돌아
		// 상태 초기화와 그룹 재배분이 근거 없이 일어난다(LeaveCombat 의 래치 정리와 같은 이유).
		return;
	}

	bWasAttacking = false;
	if (UVBCombatCoordinatorSubsystem* Coordinator = UVBCombatCoordinatorSubsystem::Get(this))
	{
		Coordinator->ReleaseAttackToken(this);
	}
}
