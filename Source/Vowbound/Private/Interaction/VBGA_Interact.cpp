// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBGA_Interact.h"

#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Abilities/Tasks/AbilityTask_ApplyRootMotionMoveToForce.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/VBCharacter.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "Engine/World.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBPushableComponent.h"
#include "Interaction/VBPushableConfig.h"
#include "Interaction/VBToggleProp.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "Vowbound/Vowbound.h"

UVBGA_Interact::UVBGA_Interact()
{
	InputTag = VBGameplayTags::Input_Interact;

	// 외부에서 이 GA 를 끊으려면 AssetTag 가 있어야 한다 - 엔진의 CancelAbilities 계열은 대상 GA 의 AssetTags 를 맞춘다.
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VBGameplayTags::Ability_Interact);
	SetAssetTags(AssetTags);

	ActivationOwnedTags.AddTag(VBGameplayTags::State_Interacting);

	// 몸이 다른 일에 묶여 있으면 시작하지 않는다. 락온은 막지 않는다(user 결정 2026-09-23).
	// 트래버설(매달림·등반)은 AVBCharacter::HandleAbilityInputPressed 의 관문이 이미 막는다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Finishing);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Dodging);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Staggered);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_HitReacting);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_GettingUp);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Weapon_Summoning);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Weapon_Sheathing);

	// 홀드 동안 공격·회피·가드를 막는다. 그것들이 돌면 칼이 수납된 채 공격 몽타주가 나가거나
	//   레버를 당기는 중에 몸이 굴러간다. 급하면 버튼을 떼면 된다 - 떼는 순간 취소되고 차단도 풀린다.
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Attack);
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Defense);
}

void UVBGA_Interact::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                    const FGameplayAbilityActorInfo*     ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                    const FGameplayEventData*            TriggerEventData)
{
	bHoldInteraction = false;
	bMotionCommitted = false;
	bFastCloseRequested = false;
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	if (!Interactor)
	{
		// 배선 사고다. 막을 수 없으니 말한다 - 조용히 끝나면 「E 를 눌러도 아무 일 없다」로만 보인다.
		VB_LOG(Warning, "%s: %s 에 UVBInteractorComponent 가 없다", *GetName(), *GetNameSafe(Avatar));
		if (ActorInfo && ActorInfo->IsNetAuthority())
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}
		return;
	}

	// 로컬 표시. 원격 클라는 여기까지만 한다 - 판정은 서버 몫이고, 여기서 EndAbility 를 부르면
	//   그것이 서버로 올라가 서버에서 도는 홀드를 끊는다(UVBGA_Execute 의 같은 주석 참조).
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		const UVBInteractableComponent* Focus = Interactor->GetFocusedInteractable();
		bHoldInteraction = Focus && Focus->GetConfig()->Mode == EVBInteractMode::Hold;
		if (Focus && Focus->GetConfig()->Mode == EVBInteractMode::Hold)
		{
			Interactor->BeginHoldDisplay(Focus->GetConfig()->HoldDuration);
		}
	}
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}

	UVBInteractableComponent* Target = Interactor->FindBestInteractable();
	if (!Target)
	{
		// 흔한 경로다(아무것도 없는 곳에서 E). 로그는 Verbose 로 둔다.
		VB_LOG(Verbose, "%s: 닿는 상호작용 대상 없음", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	// 조건 칸(.sw/prologue-opening 14.2): 밀기 예약·수납·몸 동작 전에 끊는다 - 몸이 움직인 뒤 거절하면 «했는데 안 됐다» 로 보인다.
	//   완료 때 NotifyInteracted 가 한 번 더 본다(동작 도중에 바뀐 경우).
	if (!Target->PassesUseConditions(Avatar))
	{
		Refuse(Target, Avatar);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	const UVBInteractableConfig* Config = Target->GetConfig();
	bHoldInteraction = Config->Mode == EVBInteractMode::Hold;
	if (Config->Mode == EVBInteractMode::Push)
	{
		UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target);
		if (!Pushable || !Pushable->Reserve(GetAvatarCharacter()))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		ActiveTarget = Target;
		SheatheIfWanted(Config);
		StartMotion(Target, Config->ActivateMotion);
		return;
	}

	if (Config->Mode == EVBInteractMode::Tap)
	{
		// 이번에 켜는가 끄는가는 지금 상태의 반대다. 소품도 같은 규칙으로 뒤집는다(AVBToggleProp).
		const bool bActivating = !Target->IsActivated();
		const AActor* Prop = Target->GetOwner();
		const AVBCharacter* VBCharacter = Cast<AVBCharacter>(Avatar);
		const bool bRunning = VBCharacter && VBCharacter->GetIsSprinting()
			&& VBCharacter->GetCharacterMovement()->IsMovingOnGround();
		if (const FVBInteractionMotion* Motion =
				Prop ? Config->SelectMotion(bActivating, Prop->GetActorTransform(), Avatar->GetActorLocation(),
					bRunning, Avatar->GetVelocity()) : nullptr)
		{
			ActiveTarget = Target;
			SheatheIfWanted(Config);
			StartMotion(Target, *Motion);
			return;
		}
		// 방향별 동작을 저작한 대상에서 반대편 모션이 없다고 즉시 토글로 우회하지 않는다.
		if (Config->bUseSidePlane)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		const bool bCompleted = Target->NotifyInteracted(Avatar);
		if (!bCompleted)
		{
			Refuse(Target, Avatar);
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, !bCompleted);
		return;
	}

	ActiveTarget = Target;
	SheatheIfWanted(Config);

	GetWorld()->GetTimerManager().SetTimer(HoldTimerHandle, this, &UVBGA_Interact::HandleHoldCompleted,
	                                       Config->HoldDuration, false);
	VB_LOG(Log, "%s: 홀드 시작 %s.%s (%.2f초)", *GetName(), *GetNameSafe(Target->GetOwner()), *Target->GetName(),
	       Config->HoldDuration);
}

void UVBGA_Interact::SheatheIfWanted(const UVBInteractableConfig* Config)
{
	// 이미 수납돼 있으면(맨손·트래버설 수납) 손대지 않는다 - 돌려놓는 것도 우리 몫이 아니게 된다.
	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (!Config || !Config->bSheatheWeapon || !Avatar)
	{
		return;
	}
	if (UVBWeaponStateComponent* Weapon = Avatar->FindComponentByClass<UVBWeaponStateComponent>();
		Weapon && Weapon->IsWeaponSummoned() && !Weapon->IsTempSheathed())
	{
		Weapon->TempSheatheForParkour();
		bSheathedWeapon = true;
	}
}

void UVBGA_Interact::StartMotion(UVBInteractableComponent* Target, const FVBInteractionMotion& Motion)
{
	ACharacter* Character = GetAvatarCharacter();
	UVBInteractorComponent* Interactor = Character ? Character->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	if (!Character || !Interactor)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	// 설 자리 = 소품 루트 기준 변환. 팩 애니의 원점은 발바닥이라 캡슐 중심은 반 높이만큼 위다.
	const float HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FTransform PropFrame = Target->GetOwner()->GetActorTransform();
	if (Motion.bStandFromApproach)
	{
		// 방향 없는 소품: 기준 = 소품 자리 + 다가온 방향(FVBInteractionMotion::bStandFromApproach 주석). 높이는 플레이어 발이다 -
		//   소품의 원점 높이(메시 중심)는 소품마다 달라 설 자리의 높이로 못 쓴다.
		const FVector PropLocation = PropFrame.GetLocation();
		FVector ToProp = PropLocation - Character->GetActorLocation();
		ToProp.Z = 0.0f;
		const float Yaw = ToProp.IsNearlyZero() ? Character->GetActorRotation().Yaw : ToProp.Rotation().Yaw;
		PropFrame = FTransform(FRotator(0.0f, Yaw, 0.0f),
		                       FVector(PropLocation.X, PropLocation.Y, Character->GetActorLocation().Z - HalfHeight));
	}
	const UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target);
	const FTransform Stand = Pushable ? Pushable->GetStandTransform() : Motion.StandTransform * PropFrame;
	const FVector Destination = Stand.GetLocation() + FVector(0.0f, 0.0f, HalfHeight);
	AlignDestination = Destination;

	// 방향은 이동 시작에 한 번 맞춘다. 이동 도중 PhysicsRotation 이 되돌리지 못하게 캡슐 방향의 주인이 된다.
	const uint8 MotionIndex = Target->GetConfig()->GetMotionIndex(Motion);
	bTargetInitiallyActivated = Target->IsActivated();
	Interactor->BeginMotion(Target, MotionIndex);
	VB_LOG(Log, "%s: 대상=%s 동작=%d 몽타주=%s", *GetName(), *GetNameSafe(Target->GetOwner()),
		MotionIndex, *GetNameSafe(Motion.CharacterMontage));
	Character->SetActorRotation(FRotator(0.0f, Stand.Rotator().Yaw, 0.0f), ETeleportType::None);
	PendingMontage = Motion.CharacterMontage;

	const float Duration = Target->GetConfig()->AlignDuration;
	if (Duration <= KINDA_SMALL_NUMBER)
	{
		Character->SetActorLocation(Destination, /*bSweep=*/true);
		HandleAlignFinished();
		return;
	}

	UAbilityTask_ApplyRootMotionMoveToForce* MoveTask = UAbilityTask_ApplyRootMotionMoveToForce::ApplyRootMotionMoveToForce(
		this, NAME_None, Destination, Duration, /*bSetNewMovementMode=*/false, MOVE_Walking,
		/*bRestrictSpeedToExpected=*/false, /*PathOffsetCurve=*/nullptr,
		ERootMotionFinishVelocityMode::SetVelocity, FVector::ZeroVector, 0.0f);
	MoveTask->OnTimedOut.AddDynamic(this, &UVBGA_Interact::HandleAlignFinished);
	MoveTask->OnTimedOutAndDestinationReached.AddDynamic(this, &UVBGA_Interact::HandleAlignFinished);
	MoveTask->ReadyForActivation();
	VB_LOG(Log, "%s: 설 자리로 이동 %s (%.0fcm, %.2f초)", *GetName(), *GetNameSafe(Target->GetOwner()),
	       FVector::Dist(Character->GetActorLocation(), Destination), Duration);
}

void UVBGA_Interact::HandleAlignFinished()
{
	// 두 신호(시간 초과 / 도착)가 한 틱에 둘 다 올 수 있다. 몽타주를 이미 걸었으면 무시한다.
	UAnimMontage* Montage = PendingMontage;
	PendingMontage = nullptr;
	UVBInteractableComponent* Target = ActiveTarget.Get();
	if (!Montage)
	{
		return;
	}
	ACharacter* Character = GetAvatarCharacter();
	const UVBInteractorComponent* Interactor = Character ? Character->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	const FVBInteractionMotion* Motion = Interactor ? Interactor->GetActiveMotion() : nullptr;
	if (!Target || !Character || !Motion || !Target->IsInteractionEnabled() || !IsValid(Target->GetOwner())
		|| Target->IsActivated() != bTargetInitiallyActivated)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target);
	// 설 자리는 StartMotion 이 정한 그 자리다(AlignDestination 주석).
	const FVector Destination = AlignDestination;
	if (FVector::Dist(Character->GetActorLocation(), Destination) > Target->GetConfig()->AlignTolerance)
	{
		VB_LOG(Warning, "%s: 정렬 실패로 취소 대상=%s 오차=%.1fcm", *GetName(), *GetNameSafe(Target->GetOwner()),
			FVector::Dist(Character->GetActorLocation(), Destination));
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	if (!FMath::IsFinite(Motion->CommitTime) || Motion->CommitTime < 0.0f
		|| Motion->CommitTime >= Montage->GetPlayLength())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	Character->GetCharacterMovement()->StopMovementImmediately();
	if (Pushable && !Pushable->StartPushing(Character))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}

	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, Montage, bFastCloseRequested ? FMath::Clamp(Target->GetConfig()->FastCloseRate, 1.0f, 3.0f) : 1.0f,
		Pushable ? FName(TEXT("Grab")) : NAME_None, /*bStopWhenAbilityEnds=*/true);
	MontageTask->OnCompleted.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->OnCancelled.AddDynamic(this, &UVBGA_Interact::HandleMotionMontageEnded);
	MontageTask->ReadyForActivation();
	// 재생 실패 콜백이 동기적으로 능력을 끝낼 수 있다.
	if (!IsActive()) return;
	if (Pushable) return;
	if (Motion->CommitTime <= KINDA_SMALL_NUMBER) CommitMotion();
	else GetWorld()->GetTimerManager().SetTimer(MotionCommitTimerHandle, this, &UVBGA_Interact::CommitMotion,
		Motion->CommitTime / FMath::Max(0.01f, Character->GetMesh()->GetAnimInstance()->Montage_GetPlayRate(Montage)), false);
}

void UVBGA_Interact::CommitMotion()
{
	if (!IsActive() || bMotionCommitted || !GetCurrentActorInfo()->IsNetAuthority()) return;
	ACharacter* Character = GetAvatarCharacter();
	UVBInteractableComponent* Target = ActiveTarget.Get();
	const UVBInteractorComponent* Interactor = Character ? Character->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	const FVBInteractionMotion* Motion = Interactor ? Interactor->GetActiveMotion() : nullptr;
	UAnimInstance* Anim = Character ? Character->GetMesh()->GetAnimInstance() : nullptr;
	if (!Target || !Motion || !Anim || !Anim->Montage_IsPlaying(Motion->CharacterMontage)
		|| !Target->IsInteractionEnabled() || Target->IsActivated() != bTargetInitiallyActivated
		|| !Interactor->CanReach(Target))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		return;
	}
	const float Remaining = Motion->CommitTime - Anim->Montage_GetPosition(Motion->CharacterMontage);
	if (Remaining > KINDA_SMALL_NUMBER)
	{
		GetWorld()->GetTimerManager().SetTimer(MotionCommitTimerHandle, this, &UVBGA_Interact::CommitMotion,
			FMath::Max(Remaining / FMath::Max(0.01f, Anim->Montage_GetPlayRate(Motion->CharacterMontage)), 0.01f), false);
		return;
	}
	bMotionCommitted = true;
	if (!Target->NotifyInteracted(Character))
	{
		// 동작은 이미 접촉 시점이다. 몽타주는 끝까지 두고(끊으면 손이 튄다) 상태·효과만 안 낸다.
		Refuse(Target, Character);
		return;
	}
	if (bFastCloseRequested)
	{
		if (AVBToggleProp* Prop = Cast<AVBToggleProp>(Target->GetOwner())) Prop->SetInteractionPlayRate(Target->GetConfig()->FastCloseRate);
	}
}

void UVBGA_Interact::HandleRepeatPress()
{
	if (!IsActive() || !GetCurrentActorInfo()->IsNetAuthority()) return;
	UVBInteractableComponent* Target = ActiveTarget.Get();
	if (!Target) return;
	if (UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(Target))
	{
		if (PendingMontage) EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, true);
		else Pushable->RequestRelease(GetAvatarCharacter());
		return;
	}
	const float Rate = Target->GetConfig()->FastCloseRate;
	if (!bTargetInitiallyActivated || bFastCloseRequested || !FMath::IsFinite(Rate) || Rate <= 1.0f) return;
	AVBToggleProp* Prop = Cast<AVBToggleProp>(Target->GetOwner());
	if (!Prop) return;
	bFastCloseRequested = true;
	if (PendingMontage) return;
	GetAbilitySystemComponentFromActorInfo()->CurrentMontageSetPlayRate(FMath::Clamp(Rate, 1.0f, 3.0f));
	if (bMotionCommitted) Prop->SetInteractionPlayRate(Rate);
	else CommitMotion(); // Recalculate the contact timer against the accelerated montage clock.
}

void UVBGA_Interact::HandleMotionMontageEnded()
{
	if (IsActive())
	{
		EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
	}
}

void UVBGA_Interact::HandleHoldCompleted()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	UVBInteractableComponent* Target = ActiveTarget.Get();

	// 누르는 동안 걸어서 벗어났거나 대상이 꺼졌을 수 있다. 그때는 완료가 아니라 취소다.
	if (!Interactor || !Interactor->CanReach(Target))
	{
		VB_LOG(Log, "%s: 홀드 시간은 찼으나 대상에 더는 닿지 않는다 - 취소 (%s)", *GetName(), *GetNameSafe(Target));
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
		return;
	}

	const bool bCompleted = Target->NotifyInteracted(Avatar);
	if (!bCompleted)
	{
		Refuse(Target, Avatar);
	}
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, !bCompleted);
}

void UVBGA_Interact::Refuse(UVBInteractableComponent* Target, AActor* Avatar)
{
	if (!Target)
	{
		return;
	}
	Target->ApplyRefused(Avatar);
	if (UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr)
	{
		Interactor->ClientInteractionRefused(Target->GetRefusedText());
	}
}

void UVBGA_Interact::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                                const FGameplayAbilityActorInfo*     ActorInfo,
                                const FGameplayAbilityActivationInfo ActivationInfo,
                                bool                                 bReplicateEndAbility,
                                bool                                 bWasCancelled)
{
	// 종료 경로가 넷이다 - 탭 완료 / 홀드 완료 / 뗌·피격 취소 / 대상 없음. 정리는 여기 한 곳에서 한다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HoldTimerHandle);
		World->GetTimerManager().ClearTimer(MotionCommitTimerHandle);
	}
	if (UVBPushableComponent* Pushable = Cast<UVBPushableComponent>(ActiveTarget.Get())) Pushable->StopPushing(GetAvatarCharacter());
	ActiveTarget.Reset();
	bHoldInteraction = false;

	PendingMontage = nullptr;

	AActor* Avatar = GetAvatarActorFromActorInfo();
	if (UVBInteractorComponent* Interactor = Avatar ? Avatar->FindComponentByClass<UVBInteractorComponent>() : nullptr)
	{
		Interactor->EndHoldDisplay();
		// 캡슐 방향을 돌려준다. 몸 동작이 아니었으면 이미 false 라 무동작이다.
		Interactor->EndMotion();
	}

	if (bSheathedWeapon)
	{
		bSheathedWeapon = false;
		// 죽어서 끝났으면 돌려놓지 않는다 - 시체 손에 칼이 돌아온다.
		if (!UVBAbilitySystemStatics::IsActorDead(Avatar))
		{
			if (UVBWeaponStateComponent* Weapon = Avatar ? Avatar->FindComponentByClass<UVBWeaponStateComponent>() : nullptr)
			{
				Weapon->RestoreFromTempSheathe();
			}
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
