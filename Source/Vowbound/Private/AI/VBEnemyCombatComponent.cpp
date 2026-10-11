// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AI/VBEnemyCombatComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayEffect.h" // UGameplayAbility::GetCooldownGameplayEffect 의 반환 타입
#include "GameFramework/Actor.h"
#include "Engine/World.h" // GetTimeSeconds - 슈퍼아머 반격 피격 창
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h" // PlanNextAttack - PeekAttackConfig / MeasureMontageLungeCm
#include "Data/VBAttackConfig.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "AI/VBAIController.h" // PushStartDistanceToBlackboard
#include "GameFramework/Pawn.h"
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
		// 슈퍼아머 반격의 근거(연속 피격)를 센다. 아키타입에 반격이 없어도 구독은 싸다 - 판정은 WantsArmoredCounter 가 데이터로 한다.
		HitReactingTagHandle = ASC->RegisterGameplayTagEvent(
			                          VBGameplayTags::State_Combat_HitReacting,
			                          EGameplayTagEventType::NewOrRemoved)
		                          .AddUObject(this, &UVBEnemyCombatComponent::OnHitReactingTagChanged);
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
	if (HitReactingTagHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = GetOwnerASC())
		{
			ASC->UnregisterGameplayTagEvent(HitReactingTagHandle,
			                                VBGameplayTags::State_Combat_HitReacting,
			                                EGameplayTagEventType::NewOrRemoved);
		}
		HitReactingTagHandle.Reset();
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

void UVBEnemyCombatComponent::UpdateGait()
{
	AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner());
	if (!Enemy)
	{
		return;
	}

	// 대상이 없으면 걷는다. 순찰·복귀는 아직 없지만, 그 자리가 생겼을 때 기본이 «뛴다» 이면
	//   전투가 끝난 적이 계속 달리는 그림이 된다.
	const AActor* Target = CombatTarget.Get();
	if (!Target)
	{
		Enemy->ApplyGait(EVBGait::Walk);
		return;
	}

	const UVBEnemyConfig* Config = GetCombatConfig(); // 절대 null 아님
	const FVector         Here   = Enemy->GetActorLocation();

	// Z 를 무시한다 - 계단/경사의 수직차가 거리를 튀게 만들면 걸음이 그 자리에서 왕복한다
	//   (역할 판정이 Dist2D 를 쓰는 것과 같은 이유).
	double Remaining;
	if (GetRole() == EVBCombatRole::Surrounder)
	{
		// 링 슬롯까지. 슬롯이 아직 없으면 GetSlotLocation 이 제 위치를 돌려주므로 0 이 되어 걷는다.
		Remaining = FVector::Dist2D(Here, GetSlotLocation());
	}
	else
	{
		// 사거리까지 «더 가야 하는» 거리다. 이미 사거리 안이면 음수가 되어 걷는다 -
		//   전력으로 붙었다가 급정거하는 대신 마지막 한 걸음에서 감속한다.
		// 계획된 공격이 있으면 그 시작 거리까지 - 거인은 3.5m 돌진을 위해 5m 밖에서 멈춰야 한다(design .sw/boss/design.md §12.7).
		Remaining = FVector::Dist2D(Here, Target->GetActorLocation()) - GetAttackStartDistance();
	}

	// 히스테리시스 - 지금 걷고 있으면 진입 거리를, 뛰고 있으면 (더 짧은) 이탈 거리를 본다.
	//   문턱이 하나면 경계에서 매 주기 걸음이 뒤집히고 다리가 떤다. 역할 판정이 Commit/Release 쌍을
	//   쓰는 것과 같은 이유이고, 그 규율 밖에 있던 것을 2026-09-07 /verify 가 잡았다.
	const bool  bRunningNow = (Enemy->GetGait() != EVBGait::Walk);
	const float Threshold   = bRunningNow ? Config->RunExitDistance : Config->RunEnterDistance;

	Enemy->ApplyGait(Remaining > Threshold ? EVBGait::Run : EVBGait::Walk);
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

	return CanActivateAbilityByInputTag(AttackInputTag);
}

FGameplayTag UVBEnemyCombatComponent::GetAttackInputTag() const
{
	// 홈은 아키타입 자산 하나다. 하나뿐인 트리가 여러 아키타입을 태우므로 태그가 트리에 있으면
	// 아키타입을 더할 때 트리를 통째로 복제해야 한다(VBEnemyConfig.h 의 판단).
	// 슈퍼아머 반격(design .sw/enemy-soldier/design.md §3.3): 조건이 맞으면 반격 GA 의 태그. BT 태스크·IsReadyToAttack·코디네이터가
	//   전부 이 함수를 쓰므로 «어느 공격인가» 의 홈이 여기 하나로 유지된다.
	const UVBEnemyConfig* Config = GetCombatConfig(); // 절대 null 아님
	if (WantsArmoredCounter())
	{
		return Config->ArmoredCounterAttackInputTag;
	}
	// 보스 페이즈(design .sw/boss/design.md §11.2): 액터에게 «지금 태그» 를 묻는다. 잡몹의 기본 구현은 Config->AttackAbilityInputTag
	//   그대로라 종전과 같고, 적이 아닌 액터에 붙은 경우만 CDO 값으로 떨어진다.
	if (const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(GetOwner()))
	{
		return Enemy->GetCurrentAttackInputTag();
	}
	return Config->AttackAbilityInputTag;
}

bool UVBEnemyCombatComponent::CanActivateAbilityByInputTag(const FGameplayTag& InputTag) const
{
	UAbilitySystemComponent* ASC = GetOwnerASC();
	if (!ASC || !InputTag.IsValid())
	{
		return false;
	}
	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, InputTag);
	if (!Spec || !Spec->Ability)
	{
		return false;
	}
	// 인스턴스가 있으면 그것을, 없으면 CDO 를 묻는다(엔진 InternalTryActivateAbility 와 같은 선택).
	// 삼항에 바로 넣지 않는 이유: GetPrimaryInstance() 는 생 포인터인데 Spec->Ability 는 TObjectPtr 라
	//   공통 타입이 정해지지 않는다(C2445). 지역변수로 받아 타입을 하나로 맞춘다.
	const UGameplayAbility* PrimaryInstance = Spec->GetPrimaryInstance();
	const UGameplayAbility* AbilityToQuery  = PrimaryInstance ? PrimaryInstance : Spec->Ability.Get();
	return AbilityToQuery->CanActivateAbility(Spec->Handle, ASC->AbilityActorInfo.Get());
}

bool UVBEnemyCombatComponent::WantsArmoredCounter() const
{
	const UVBEnemyConfig* Config = GetCombatConfig();
	if (!Config->ArmoredCounterAttackInputTag.IsValid() || Config->ArmoredCounterAfterHits <= 0)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	if (!World || CountRecentHits(World->GetTimeSeconds()) < Config->ArmoredCounterAfterHits)
	{
		return false;
	}
	// 반격 GA 가 쿨다운이면 보통 공격으로 떨어진다 - 여기서 참을 돌려주면 IsReadyToAttack 이 거짓이 되어 아예 안 때린다.
	return CanActivateAbilityByInputTag(Config->ArmoredCounterAttackInputTag);
}

int32 UVBEnemyCombatComponent::CountRecentHits(double Now) const
{
	const double Window = GetCombatConfig()->ArmoredCounterWindowSeconds;
	int32 Count = 0;
	for (const double HitTime : RecentHitTimes)
	{
		if (Now - HitTime <= Window)
		{
			++Count;
		}
	}
	return Count;
}

void UVBEnemyCombatComponent::OnHitReactingTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	if (NewCount <= 0)
	{
		bWasHitReacting = false;
		return;
	}
	if (bWasHitReacting)
	{
		return; // 1 -> 2 는 같은 피격 구간이다
	}
	bWasHitReacting = true;

	const UWorld* World = GetWorld();
	const UVBEnemyConfig* Config = GetCombatConfig();
	if (!World || !Config->ArmoredCounterAttackInputTag.IsValid())
	{
		return; // 반격이 없는 아키타입은 세지 않는다
	}
	const double Now = World->GetTimeSeconds();
	// 창 밖은 버리고 넣는다. 배열은 임계(한 자리 수) 안팎이라 선형이 싸다.
	RecentHitTimes.RemoveAll([Now, Config](double HitTime) { return Now - HitTime > Config->ArmoredCounterWindowSeconds; });
	RecentHitTimes.Add(Now);
	const int32 Hits = RecentHitTimes.Num();
	if (Hits >= Config->ArmoredCounterAfterHits)
	{
		VB_LOG(Log, "[Counter] %s 창 %.1f초 안 %d대 - 슈퍼아머 반격 요청 (%s)", *GetNameSafe(GetOwner()),
		       Config->ArmoredCounterWindowSeconds, Hits,
		       CanActivateAbilityByInputTag(Config->ArmoredCounterAttackInputTag) ? TEXT("켤 수 있음") : TEXT("쿨다운 - 보통 공격으로"));
	}
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

	// 슈퍼아머 반격 태그도 같은 계약이다 - 저작됐는데 GA 가 없으면 WantsArmoredCounter 가 조용히 거짓으로 떨어져 반격이 영영 안 나온다.
	const FGameplayTag ArmoredTag = GetCombatConfig()->ArmoredCounterAttackInputTag;
	if (ArmoredTag.IsValid() && !UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, ArmoredTag))
	{
		VB_LOG(Warning, "%s: ArmoredCounterAttackInputTag '%s' 에 맞는 어빌리티가 부여되지 않았다 - 슈퍼아머 반격이 영영 나오지 않는다. "
		                "DefaultAbilities 에 그 InputTag 의 GA 를 넣어라", *Owner->GetName(), *ArmoredTag.ToString());
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
			// 공격이 시작됐으면(반격이든 보통이든) «이번 압박» 은 끝났다 - 슈퍼아머 반격의 근거를 비운다.
			RecentHitTimes.Reset();
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

void UVBEnemyCombatComponent::PlanNextAttack(const FVector& TargetLocation)
{
	ClearPlannedAttack();

	const AActor* Owner = GetOwner();
	UAbilitySystemComponent* ASC = GetOwnerASC();
	const FGameplayTag Tag = GetAttackInputTag();
	if (!Owner || !ASC || !Tag.IsValid())
	{
		return;
	}
	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, Tag);
	if (!Spec)
	{
		return;
	}
	// 인스턴스가 있으면 그것을(무브셋 해석이 인스턴스 상태를 볼 수 있다), 없으면 CDO.
	const UGameplayAbility*     PrimaryInstance = Spec->GetPrimaryInstance();
	const UVBGA_MeleeAttackBase* AttackGA       = Cast<UVBGA_MeleeAttackBase>(PrimaryInstance ? PrimaryInstance : Spec->Ability.Get());
	const UVBAttackConfig*       Config         = AttackGA ? AttackGA->PeekAttackConfig() : nullptr;
	if (!Config)
	{
		return;
	}
	const int32 VariantCount = Config->GetMontageVariantCount();
	if (VariantCount <= 0)
	{
		return;
	}

	// 시작 거리 = 클립 전진(메시 배율 반영) + 그 변형의 멈추는 거리(UVBAttackConfig::GetVariantApproachOffset - 워프·스윙 선택과 같은 홈).
	const AVBEnemyBase* Enemy    = Cast<AVBEnemyBase>(Owner);
	const float         Scale    = Enemy ? Enemy->GetStrideScale() : 1.0f;
	const float         Ratio    = FMath::Max(1.0f, GetCombatConfig()->WarpLungeToleranceRatio);
	const float         Distance = FVector::Dist2D(Owner->GetActorLocation(), TargetLocation);

	TArray<float>   Lunges;
	TArray<float>   Standoffs;
	TArray<FString> Names;
	Lunges.Reserve(VariantCount);
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		const FVBAttackMontageVariant Variant = Config->ResolveMontageVariant(Index);
		Lunges.Add(UVBGA_MeleeAttackBase::MeasureMontageLungeCm(Variant.Montage, Scale));
		Standoffs.Add(Config->GetVariantApproachOffset(Index, Scale));
		Names.Add(GetNameSafe(Variant.Montage));
	}

	// 후보: 지금 거리에서 «대역 안으로» 닿을 수 있는 변형 = 거리 >= 멈춤 + 전진/허용비. 이보다 가까우면 그 클립은 눌려서 발이 민다.
	// 순환 커서로 고른다 - 같은 공격이 연달아 나오지 않게. 후보가 없으면(플레이어가 바짝 붙음) 전진이 가장 짧은 것.
	int32 Chosen = INDEX_NONE;
	for (int32 Step = 0; Step < VariantCount && Chosen == INDEX_NONE; ++Step)
	{
		const int32 Index = (VariantPlanCursor + Step) % VariantCount;
		if (Distance >= Standoffs[Index] + Lunges[Index] / Ratio)
		{
			Chosen = Index;
		}
	}
	if (Chosen == INDEX_NONE)
	{
		Chosen = 0;
		for (int32 Index = 1; Index < VariantCount; ++Index)
		{
			if (Lunges[Index] < Lunges[Chosen])
			{
				Chosen = Index;
			}
		}
	}
	VariantPlanCursor = Chosen + 1;

	PlannedVariantIndex  = Chosen;
	PlannedStartDistance = Lunges[Chosen] + Standoffs[Chosen];
	PushStartDistanceToBlackboard();

	FString Candidates;
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		Candidates += FString::Printf(TEXT("%s%s %.0f+%.0f"), Index ? TEXT(" / ") : TEXT(""), *Names[Index], Lunges[Index], Standoffs[Index]);
	}
	VB_LOG(Log, "[공격 계획] %s 거리 %.0f -> %d번 %s (전진 %.0f + 멈춤 %.0f = 시작 %.0f) 후보 [%s]",
	       *GetNameSafe(Owner), Distance, Chosen, *Names[Chosen], Lunges[Chosen], Standoffs[Chosen], PlannedStartDistance, *Candidates);
}

void UVBEnemyCombatComponent::ClearPlannedAttack()
{
	PlannedVariantIndex  = INDEX_NONE;
	PlannedStartDistance = -1.0f;
	PushStartDistanceToBlackboard();
}

void UVBEnemyCombatComponent::PushStartDistanceToBlackboard() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (AVBAIController* AI = Pawn ? Cast<AVBAIController>(Pawn->GetController()) : nullptr)
	{
		AI->PushAttackStartDistance(GetAttackStartDistance());
	}
}

int32 UVBEnemyCombatComponent::ConsumePlannedVariantIndex()
{
	// 시작 거리는 남긴다 - 스윙 중에도 역할 판정·걸음이 같은 문턱을 봐야 한다. 토큰 반납이 전부 비운다.
	const int32 Index = PlannedVariantIndex;
	PlannedVariantIndex = INDEX_NONE;
	return Index;
}

float UVBEnemyCombatComponent::GetAttackStartDistance() const
{
	// 계획은 토큰과 함께 산다. 코디네이터가 토큰을 거두는 길은 반납 하나가 아니라 여섯이고
	//   (임대 만료 청소·접근 정체 재배분·물러난 자 강탈·슈퍼아머 반격 선점·전투 이탈·반납),
	//   그중 반납만 ClearPlannedAttack 을 부른다. 나머지는 코디네이터 제 구조체(ClearLease)만 비우므로
	//   저장된 거리를 그대로 믿으면 토큰 없는 적이 남의 계획대로 멈춰 서고 역할·걸음도 그 값으로 판정된다.
	//   «지금 토큰을 쥐고 있나» 를 그 자리에서 물으면 경로가 몇이든 한 번에 닫힌다(2026-09-17 검증 지적).
	return (PlannedStartDistance > 0.0f && HasAttackToken()) ? PlannedStartDistance : GetCombatConfig()->AttackCommitDistance;
}

float UVBEnemyCombatComponent::GetAttackReleaseDistance() const
{
	// 히스테리시스 폭은 자산의 Release-Commit 그대로 - 계획이 없으면 종전 AttackReleaseDistance 와 같다.
	const UVBEnemyConfig* Config = GetCombatConfig();
	return GetAttackStartDistance() + FMath::Max(0.0f, Config->AttackReleaseDistance - Config->AttackCommitDistance);
}
