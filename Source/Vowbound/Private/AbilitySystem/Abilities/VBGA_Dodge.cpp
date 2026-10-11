// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Dodge.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h" // SetLooseGameplayTagCount - 회피 커밋 태그
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"   // 앉은 채 회피 첫 프레임 로그의 캡슐 반높이
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Animation/AnimMontage.h"
#include "Vowbound/Vowbound.h"   // VB_LOG

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

	// 이 어빌리티가 «무엇인가» 를 말하는 자산 태그. 남이 이것을 차단 목록으로 걸 때 쓰는 이름이다.
	//
	// 왜 BP 가 아니라 여기인가: 이것은 튜닝값이 아니라 클래스의 정체다. BP 저작에 맡겼더니 실제로
	//   안 버텼다 - 2026-09-13 실측으로 어빌리티 BP 19개 중 10개가 비어 있었고, 그중 하나가 이것이라
	//   UVBGA_Execute 의 Ability.Defense 차단이 회피에만 조용히 무동작이었다(가드는 걸렸다).
	//   BP 에 한 번 넣어 봤지만 에디터 재시작을 못 넘겼다 - 저장 경로에 기대는 것 자체가 약한 자리다.
	// 부작용: 없다. 태그가 늘 뿐이고 이 GA 의 동작은 안 바뀐다.
	AbilityTags.AddTag(VBGameplayTags::Ability_Defense_Dodge);

	// 활성화 게이트 신설 (FIND-054 동반 발견, 2026-07-26)
	// 회피는 그동안 ActivationBlockedTags 가 하나도 없어 사망 중에도 활성화됐다. 다른 GA 는 전부 사망을
	// 막으므로(VBGA_Guard.cpp:32 / VBGA_HitReact.cpp:50 / VBGA_WeaponSummon.cpp:15) Dodge 도 규칙에 편입한다.
	//
	// 이 게이트는 살아 있다(2026-08-14 정정). 종전 주석은 "State.Dead 부여처가 적 하나뿐이고 플레이어
	//   사망은 미구현이라 발동하지 않는다"고 적었는데, 지금은 셋 다 거짓이다 — AVBCharacter::HandleDeath
	//   (VBCharacter.cpp::AVBCharacter::HandleDeath)가 존재하고 거기서 State.Dead 를 부여한다. 적 쪽도 부여처가 둘이다
	//   (VBEnemyBase.cpp::AVBEnemyBase::HandleDeath / ::MulticastHandleDeathCosmetic_Implementation). 죽은 게이트로 오인해 제거하지 말 것.
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

	// 앉은 채 회피하면 일어서며 회피한다(설계 .sw/crouch-montage 2.3 - 공격과 같은 규칙 «행동은 서서 한다»).
	//   회피 몽타주는 서기 전신 클립이라 앉은 캡슐 위에서 돌면 다리가 땅에 박히고 끝나면 앉기로 튄다(감사 2026-10-03).
	//   앉기 능력이 끝나면 그 EndAbility 가 양쪽에서 UnCrouch 한다. 무적 창 때문에 공격처럼 «선 뒤에» 로 미루지 않는다 -
	//   캡슐 높이가 다음 이동 틱에 돌아오는 한 틱의 어긋남은 받아들인다(PIE 확인 항목).
	CancelAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Locomotion_Crouch);
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

	// 앉은 채 시작한 회피의 첫 프레임을 남긴다(설계 .sw/crouch-montage 2.3, Codex 01 검토 10-03). 앉기는 활성화 때 이미 취소돼 UnCrouch 가 나갔지만
	//   캡슐은 이동 틱이 실제로 세운다 - 그 사이 서기 회피 몽타주가 앉은 캡슐 위에 한 틱이라도 있었는지는 PIE 로만 보인다. 이 줄이 그 자리의 상태다.
	if (AvatarCharacter->bIsCrouched)
	{
		VB_LOG(Log, "회피: 앉은 채 시작 - 앉기 취소됨, 캡슐 반높이 %.1f(아직 앉은 높이), 이동 틱에 선다 [%s]",
			AvatarCharacter->GetCapsuleComponent() ? AvatarCharacter->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : -1.0f,
			bIsServer ? TEXT("서버") : TEXT("클라"));
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

	// 회피 커밋 (design §7): 캐릭터가 서버 ASC 에 DodgeCommit 태그를 세우고 AttackCancelSeconds 뒤 푼다. 타이머의 주인이 캐릭터인 이유는 헤더 주석.
	if (bIsServer && DodgeConfig->AttackCancelSeconds > 0.0f)
	{
		if (AVBCharacter* VBChar = Cast<AVBCharacter>(AvatarCharacter))
		{
			VBChar->BeginDodgeCommit(DodgeConfig->AttackCancelSeconds, DodgeConfig->AttackBufferSeconds);
		}
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
	// 취소로 끝났으면(피격 등) 커밋도 즉시 푼다 - 저장된 공격 입력은 버린다(플린치에서 공격이 튀어나오지 않게).
	// 정상 종료(무적 타이머)는 커밋을 그대로 둔다 - 커밋(0.45)이 무적(0.4)보다 길고, 그 타이머는 캐릭터가 쥐고 있어 여기서 안 지워진다.
	if (bWasCancelled && ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo()))
		{
			VBChar->CancelDodgeCommit();
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
