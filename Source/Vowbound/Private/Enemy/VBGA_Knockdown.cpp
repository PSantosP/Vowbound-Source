// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBGA_Knockdown.h"

#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Animation/AnimInstance.h" // Montage_GetPosition - 넘어짐 클립이 어디까지 왔나
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitImpact.h" // VBGetPushYaw - 넘어지는 방향
#include "AI/VBEnemyCombatComponent.h"
#include "Data/VBHitReactConfig.h"
#include "Enemy/VBEnemyBase.h" // MulticastPlayAdditiveFlinch - 플린치를 모든 머신에 실어 보낸다
#include "Vowbound/Vowbound.h"

namespace
{
	// 반격 감시 주기(초). 표적 거리를 이 간격으로 본다 - 매 틱은 과하고, 반격 클립(3초)에 비해 0.2 는 충분히 촘촘하다.
	constexpr float DownAttackWatchInterval = 0.2f;
}

UVBGA_Knockdown::UVBGA_Knockdown()
{
	// 두 번째 진입로: 넘어뜨리는 타격(DEC-025). 부모가 건 PoiseBroken 과 함께 이 GA 하나가 받는다 -
	//   같은 몸을 두 GA 가 넘어뜨리면 누가 Downed 를 붙이고 떼는지가 갈라진다. 콘텐츠 유무는 ShouldAbilityRespondToEvent 가 답한다.
	FAbilityTriggerData KnockedDownTrigger;
	KnockedDownTrigger.TriggerTag = VBGameplayTags::Event_Combat_KnockedDown;
	AbilityTriggers.Add(KnockedDownTrigger);

	// 다운 중 두 번째 파탄은 다운을 다시 시작하지 않는다. Poise 는 파탄 때 만충되므로 그 파탄은 그냥 버려진다.
	//   넘어뜨리는 타격도 같다 - 엎드린 몸에는 허브가 무거움으로 내려 보내고, 그것은 아래 다운 피격(Hit 구독)이 받는다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Downed);

	// 엎드린 채 휘두르는 반격 GA 가 Ability.Attack.* 다 - 부모의 차단을 물려받으면 반격이 영영 안 켜진다.
	//   «경직 중 신규 공격 없음» 은 BT 가 Staggered 로 닫혀 있는 것으로 그대로 성립한다(헤더 주석).
	BlockAbilitiesWithTag.RemoveTag(VBGameplayTags::Ability_Attack);

	// bRetriggerInstancedAbility 는 부모 값(true) 그대로다. 다운 경로는 위 Downed 차단이 먼저 걸려 재진입이 없고,
	//   쿨다운 경로(서 있는 경직)는 부모와 같은 연타 FEEL 이 필요하다.
}

const FVBKnockdownSet* UVBGA_Knockdown::GetKnockdownSet() const
{
	return HitReactConfig ? &HitReactConfig->KnockdownSet : nullptr;
}

bool UVBGA_Knockdown::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
                                                  const FGameplayEventData*        Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}
	// 넘어뜨리는 타격은 넘어질 콘텐츠가 있을 때만 받는다. 여기서 거절하면 허브가 켜진 수 0 을 보고 무거운 피격으로 내린다 -
	//   받고 나서 서 있는 경직으로 떨어지면 «강타에 3.7초 기절» 같은 다른 반응이 되고, 허브는 그것을 모른다.
	// 기절 게이지 소진은 콘텐츠가 없어도 받는다 - 부모의 서 있는 경직이 그 경로다(종전 그대로).
	if (Payload && Payload->EventTag == VBGameplayTags::Event_Combat_KnockedDown)
	{
		const FVBKnockdownSet* Set = GetKnockdownSet();
		return Set && Set->IsAuthored();
	}
	return true;
}

void UVBGA_Knockdown::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                     const FGameplayAbilityActorInfo*     ActorInfo,
                                     const FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData*            TriggerEventData)
{
	const FVBKnockdownSet* Set    = GetKnockdownSet();
	const UWorld*          World  = GetWorld();
	const double           Now    = World ? World->GetTimeSeconds() : 0.0;
	const AActor*          Avatar = GetAvatarActorFromActorInfo();

	// 무엇이 넘어뜨리나. 넘어뜨리는 타격(DEC-025)이면 쿨다운·bFallOnPoiseBreak 를 안 본다 - 그 둘은 기절 게이지 소진의 규칙이다.
	const bool bFromImpact = TriggerEventData && TriggerEventData->EventTag == VBGameplayTags::Event_Combat_KnockedDown;

	// 못 넘어지는 이유 셋. 전부 서 있는 경직으로 떨어진다 - 부모가 방향까지 봐서 종전 그대로 돈다.
	//   넘어뜨리는 타격은 첫째 이유로는 여기 못 온다(ShouldAbilityRespondToEvent 가 먼저 거절). 둘째·셋째는 기절 게이지 소진 전용이다.
	const TCHAR* Reason = nullptr;
	if (!Set || !Set->IsAuthored())
	{
		Reason = TEXT("KnockdownSet 미저작(넘어짐·기상 둘 다 있어야 한다)");
	}
	else if (!bFromImpact && !Set->bFallOnPoiseBreak)
	{
		Reason = TEXT("기절 게이지 소진은 넘어뜨리지 않는 아키타입(bFallOnPoiseBreak=false)");
	}
	else if (!bFromImpact && Now < CooldownUntil)
	{
		Reason = TEXT("다운 쿨다운 중");
	}
	if (Reason)
	{
		VB_LOG(Log, "[다운] %s 넘어지지 않는다 - %s -> 서 있는 경직", *GetNameSafe(Avatar), Reason);
		Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
		return;
	}

	// 부모(UVBGA_Stagger)의 본문은 서 있는 경직이라 건너뛰고, 그 위의 공통 베이스만 부른다.
	UVBGameplayAbility::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 여기부터의 부작용(루즈태그·GE·타이머·반격 시동)은 서버 전용이다. IsNetAuthority 가드를 따로 두지 않는 전제: 이 GA 는 AI 적에게만
	//   부여되고(소유 클라가 없다) ServerInitiated 라 ExecuteAbility 가 서버에서만 돈다 - UVBGA_EnemyMeleeAttack 과 같은 전제.
	//   플레이어에게 붙이는 날이 오면 이 줄의 전제가 깨지니 그때 가드를 넣는다(베이스 VBGameplayAbility.cpp 의 계약).
	// 복제 상태는 프로젝트의 다른 루즈태그(슈퍼아머·사망·페이즈 전환)와 같은 CountToOwner 다.
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1, EGameplayTagReplicationState::CountToOwner);
	bDownedTagGranted = true;

	// 넘어뜨리는 타격이면 «공격 정면» 에 마주 서게 돌려 세운 뒤 넘어뜨린다(DEC-025 design §7 1-A-8).
	//   넘어짐 클립이 한 방향(앞에서 맞아 뒤로 넘어짐)뿐이라, 돌리지 않으면 뒤나 옆에서 맞았을 때 공격자 쪽이나 옆으로 쓰러진다.
	//   2026-09-29 개정(user 「넉백은 내가 공격하는 정면 방향으로」): 종전에는 «공격자 위치» 를 향했는데, 이제 허브가 실어 보낸
	//   밀린 방향(공격자의 판정 방향)의 반대를 향한다 - 무거운 피격(UVBGA_HitReact)·넉백과 같은 값이라 셋이 한 방향이 된다.
	//   밀린 방향이 없는 이벤트(옛 경로)만 공격자 위치로 떨어진다.
	//   방향 쌍(앞으로 엎어짐 + 엎드린 기상)이 생기면 그때 방향별로 가른다.
	//   기절 게이지 소진 경로(보스 그로기)는 돌리지 않는다 - 종전 그대로다.
	//   AI 포커스는 Staggered 동안 풀린다(UVBBTService_UpdateCombat) - 한 번 돌린 회전을 컨트롤러가 되돌리지 않는다.
	if (bFromImpact)
	{
		AActor* FallingActor = GetAvatarActorFromActorInfo();
		float   PushYaw      = 0.0f;
		if (FallingActor && VBGetPushYaw(*TriggerEventData, PushYaw))
		{
			FallingActor->SetActorRotation(FRotator(0.0f, PushYaw + 180.0f, 0.0f));
		}
		// 이벤트의 Instigator 는 플레이어면 PlayerState(위치 원점)다 - 몸으로 풀어 읽는다(VBResolveInstigatorBody, 2026-09-29).
		else if (const AActor* Attacker = VBResolveInstigatorBody(TriggerEventData->Instigator.Get()); FallingActor && Attacker)
		{
			FVector ToAttacker = Attacker->GetActorLocation() - FallingActor->GetActorLocation();
			ToAttacker.Z = 0.0f;
			if (!ToAttacker.IsNearlyZero())
			{
				FallingActor->SetActorRotation(FRotator(0.0f, ToAttacker.Rotation().Yaw, 0.0f));
			}
		}
	}

	// 받는 피해 배수의 값은 이 GE 자산이 든다(UVBCombatAttributeSet::DamageTakenScale). 비면 배수 없이 다운만.
	if (Set->VulnerabilityEffect)
	{
		const FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Set->VulnerabilityEffect, GetAbilityLevel());
		VulnerabilityHandle = ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}

	// 다운 중 피격 반응. 이벤트는 피해가 실제로 든 자리(VBHealthAttributeSet)가 쏜다 - 무적·가드로 무른 타격은 여기 안 온다.
	UAbilityTask_WaitGameplayEvent* HitWait = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
		this, VBGameplayTags::Event_Combat_Hit, /*OptionalExternalTarget=*/nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
	HitWait->EventReceived.AddDynamic(this, &ThisClass::OnHitEventReceived);
	HitWait->ReadyForActivation();

	const bool bHasCounter = Set->DownAttackInputTag.IsValid() && Set->DownAttackTriggerRadius > 0.0f;
	if (!Set->DownIdleMontage)
	{
		// 대기 클립이 없으면 넘어짐 클립이 끝난 뒤 ABP 로코모션(서 있는 자세)이 새어 나온다. 다운은 성립하되 그림이 틀리므로 소리낸다.
		VB_LOG(Warning, "[다운] %s KnockdownSet.DownIdleMontage 가 비었다 - 대기 동안 서 있는 자세가 보인다", *GetNameSafe(Avatar));
	}
	else if (Set->DownIdleMontage->BlendModeIn != EMontageBlendMode::Inertialization && Set->DownIdleMontage->BlendIn.GetBlendTime() > KINDA_SMALL_NUMBER)
	{
		// 막을 수는 없고(자산 값) 말할 수는 있다 - 표준 블렌드로 0 에서 들어오면 넘어짐→대기 이음매에서 몸이 뜬다(FVBKnockdownSet::DownIdleMontage 주석).
		//   관성 모드가 켜져 있어도 ABP 슬롯 뒤에 관성 노드가 없으면 블렌드 없이 끊긴다 - 그건 엔진이 «No Inertialization node found» 로 알린다.
		VB_LOG(Warning, "[다운] %s %s 가 표준 블렌드인 %.2f초다 - 관성 모드(또는 0)가 아니면 앞 몽타주가 빠지는 사이 서 있는 자세가 섞여 누운 몸이 뜬다",
		       *GetNameSafe(Avatar), *GetNameSafe(Set->DownIdleMontage), Set->DownIdleMontage->BlendIn.GetBlendTime());
	}
	VB_LOG(Log, "[다운] %s 넘어진다 - 대기 %.1f초, 쿨다운 %.0f초, 반격 %s(반경 %.0f = 자산 %.0f x 몸 배율 %.2f)",
	       *GetNameSafe(Avatar), Set->DownHoldSeconds, Set->CooldownSeconds,
	       bHasCounter ? TEXT("있음") : TEXT("없음"), Set->DownAttackTriggerRadius * AVBEnemyBase::ResolveStrideScale(Avatar),
	       Set->DownAttackTriggerRadius, AVBEnemyBase::ResolveStrideScale(Avatar));

	Phase            = EKnockdownPhase::Falling;
	FallTimeCutShort = 0.0f;
	UAbilityTask_PlayMontageAndWait* Fall = MakeMontageTask(Set->FallMontage);
	Fall->OnBlendOut.AddDynamic(this, &ThisClass::OnFallEnded);
	Fall->OnInterrupted.AddDynamic(this, &ThisClass::OnFallEnded);
	Fall->OnCancelled.AddDynamic(this, &ThisClass::OnFallEnded);
	Fall->ReadyForActivation();
}

void UVBGA_Knockdown::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                                 const FGameplayAbilityActorInfo*     ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 bool                                 bReplicateEndAbility,
                                 bool                                 bWasCancelled)
{
	// Phase 가 None 이면 서 있는 경직 경로였다 - 다운의 정리·쿨다운은 없다.
	if (Phase != EKnockdownPhase::None)
	{
		const FVBKnockdownSet* Set   = GetKnockdownSet();
		UWorld*                World = GetWorld();
		const double           Now   = World ? World->GetTimeSeconds() : 0.0;
		const float            Cooldown = Set ? Set->CooldownSeconds : 0.0f;
		CooldownUntil = Now + Cooldown;

		if (World)
		{
			World->GetTimerManager().ClearTimer(HoldTimer);
			World->GetTimerManager().ClearTimer(WatchTimer);
		}
		if (UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		{
			if (bDownedTagGranted)
			{
				ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed);
			}
			if (VulnerabilityHandle.IsValid())
			{
				ASC->RemoveActiveGameplayEffect(VulnerabilityHandle);
			}
			if (AbilityEndedHandle.IsValid())
			{
				ASC->OnAbilityEnded.Remove(AbilityEndedHandle);
			}
		}
		if (UAnimInstance* AnimInstance = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr)
		{
			AnimInstance->OnMontageBlendingOut.RemoveDynamic(this, &ThisClass::OnAnyMontageBlendingOut);
		}
		VulnerabilityHandle.Invalidate();
		AbilityEndedHandle.Reset();
		DownHitTask.Reset();
		VB_LOG(Log, "[다운] %s 끝(%s) - 다음 파탄은 %.0f초 동안 서 있는 경직",
		       *GetNameSafe(GetAvatarActorFromActorInfo()), bWasCancelled ? TEXT("취소") : TEXT("기상"), Cooldown);

		Phase             = EKnockdownPhase::None;
		bDownedTagGranted = false;
		bHoldExpired      = false;
		bDownHitPlaying   = false;
		bDownAttackActive = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

UAbilityTask_PlayMontageAndWait* UVBGA_Knockdown::MakeMontageTask(UAnimMontage* Montage)
{
	// 부모 경직과 같은 인자: 재생 속도는 HitReactConfig->MontagePlayRate, 어빌리티가 끝나면 몽타주도 멈춘다(bStopWhenAbilityEnds),
	//   블렌드아웃 뒤 중단 알림은 받지 않는다(bAllowInterruptAfterBlendOut=false - 한 태스크에서 BlendOut 과 Interrupted 가 둘 중 하나만 오는 근거).
	return UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, HitReactConfig ? HitReactConfig->MontagePlayRate : 1.0f, NAME_None, true, 1.0f, 0.0f, false);
}

void UVBGA_Knockdown::OnFallEnded()
{
	if (Phase != EKnockdownPhase::Falling)
	{
		return;
	}
	BeginHold();
}

void UVBGA_Knockdown::BeginHold()
{
	const FVBKnockdownSet* Set   = GetKnockdownSet();
	UWorld*                World = GetWorld();
	Phase        = EKnockdownPhase::Hold;
	bHoldExpired = false;
	if (!Set || !World)
	{
		BeginGetUp();
		return;
	}
	// 0 초는 «바로 일어난다» 다. SetTimer 는 0 이하를 «안 건다» 로 읽으므로 아주 짧은 양수로 바꿔 한 번은 울리게 한다.
	// 넘어짐을 다운 피격이 끊었으면 그 남은 클립 시간을 더한다 - 때린다고 전체 다운이 짧아지지 않게(FallHitAcceptSeconds 주석).
	World->GetTimerManager().SetTimer(HoldTimer, this, &ThisClass::OnHoldExpired,
	                                  FMath::Max(Set->DownHoldSeconds + FallTimeCutShort, 0.01f), false);
	if (Set->DownAttackInputTag.IsValid() && Set->DownAttackTriggerRadius > 0.0f)
	{
		World->GetTimerManager().SetTimer(WatchTimer, this, &ThisClass::TickDownAttackWatch, DownAttackWatchInterval, true);
	}
	// 넘어짐을 끊은 다운 피격이 아직 재생 중이면 대기 클립을 틀지 않는다 - 그 피격이 끝날 때 ResumeAfterInterlude 가 튼다.
	if (!bDownHitPlaying)
	{
		PlayDownIdle();
	}
}

void UVBGA_Knockdown::PlayDownIdle()
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	if (!Set || !Set->DownIdleMontage)
	{
		return;
	}
	// 대기가 이미 온전히 돌고 있으면 다시 틀지 않는다 - 반격 블렌드아웃과 반격 종료처럼 이어 달리기가 두 번 올 때 대기를 첫 프레임으로 되감지 않게.
	if (const UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance())
	{
		const FAnimMontageInstance* Running = AnimInstance->GetActiveInstanceForMontage(Set->DownIdleMontage);
		if (Running && !Running->IsStopped())
		{
			return;
		}
	}
	UAbilityTask_PlayMontageAndWait* Idle = MakeMontageTask(Set->DownIdleMontage);
	// Interrupted 는 안 건다 - 다운 피격·반격 몽타주가 대기를 덮는 것이 정상 경로고, 그 몽타주가 끝날 때 대기를 다시 튼다.
	Idle->OnBlendOut.AddDynamic(this, &ThisClass::OnDownIdleEnded);
	Idle->ReadyForActivation();
}

void UVBGA_Knockdown::OnDownIdleEnded()
{
	// 대기 태스크는 BlendOut 만 받으므로 여기 오는 것은 «제 끝에서 빠지는» 경우뿐이다(남이 밀어내면 Interrupted 라 안 온다).
	//   반격 예고 중이면 반격 몽타주가 아직 안 틀렸으니 대기를 다시 걸어 몸을 덮는다 - ResumeAfterInterlude 는 반격 중이면 아무것도 안 튼다.
	if (bDownAttackActive)
	{
		PlayDownIdle();
		return;
	}
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::OnHoldExpired()
{
	bHoldExpired = true;
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::ResumeAfterInterlude()
{
	if (Phase != EKnockdownPhase::Hold || bDownHitPlaying || bDownAttackActive)
	{
		return;
	}
	if (!bHoldExpired)
	{
		PlayDownIdle();
		return;
	}
	// 기상을 «이미 빠지는 중» 인 몽타주(다운 피격·반격·대기의 블렌드아웃) 바로 뒤에 틀면, 기상이 0 에서 들어오는 동안 두 무게의 합이
	//   1 아래로 내려가 몽타주 아래의 서 있는 자세가 섞인다(AnimInstanceProxy.cpp::FAnimInstanceProxy::GetSlotWeight). 누운 몸에서는
	//   몸이 뜨는 것으로 보인다(2026-09-29 user 「그로기 Idle 때 갑자기 몸이 살짝 뜬다」 - 같은 틈이 넘어짐→대기에도 있었다).
	// 대기가 온전히 서 있을 때(무게 1, 빠지는 중 아님)만 기상의 교차 블렌드가 합 1 을 지킨다. 아니면 대기를 먼저 걸고(관성 블렌드라
	//   즉시 무게 1 - FVBKnockdownSet::DownIdleMontage 주석) 다음 틱에 기상한다. 판별은 대기 몽타주 인스턴스가 «멈추는 중» 인가다(FAnimMontageInstance::IsStopped).
	const FVBKnockdownSet*      Set          = GetKnockdownSet();
	const UAnimInstance*        AnimInstance = GetActorInfo().GetAnimInstance();
	const FAnimMontageInstance* IdleInstance = (Set && Set->DownIdleMontage && AnimInstance)
		                                           ? AnimInstance->GetActiveInstanceForMontage(Set->DownIdleMontage) : nullptr;
	if (!Set || !Set->DownIdleMontage || (IdleInstance && !IdleInstance->IsStopped()))
	{
		BeginGetUp();
		return;
	}
	PlayDownIdle();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			// 그 한 틱 사이에 다운 피격·반격이 끼어들었으면 기상은 그것이 끝날 때 다시 묻는다(ResumeAfterInterlude).
			if (IsActive() && Phase == EKnockdownPhase::Hold && !bDownHitPlaying && !bDownAttackActive)
			{
				BeginGetUp();
			}
		}));
	}
}

void UVBGA_Knockdown::OnHitEventReceived(FGameplayEventData Payload)
{
	// 규칙(헤더 주석, design §14.13 - user 「공격할 때 빼고 늘 반응」): 반격 중만 반응 없음. 몸통 반응을 틀 수 있는 자리면 방향별 다운 피격을
	//   «새로» 틀고(앞 반응이 재생 중이어도), 아니면 지금 동작 위에 가산 플린치를 얹는다. 버리는 타격은 모두 이유를 로그로 남긴다 -
	//   종전에는 기상 중·반응 중 타격이 한 줄도 없이 버려져 «왜 반응이 없었나» 를 로그로 답할 수 없었다(2026-09-29 기상 중 여섯 타).
	const FVBKnockdownSet* Set    = GetKnockdownSet();
	const AActor*          Avatar = GetAvatarActorFromActorInfo();
	if (!Set)
	{
		return;
	}
	if (bDownAttackActive)
	{
		VB_LOG(Log, "[다운] %s 반격 중 피격 - 반응 없음", *GetNameSafe(Avatar));
		return;
	}
	const EVBHitDirection Direction = VBComputeHitDirection(Avatar, Payload.Instigator.Get());

	// 몸통 반응을 못 트는 자리. 넘어짐 앞부분·기상은 클립이 스스로 자세를 바꾸는 중이라 갈아 끼우면 튀고(§14.10 실측 50~120cm),
	//   대기 시간이 끝난 뒤에 새로 틀면 계속 때려 영원히 눕힐 수 있다 - 대기 시간이 영구 경직의 뚜껑이다.
	float        FallPosition    = -1.0f;
	const TCHAR* FlinchSituation = nullptr;
	switch (Phase)
	{
	case EKnockdownPhase::Falling:
		{
			const UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance();
			FallPosition = AnimInstance ? AnimInstance->Montage_GetPosition(Set->FallMontage) : -1.0f;
			if (Set->FallHitAcceptSeconds < 0.0f || FallPosition < Set->FallHitAcceptSeconds)
			{
				FlinchSituation = TEXT("넘어지는 중");
			}
			break;
		}
	case EKnockdownPhase::Hold:
		if (bHoldExpired)
		{
			FlinchSituation = TEXT("대기 시간이 끝난 뒤");
		}
		break;
	case EKnockdownPhase::GettingUp:
		FlinchSituation = TEXT("일어나는 중");
		break;
	default:
		return;
	}
	if (FlinchSituation)
	{
		PlayFlinch(Direction, FlinchSituation);
		return;
	}

	UAnimMontage* Montage = Set->DownHitSet.ResolveByDirection(Direction);
	if (!Montage)
	{
		VB_LOG(Log, "[다운] %s 다운 피격 %s - DownHitSet 미저작, 반응 없음", *GetNameSafe(Avatar), *UEnum::GetValueAsString(Direction));
		return;
	}
	if (Phase == EKnockdownPhase::Falling && Set->FallMontage)
	{
		// 넘어짐을 여기서 끊는다. 남은 클립 시간은 대기에 더한다(BeginHold). 넘어짐 태스크는 아래 피격 몽타주가 넘어짐을 덮을 때
		//   OnInterrupted 로 OnFallEnded -> BeginHold 에 온다 - 그때 bDownHitPlaying 이 서 있어 대기 클립을 틀지 않는다.
		const float RateScale = FMath::Max(Set->FallMontage->RateScale * (HitReactConfig ? HitReactConfig->MontagePlayRate : 1.0f), KINDA_SMALL_NUMBER);
		FallTimeCutShort = FMath::Max(0.0f, (Set->FallMontage->GetPlayLength() - FallPosition) / RateScale);
	}
	// 앞 반응이 재생 중이면 갈아 끼운다. 앞 태스크의 종료 알림을 먼저 떼어 낸다 - 새 몽타주가 앞 몽타주를 밀어내며 앞 태스크가
	//   Interrupted 를 부르면 대기가 틀려 방금 튼 반응을 덮는다. 앞 몽타주 자체는 새 몽타주가 교차 블렌드로 밀어낸다(같은 그룹, 둘 다 무게 1 이라 합 1).
	const bool bReplacing = bDownHitPlaying;
	if (UAbilityTask_PlayMontageAndWait* Previous = DownHitTask.Get())
	{
		Previous->OnBlendOut.RemoveAll(this);
		Previous->OnInterrupted.RemoveAll(this);
		Previous->OnCancelled.RemoveAll(this);
		Previous->EndTask();
	}
	VB_LOG(Log, "[다운] %s 다운 피격 %s -> %s%s%s", *GetNameSafe(Avatar), *UEnum::GetValueAsString(Direction), *GetNameSafe(Montage),
	       Phase == EKnockdownPhase::Falling ? *FString::Printf(TEXT(" (넘어짐 %.2f초에서 끊음, 대기 +%.2f초)"), FallPosition, FallTimeCutShort) : TEXT(""),
	       bReplacing ? TEXT(" (앞 반응을 갈아 끼움)") : TEXT(""));
	bDownHitPlaying = true;
	UAbilityTask_PlayMontageAndWait* Hit = MakeMontageTask(Montage);
	Hit->OnBlendOut.AddDynamic(this, &ThisClass::OnDownHitEnded);
	Hit->OnInterrupted.AddDynamic(this, &ThisClass::OnDownHitInterrupted);
	Hit->OnCancelled.AddDynamic(this, &ThisClass::OnDownHitInterrupted);
	DownHitTask = Hit;
	Hit->ReadyForActivation();
}

void UVBGA_Knockdown::OnDownHitEnded()
{
	// 제 끝에서 빠지기 시작했다 - 이 뒤를 덮는 것은 우리 몫이다. 반격 «예고» 중이면(반격 몽타주가 아직 안 틀림) 대기로 덮는다.
	//   ResumeAfterInterlude 는 반격 중이면 아무것도 안 트므로 여기서 따로 건다. 안 덮으면 예고 동안 서 있는 자세가 샌다.
	bDownHitPlaying = false;
	if (bDownAttackActive)
	{
		PlayDownIdle();
		return;
	}
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::OnDownHitInterrupted()
{
	// 다른 몽타주(반격 휘두르기 등)가 밀어냈다 - 몸은 그 몽타주가 덮고 있다. 반격이면 그것이 끝날 때 대기로 돌아온다.
	bDownHitPlaying = false;
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::PlayFlinch(EVBHitDirection Direction, const TCHAR* Situation)
{
	AVBEnemyBase* Enemy   = Cast<AVBEnemyBase>(GetAvatarActorFromActorInfo());
	UAnimMontage* Montage = HitReactConfig ? HitReactConfig->FlinchSet.ResolveByDirection(Direction) : nullptr;
	if (!Montage)
	{
		VB_LOG(Log, "[다운] %s %s 피격 %s - 반응 없음 (FlinchSet 미저작)", *GetNameSafe(Enemy), Situation, *UEnum::GetValueAsString(Direction));
		return;
	}
	if (!Enemy)
	{
		// 이 GA 는 적에게만 붙는다(ExecuteAbility 의 서버 전용 전제와 같은 줄). 적이 아니면 실어 보낼 멀티캐스트가 없다.
		VB_LOG(Warning, "[다운] %s 는 적이 아니다 - 플린치를 실어 보낼 곳이 없다", *GetNameSafe(GetAvatarActorFromActorInfo()));
		return;
	}
	// 가산이 아니면 몸통을 덮어 넘어짐·기상을 가린다. 막을 수 있으니 막는다(자산 실수가 «보스가 순간 서 있다» 로 보이게 두지 않는다).
	if (!Montage->IsValidAdditive())
	{
		VB_LOG(Warning, "[다운] %s FlinchSet 의 %s 가 가산 몽타주가 아니다 - 틀면 %s 동작을 덮는다. 재생 안 함",
		       *GetNameSafe(Enemy), *GetNameSafe(Montage), Situation);
		return;
	}
	// ASC 를 거치지 않는다: ASC 로 틀면 능력의 «현재 몽타주» 가 플린치로 바뀌어 기상 태스크의 블렌드아웃 판정이 어긋난다.
	//   그래서 ASC 몽타주 복제 대신 적의 멀티캐스트로 실어 보낸다(리슨 호스트 자신도 받는다). 앞 플린치 정지·그룹 정지 끔은 거기서 한다.
	Enemy->MulticastPlayAdditiveFlinch(Montage);
	VB_LOG(Log, "[다운] %s %s 피격 %s -> 가산 플린치 %s", *GetNameSafe(Enemy), Situation, *UEnum::GetValueAsString(Direction), *GetNameSafe(Montage));
}

void UVBGA_Knockdown::TickDownAttackWatch()
{
	// 다운 피격 재생 중에도 본다(2026-09-29). 피격마다 반응을 새로 트니 연타 중에는 반응 사이 빈틈이 없고, 빈틈을 기다리면 반격이 영영 안 나온다.
	//   반격 몽타주가 피격 몽타주를 밀어낸다(OnDownHitInterrupted). 대기 시간이 끝난 뒤에는 안 켠다 - 켜면 그만큼 눕는 시간이 늘어난다.
	if (Phase != EKnockdownPhase::Hold || bHoldExpired || bDownAttackActive)
	{
		return;
	}
	const FVBKnockdownSet* Set   = GetKnockdownSet();
	const UWorld*          World = GetWorld();
	if (!Set || !World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastDownAttackTime < Set->DownAttackIntervalSeconds)
	{
		return;
	}
	const AActor*                 Avatar = GetAvatarActorFromActorInfo();
	const UVBEnemyCombatComponent* Combat = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr;
	const AActor*                 Target = Combat ? Combat->GetCombatTarget() : nullptr;
	if (!Target)
	{
		return;
	}
	// 반격도 공격이다 - 어택 토큰 없이 켜면 코디네이터가 «토큰 없이 공격이 시작됐다» 경보를 내고 남의 다음 공격을 미룬다
	//   (VBCombatCoordinatorSubsystem.cpp::NotifyAttackStarted / ReleaseAttackToken). 보스 단독 아레나에서는 늘 쥐고 있어 조용하지만
	//   잡몹이 섞이면 거짓 경보가 된다. design .sw/boss/design.md §14.7 「토큰이 없으면 반격이 안 나온다」의 실제 자리.
	if (!Combat->HasAttackToken())
	{
		return;
	}
	// 반경은 애니 고유 단위 - 반격 클립의 손 뻗음은 몸 배율만큼 커지므로 곱한다(design .sw/boss/design.md §15).
	const float Distance      = FVector::Dist2D(Avatar->GetActorLocation(), Target->GetActorLocation());
	const float TriggerRadius = Set->DownAttackTriggerRadius * AVBEnemyBase::ResolveStrideScale(Avatar);
	if (Distance > TriggerRadius)
	{
		return;
	}
	if (TryStartDownAttack(*Set, Now))
	{
		VB_LOG(Log, "[다운] %s 반격 - 표적 거리 %.0f (반경 %.0f)", *GetNameSafe(Avatar), Distance, TriggerRadius);
	}
}

bool UVBGA_Knockdown::TryStartDownAttack(const FVBKnockdownSet& Set, double Now)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (!ASC)
	{
		return false;
	}
	// 간격 시계는 시도 시점에 찍는다 - 실패(GA 없음·거부)도 다음 시도까지 간격을 둬서 같은 경고가 매 0.2초 찍히지 않게.
	LastDownAttackTime = Now;

	// BT 태스크(UVBBTTask_ActivateAbility)와 같은 함수로 스펙을 찾는다 - 태그로 GA 를 고르는 홈은 하나다.
	const FGameplayAbilitySpec* Spec = UVBAbilitySystemStatics::FindActivatableAbilitySpecByInputTag(ASC, Set.DownAttackInputTag);
	if (!Spec)
	{
		VB_LOG(Warning, "[다운] %s 반격 태그 %s 에 맞는 GA 가 없다 - 보스 BP 의 DefaultAbilities 에 반격 GA 를 넣어라",
		       *GetNameSafe(GetAvatarActorFromActorInfo()), *Set.DownAttackInputTag.ToString());
		return false;
	}
	if (!AbilityEndedHandle.IsValid())
	{
		AbilityEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &ThisClass::OnAnyAbilityEnded);
	}
	// 반격 몽타주의 «블렌드아웃 시작» 도 받는다 - 능력 종료는 그 0.25초 뒤라 그때 대기를 걸면 이음매가 빈다(OnAnyMontageBlendingOut).
	if (UAnimInstance* AnimInstance = GetActorInfo().GetAnimInstance())
	{
		AnimInstance->OnMontageBlendingOut.AddUniqueDynamic(this, &ThisClass::OnAnyMontageBlendingOut);
	}
	DownAttackSpecHandle = Spec->Handle;

	// 활성화 «앞에» 켠다. TryActivateAbility 안에서 GA 가 동기로 끝날 수 있고(토큰·게이트로 즉시 취소), 그 종료가 OnAnyAbilityEnded 로
	//   들어와 대기를 되살려야 한다 - 뒤에 켜면 그 종료를 놓쳐 대기 몽타주가 끊긴 채 남는다.
	bDownAttackActive = true;
	if (!ASC->TryActivateAbility(Spec->Handle))
	{
		bDownAttackActive = false;
		VB_LOG(Log, "[다운] %s 반격 활성화 거부(게이트·토큰)", *GetNameSafe(GetAvatarActorFromActorInfo()));
		return false;
	}
	return true;
}

void UVBGA_Knockdown::OnAnyMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted)
{
	// 반격이 제 끝에서 빠지기 시작한 순간이 대기가 들어갈 자리다(2026-09-29 로그: 반격 블렌드아웃 33.52, 능력 종료 33.77 -
	//   그 사이 몸을 덮는 몽타주가 없어 서 있는 자세가 0→100% 드러났고 곧바로 기상이 이어져 «일어날 때 뜬다» 로 보였다).
	// 가려내는 법: 반격 중이고, 끊긴 게 아니라 제 끝에서 빠지며, 우리 그로기 몽타주가 아니다. 보스에겐 예고 몽타주가 없어
	//   반격 중에 도는 남의 몽타주는 휘두르는 것 하나뿐이다(DA_Enemy_BossTroll·DA_Attack_Boss_Down TelegraphMontage 없음, 2026-09-29 리드백).
	//   끊긴 반격은 여기서 안 받는다 - 끊은 쪽 몽타주가 몸을 덮고 있으므로 능력 종료 알림(OnAnyAbilityEnded)으로 충분하다.
	const FVBKnockdownSet* Set = GetKnockdownSet();
	if (!bDownAttackActive || bInterrupted || !Montage || !Set)
	{
		return;
	}
	const FVBHitReactSet& Hits = Set->DownHitSet;
	const auto InSet = [Montage](const FVBHitReactSet& S)
	{
		return Montage == S.Montage || Montage == S.MontageFront || Montage == S.MontageBack || Montage == S.MontageLeft || Montage == S.MontageRight;
	};
	// 플린치도 우리 것이다 - 반격 직전에 틀린 플린치가 제 끝에서 빠지는 것을 «반격이 끝났다» 로 읽으면 휘두르는 중에 대기가 덮는다.
	const bool bOwnMontage = Montage == Set->FallMontage || Montage == Set->DownIdleMontage || Montage == Set->GetUpMontage ||
	                         InSet(Hits) || (HitReactConfig && InSet(HitReactConfig->FlinchSet));
	if (bOwnMontage)
	{
		return;
	}
	// 반격 능력은 아직 살아 있다(몽타주 완료 때 끝난다). 여기서 «반격 중» 을 내리면 뒤늦은 종료 알림은 OnAnyAbilityEnded 첫 줄에서 걸러진다.
	bDownAttackActive = false;
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::OnAnyAbilityEnded(const FAbilityEndedData& EndedData)
{
	if (!bDownAttackActive || EndedData.AbilitySpecHandle != DownAttackSpecHandle)
	{
		return;
	}
	bDownAttackActive = false;
	ResumeAfterInterlude();
}

void UVBGA_Knockdown::BeginGetUp()
{
	const FVBKnockdownSet* Set = GetKnockdownSet();
	Phase = EKnockdownPhase::GettingUp;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldTimer);
		World->GetTimerManager().ClearTimer(WatchTimer);
	}
	if (!Set || !Set->GetUpMontage)
	{
		// IsAuthored 가 진입을 막으므로 여기 오는 길은 없다. 오면 자세는 틀리되 상태는 닫힌다.
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}
	UAbilityTask_PlayMontageAndWait* GetUp = MakeMontageTask(Set->GetUpMontage);
	GetUp->OnBlendOut.AddDynamic(this, &ThisClass::OnGetUpEnded);
	GetUp->OnInterrupted.AddDynamic(this, &ThisClass::OnGetUpEnded);
	GetUp->OnCancelled.AddDynamic(this, &ThisClass::OnGetUpEnded);
	GetUp->ReadyForActivation();
}

void UVBGA_Knockdown::OnGetUpEnded()
{
	if (Phase != EKnockdownPhase::GettingUp)
	{
		return;
	}
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
