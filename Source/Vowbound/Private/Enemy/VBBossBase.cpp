// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Enemy/VBBossBase.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayEffectTypes.h" // FOnAttributeChangeData
#include "Net/UnrealNetwork.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Data/VBBossConfig.h"
#include "Vowbound/Vowbound.h"

AVBBossBase::AVBBossBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AVBBossBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 조건 없음 + 값이 바뀔 때만 OnRep. DOREPLIFETIME 과 같은 동작이지만 «언제 알리는가» 를 읽는 사람이 찾지 않게 적는다.
	DOREPLIFETIME_CONDITION_NOTIFY(AVBBossBase, CurrentPhaseIndex, COND_None, REPNOTIFY_OnChanged);
	// 인덱스와 같은 번치로 간다 - OnRep_CurrentPhaseIndex 가 읽는 값이라 인덱스보다 늦게 도착하면 안 된다(헤더 주석).
	DOREPLIFETIME(AVBBossBase, bPhaseTransitionSilent);
}

const UVBBossConfig* AVBBossBase::GetBossConfig() const
{
	return Cast<UVBBossConfig>(GetEnemyConfig());
}

void AVBBossBase::BeginPlay()
{
	Super::BeginPlay();

	// 부팅 시 1회 계약 검사. 여기서 못 걸리면 증상은 «50% 를 지나도 아무 일 없다» 뿐이고 로그는 없다.
	const UVBBossConfig* BossConfig = GetBossConfig();
	if (!BossConfig)
	{
		VB_LOG(Warning, "보스 %s 의 EnemyConfig 가 UVBBossConfig 가 아니다(%s). 페이즈 없이 잡몹처럼 행동한다.",
		       *GetName(), *GetNameSafe(GetEnemyConfig()));
		return;
	}

	// 페이즈 태그마다 부여된 GA 가 있는지. UVBEnemyCombatComponent::ValidateAttackAbilityContract 가 첫 페이즈 태그를
	//   같은 방식으로 보지만 페이즈 태그는 모르므로 여기서 나머지를 본다. 없으면 그 페이즈에서 영영 안 때린다.
	if (HasAuthority() && AbilitySystemComponent)
	{
		for (int32 Index = 0; Index < BossConfig->AdditionalPhases.Num(); ++Index)
		{
			const FGameplayTag& Tag = BossConfig->AdditionalPhases[Index].AttackInputTag;
			if (!UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(AbilitySystemComponent, Tag))
			{
				VB_LOG(Warning, "보스 %s: AdditionalPhases[%d] 의 공격 태그 %s 에 맞는 GA 가 DefaultAbilities 에 없다. "
				                "그 페이즈에서 공격하지 않는다.",
				       *GetName(), Index, *Tag.ToString());
			}
		}
	}
}

FGameplayTag AVBBossBase::GetCurrentAttackInputTag() const
{
	if (CurrentPhaseIndex > 0)
	{
		if (const UVBBossConfig* BossConfig = GetBossConfig())
		{
			if (BossConfig->AdditionalPhases.IsValidIndex(CurrentPhaseIndex - 1))
			{
				return BossConfig->AdditionalPhases[CurrentPhaseIndex - 1].AttackInputTag;
			}
		}
	}
	return Super::GetCurrentAttackInputTag();
}

void AVBBossBase::OnHealthChanged(const struct FOnAttributeChangeData& Data)
{
	Super::OnHealthChanged(Data);

	// 사망은 베이스가 방금 처리했다. 죽은 보스가 페이즈에 들어가면 시체 위에서 도발 몽타주가 돈다.
	if (!HasAuthority() || IsDead())
	{
		return;
	}

	const UVBBossConfig* BossConfig = GetBossConfig();
	if (!BossConfig || !HealthAttributeSet)
	{
		return;
	}

	const float MaxHealth = HealthAttributeSet->GetMaxHealth();
	if (MaxHealth <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float HealthRatio = Data.NewValue / MaxHealth;

	// 지금 페이즈 뒤의 문턱을 차례로 본다. 한 대에 둘을 지나면 마지막 것으로 바로 간다 - 중간 페이즈의 연출은 건너뛴다.
	int32 TargetPhaseIndex = CurrentPhaseIndex;
	for (int32 Index = CurrentPhaseIndex; Index < BossConfig->AdditionalPhases.Num(); ++Index)
	{
		if (HealthRatio > BossConfig->AdditionalPhases[Index].HealthRatioThreshold)
		{
			break;
		}
		TargetPhaseIndex = Index + 1;
	}

	if (TargetPhaseIndex != CurrentPhaseIndex)
	{
		EnterPhase(TargetPhaseIndex);
	}
}

void AVBBossBase::EnterPhase(int32 NewPhaseIndex)
{
	const UVBBossConfig* BossConfig = GetBossConfig();
	if (!BossConfig || !BossConfig->AdditionalPhases.IsValidIndex(NewPhaseIndex - 1))
	{
		return;
	}

	// 엎드린 채(그로기) 문턱을 지나면 연출 없이 페이즈만 바뀐다(design .sw/boss/design.md §14.4). 판단은 여기 한 번이고
	//   서버의 BeginPhaseTransition 과 원격 클라의 OnRep 이 같은 값을 읽는다(FIND-131 - 종전엔 서버만 알았다).
	bPhaseTransitionSilent = UVBAbilitySystemStatics::IsDowned(AbilitySystemComponent);

	const int32 PreviousPhaseIndex = CurrentPhaseIndex;
	CurrentPhaseIndex = NewPhaseIndex;
	const FVBBossPhase& Phase = BossConfig->AdditionalPhases[NewPhaseIndex - 1];
	VB_LOG(Log, "보스 %s 페이즈 %d -> %d (공격 태그 %s, 전환 몽타주 %s)",
	       *GetName(), PreviousPhaseIndex, CurrentPhaseIndex, *Phase.AttackInputTag.ToString(),
	       *GetNameSafe(Phase.TransitionMontage));

	// 스윙 중이든 예고 중이든 지금 공격을 끊는다. 예고만 끊으면 이미 뽑은 칼이 연출 위로 내려온다(코디네이터의 처형 게이트와 같은 판단).
	//   부모 태그로 취소한다 - 공격 GA 들의 AssetTag 는 Ability.Attack.* 자식이라 매칭된다.
	if (AbilitySystemComponent)
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(VBGameplayTags::Ability_Attack);
		AbilitySystemComponent->CancelAbilities(&AttackTags);
	}

	BeginPhaseTransition(Phase);
}

void AVBBossBase::BeginPhaseTransition(const FVBBossPhase& Phase)
{
	// 이미 도는 전환 위에 또 들어오면(두 문턱을 연달아 지남) 앞 것을 걷고 새로 건다. 래치가 «걸었을 때만 뗀다» 를 지킨다.
	//   앞 몽타주의 종료 델리게이트는 블렌드아웃 뒤에 늦게 오므로 세대 번호로 무시한다(OnTransitionMontageEnded).
	EndPhaseTransition();
	++PhaseTransitionSerial;

	if (!Phase.TransitionMontage)
	{
		return;
	}

	// 엎드린 채(그로기) 문턱을 지나면 연출 없이 페이즈만 바뀐다(design .sw/boss/design.md §14.4). 서 있는 도발 클립이 엎드린 몸을 덮으면
	//   몸이 튀고, 다운 대기 몽타주가 끊겨 UVBGA_Knockdown 이 자세를 잃는다. 취약 배수 때문에 다운 중에 문턱을 지나는 일은 흔하다.
	//   판단은 EnterPhase 가 bPhaseTransitionSilent 에 한 번 적었다 - 여기서 태그를 다시 읽으면 홈이 둘이 된다.
	if (bPhaseTransitionSilent)
	{
		VB_LOG(Log, "%s 페이즈 전환 - 엎드린 채라 연출 생략(공격 풀만 바뀐다)", *GetName());
		return;
	}

	if (AbilitySystemComponent)
	{
		// 이 태그가 BT_Infected 의 Engage / Move To Ring Slot 분기를 닫는다(Staggered·HitReacting·FinisherWindow 와 같은 줄).
		//   또 UVBGA_EnemyMeleeAttack 의 ActivationBlockedTags 라 코디네이터가 토큰을 줘도 GA 가 안 켜진다.
		AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Combat_PhaseTransition, 1,
		                                            EGameplayTagReplicationState::CountToOwner);
		bPhaseTransitionActive = true;

		if (Phase.bSuperArmorDuringTransition)
		{
			AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Combat_SuperArmor, 1,
			                                            EGameplayTagReplicationState::CountToOwner);
			bGrantedTransitionSuperArmor = true;
		}
	}

	// 데코레이터는 다음 틱에 이동을 끊는다. 그 사이의 속도를 여기서 지워 도발이 미끄러지며 시작하지 않게 한다.
	//   BT 가 다시 명령하지 않는 것은 위 태그가 보장하므로 두 시스템이 싸우지 않는다(UVBGA_FinisherWindow 의 bStopMovementOnOpen 과 같은 짝).
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
	}

	PhaseTransitionStartLocation = GetActorLocation();
	PhaseTransitionStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	const float PlayedLength = PlayTransitionMontage(Phase);
	if (PlayedLength <= 0.0f)
	{
		// 재생이 안 됐는데 태그를 걸어 두면 보스가 영영 멈춘다. 즉시 걷는다.
		VB_LOG(Warning, "보스 %s: 전환 몽타주 %s 재생 실패. 전환 없이 페이즈만 바꾼다.",
		       *GetName(), *GetNameSafe(Phase.TransitionMontage));
		EndPhaseTransition();
		return;
	}

	if (UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		FOnMontageEnded EndDelegate;
		EndDelegate.BindUObject(this, &AVBBossBase::OnTransitionMontageEnded, PhaseTransitionSerial);
		AnimInstance->Montage_SetEndDelegate(EndDelegate, Phase.TransitionMontage);
	}
}

void AVBBossBase::EndPhaseTransition()
{
	if (!bPhaseTransitionActive)
	{
		return;
	}
	bPhaseTransitionActive = false;

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_PhaseTransition);
		if (bGrantedTransitionSuperArmor)
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_SuperArmor);
		}
	}
	bGrantedTransitionSuperArmor = false;
}

void AVBBossBase::OnTransitionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Serial)
{
	if (Serial != PhaseTransitionSerial)
	{
		// 앞 세대의 몽타주가 늦게 끝난 것. 지금 도는 전환의 것이 아니다.
		return;
	}
	// 끊겼어도(사망·다른 몽타주) 전환은 끝이다. 태그가 남으면 보스가 영영 멈춘다.
	// 이동 거리는 «BT 가 전환 중에 명령을 냈는가» 의 증거다 - 도발 클립의 루트모션은 원점으로 돌아오므로(실측 37cm 이탈 뒤 복귀)
	//   순이동이 수십 cm 를 넘으면 게이트가 아니라 걸음이다.
	const double Elapsed = GetWorld() ? GetWorld()->GetTimeSeconds() - PhaseTransitionStartTime : 0.0;
	const double Moved = FVector::Dist2D(GetActorLocation(), PhaseTransitionStartLocation);
	VB_LOG(Log, "보스 %s 전환 종료: %.2f초, 끊김=%d, 순이동 %.0fcm",
	       *GetName(), Elapsed, bInterrupted ? 1 : 0, Moved);
	EndPhaseTransition();
}

void AVBBossBase::HandleDeath()
{
	EndPhaseTransition();
	Super::HandleDeath();
}

float AVBBossBase::PlayTransitionMontage(const FVBBossPhase& Phase)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Phase.TransitionMontage)
	{
		return 0.0f;
	}
	return AnimInstance->Montage_Play(Phase.TransitionMontage, 1.0f);
}

void AVBBossBase::OnRep_CurrentPhaseIndex()
{
	// 원격 클라의 연출만. 게임플레이 판단(태그·공격 취소)은 서버가 EnterPhase 에서 했다.
	// 초기 복제(늦게 들어온 클라)는 BeginPlay 전에 도착한다 - 그때 몽타주를 틀면 이미 지난 전환을 재생한다.
	if (!HasActorBegunPlay())
	{
		return;
	}
	// 서버가 «연출 없음» 으로 판단한 전환(엎드린 채)은 클라도 틀지 않는다 - 같은 번치로 온 복제 값이라 여기서 읽어도 안전하다(헤더 주석, FIND-131).
	if (bPhaseTransitionSilent)
	{
		return;
	}
	const UVBBossConfig* BossConfig = GetBossConfig();
	if (BossConfig && BossConfig->AdditionalPhases.IsValidIndex(CurrentPhaseIndex - 1))
	{
		PlayTransitionMontage(BossConfig->AdditionalPhases[CurrentPhaseIndex - 1]);
	}
}
