// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Dodge.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Character.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"

EVBDodgeDirection UVBGA_Dodge::ResolveDirection(const AActor* Avatar, const FVector& WorldDir)
{
	if (!Avatar)
	{
		return EVBDodgeDirection::Backward; // 아바타 없음 = 정지 폴백과 동일 취급
	}

	// 월드 방향을 액터 로컬로 — UE 규약 X=전방, Y=우측. 클립의 내부 root 축 규약과 무관하다
	// (클립은 방향별 전용 저작이라 여기선 '어느 클립을 고를까'만 판단한다).
	const FVector Local = Avatar->GetActorTransform().InverseTransformVectorNoScale(WorldDir);

	if (FMath::Abs(Local.X) >= FMath::Abs(Local.Y))
	{
		return (Local.X >= 0.0f) ? EVBDodgeDirection::Forward : EVBDodgeDirection::Backward;
	}
	return (Local.Y >= 0.0f) ? EVBDodgeDirection::Right : EVBDodgeDirection::Left;
}

UVBGA_Dodge::UVBGA_Dodge()
{
	InputTag = VBGameplayTags::Input_Dodge;
	// 타이머 핸들을 쓰려면 필수 (InstancingPolicy는 베이스에서도 InstancedPerActor라 중복이지만 명시 유지)
	InstancingPolicy   = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	// NetExecutionPolicy: 베이스(VBGameplayAbility) ServerInitiated 상속 (2026-04-20 전환 후 명시 오버라이드 제거)
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Dodging);

	// 활성화 게이트 신설 (FIND-054 동반 발견, 2026-07-26)
	// 회피는 그동안 ActivationBlockedTags 가 하나도 없어 사망 중에도 활성화됐다. 다른 GA 는 전부 사망을
	// 막으므로(VBGA_Guard.cpp:32 / VBGA_HitReact.cpp:50 / VBGA_WeaponSummon.cpp:15) Dodge 도 규칙에 편입한다.
	//
	// 이 게이트는 살아 있다(2026-08-14 정정). 종전 주석은 "State.Dead 부여처가 적 하나뿐이고 플레이어
	//   사망은 미구현이라 발동하지 않는다"고 적었는데, 지금은 셋 다 거짓이다 — AVBCharacter::HandleDeath
	//   (VBCharacter.cpp:221)가 존재하고 :228 에서 State.Dead 를 부여한다. 적 쪽도 부여처가 둘이다
	//   (VBEnemyBase.cpp:242 / :272). 죽은 게이트로 오인해 제거하지 말 것.
	//
	// State.Combat.Guarding 은 일부러 넣지 않는다. 가드 캔슬 회피는 의도된 동작이다(user 확정 2026-07-26).
	//   한 번 넣었다가 되돌린 이력이 있으니 "공격은 막는데 회피는 왜 안 막나"라는 대칭 논리로 재추가 금지.
	//   가드는 '버티기'고 회피는 '빠지기'다. 블록 자세에서 롤로 이탈하는 건 이 장르의 기본 방어 문법이라
	//   막으면 손이 갇힌다. 공격(LMB) 차단의 근거였던 판정 모순(자기 스윙 중 패링 판정이 살아남음)이
	//   회피엔 없다: 데미지 hub 가 dodge i-frame 을 가드 분기보다 먼저 처리해
	//   (VBHealthAttributeSet.cpp:70-79 이 DamageValue=0 → :85 의 `> 0.0f` 재검사에서 가드 분기 진입 차단)
	//   회피 중 피격은 가드 판정에 도달하지 않는다.
	// 알려진 잔여(수용됨): 회피가 가드 몽타주를 같은 DefaultSlot 에서 덮으므로, 회피 후 RMB 를 계속 쥐고 있어도
	//   가드 포즈는 돌아오지 않는다(BeginGuard 는 GA 활성 시 1회만 호출). 가드 '상태'는 유지되므로 판정은 정상.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
}

void UVBGA_Dodge::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                  const FGameplayAbilityActivationInfo ActivationInfo,
                                  const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// ServerInitiated 전환으로 클라에서도 실행됨. 서버 권위 작업 분리.
	const bool bIsServer = ActorInfo && ActorInfo->IsNetAuthority();

	ACharacter* AvatarCharacter = GetAvatarCharacter();
	if (!AvatarCharacter)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (!DodgeConfig)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (AVBCharacter* VBChar = Cast<AVBCharacter>(AvatarCharacter))
	{
		VBChar->NotifyCombatStimulus(); // 내부에서 HasAuthority 가드 처리됨. 회피는 발도 없는 전투 자극 (DEC-006 ①)
	}

	// ── 입력 방향 획득 (서버·클라 공통. 몽타주 선택에 필요하므로 서버 전용 블록 밖에 둔다) ──
	// fallback 체인:
	//   1) PendingInputVector: 이번 틱에 누적된 input (가장 신선)
	//   2) LastInputVector: 지난 틱 소비된 input (이번 틱 Pending이 비면 보조)
	//   3) Velocity 2D: 현재 이동 중이면 속도 방향 (관성 반영)
	//   4) -ForwardVector: 진짜 정지 상태에서만 뒤로 회피
	// Enhanced Input fire 순서는 비결정적 → HandleMove가 HandleDodge보다 늦게 불리면 Last는 stale.
	FVector InputDirection = AvatarCharacter->GetPendingMovementInputVector();
	if (InputDirection.IsNearlyZero())
	{
		InputDirection = AvatarCharacter->GetLastMovementInputVector();
	}
	if (InputDirection.IsNearlyZero())
	{
		FVector Vel = AvatarCharacter->GetVelocity();
		Vel.Z = 0.0f;
		if (!Vel.IsNearlyZero())
		{
			InputDirection = Vel.GetSafeNormal();
		}
	}
	if (InputDirection.IsNearlyZero())
	{
		// 진짜 정지 상태: 뒤쪽으로 회피 (fallback)
		InputDirection = -AvatarCharacter->GetActorForwardVector();
	}
	InputDirection.Normalize();

	// ── 무기 타입 + 방향으로 닷지 몽타주 해석 (무기세트 → 통일세트 → nullptr) ──
	EVBWeaponType WeaponType = EVBWeaponType::None;
	if (const AVBCharacter* VBChar = Cast<AVBCharacter>(AvatarCharacter))
	{
		if (const UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent())
		{
			WeaponType = WSC->GetCurrentWeaponType();
		}
	}
	const EVBDodgeDirection DodgeDir = ResolveDirection(AvatarCharacter, InputDirection);
	UAnimMontage* DodgeMontage = DodgeConfig->ResolveDodgeMontage(WeaponType, DodgeDir);

	if (DodgeMontage)
	{
		// ── ① 루트모션 경로 ── 몽타주가 이동을 담당한다. LaunchCharacter 는 호출하지 않는다(2배 이동 방지).
		// bStopWhenAbilityEnds = false 필수: 무적창(DodgeDuration 0.4s)이 끝나 GA 가 종료돼도 몽타주는 계속 재생돼야
		//   클립 루트모션이 끝까지 적용된다(클립 0.67~1.20s). true 면 0.4s 에 잘려 이동거리가 1/3 로 truncate 된다.
		//   = "무적은 짧게, 회피 동작은 끝까지, 후반은 위험 구간" (설계 §2.5).
		// 양쪽(서버+소유 클라) 재생 — ServerInitiated 라 클라도 여기 도달, 로컬 재생이 시각 정합의 핵심.
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this,
				NAME_None,
				DodgeMontage,
				DodgeConfig->DodgeMontagePlayRate,
				NAME_None,
				/*bStopWhenAbilityEnds=*/false,
				1.0f,
				0.0f,
				true);
		MontageTask->ReadyForActivation();
	}
	else if (bIsServer)
	{
		// ── ② 레거시 안전망 ── 몽타주 미배선(미등록 무기/통일세트 공백) 시 현행 물리 추진 유지 = 회귀 0.
		// LaunchCharacter 가 서버 전용인 이유: 서버 호출이면 CMC Velocity 변경이 소유자 클라로 복제된다.
		// 클라가 따로 호출하면 prediction 없이 순간 점프 → 서버 교정 시 jitter.
		AvatarCharacter->LaunchCharacter(
										 InputDirection * DodgeConfig->DodgeStrength + DodgeConfig->DodgeVerticalBoost,
										 true,
										 true);
	}

	// Timer→EndAbility 는 서버 전용: 서버 권위로 종료 시 ClientEndAbility RPC로 클라에 전파됨.
	// 클라 자체 타이머는 이중 종료 위험. 이 타이머가 무적창(State.Combat.Dodging)의 수명이다.
	if (bIsServer)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
			                                  DodgeTimerHandle,
			                                  FTimerDelegate::CreateWeakLambda(this, [this, Handle, ActorInfo,
				                                                                   ActivationInfo]()
			                                                                   {
				                                                                   EndAbility(Handle, ActorInfo,
						                                                                    ActivationInfo, true,
						                                                                    false);
			                                                                   }),
			                                  DodgeConfig->DodgeDuration,
			                                  false); // 반복 안함
		}
	}
}

void UVBGA_Dodge::EndAbility(const FGameplayAbilitySpecHandle     Handle, const FGameplayAbilityActorInfo* ActorInfo,
                             const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                             bool                                 bWasCancelled)
{
	// 타이머 정리
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DodgeTimerHandle);
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
