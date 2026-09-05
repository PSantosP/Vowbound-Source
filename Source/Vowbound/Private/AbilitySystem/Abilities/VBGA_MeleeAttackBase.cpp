// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h" // EWarpTargetLocationOffsetDirection
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetable.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponMovesetConfig.h"
#include "Data/VBSwingImpactProfile.h" // Cast<> 대상 + LocalSwingAxis 읽기 — 완전 타입 필요
#include "AbilitySystem/Abilities/VBWeaponStyleBehavior.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Animation/VBMotionWarpingNames.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Vowbound/Vowbound.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

namespace
{
	void LogMontageMeshState(const TCHAR* Phase, ACharacter* Character)
	{
		if (!Character || !Character->GetMesh())
		{
			return;
		}

		const UCapsuleComponent*           Capsule  = Character->GetCapsuleComponent();
		const USkeletalMeshComponent*      Mesh     = Character->GetMesh();
		const UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();

		VB_LOG(Warning,
		       "[%s] ActorZ=%.2f CapsuleZ=%.2f MeshWorldZ=%.2f MeshRelZ=%.2f HalfHeight=%.2f MovementMode=%d",
		       Phase,
		       Character->GetActorLocation().Z,
		       Capsule ? Capsule->GetComponentLocation().Z : -9999.0f,
		       Mesh->GetComponentLocation().Z,
		       Mesh->GetRelativeLocation().Z,
		       Capsule ? Capsule->GetScaledCapsuleHalfHeight() : -9999.0f,
		       MoveComp ? (int32)MoveComp->MovementMode : -1);
	}
}

UVBGA_MeleeAttackBase::UVBGA_MeleeAttackBase()
{
	// ServerInitiated: 서버가 활성화 시작 + 소유자 클라에 GA 인스턴스 복제.
	// 이유: ServerOnly는 소유자 클라에 인스턴스가 없어 몽타주 로컬 재생이 불가능.
	//       OnRep_ReplicatedAnimMontage는 IsLocallyControlled()로 소유자 스킵 (엔진 5.7 설계 의도).
	//       ServerInitiated로 전환 → 클라가 자기 GA의 PlayMontageAndWait Task로 로컬 재생 → 몽타주 visible.
	// InstancingPolicy는 VBGameplayAbility 베이스에서 InstancedPerActor 상속 (복제 필수 조건).
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
}

void UVBGA_MeleeAttackBase::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                           const FGameplayAbilityActorInfo*     ActorInfo,
                                           const FGameplayAbilityActivationInfo ActivationInfo,
                                           const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// ServerInitiated 전환으로 이 함수는 서버/클라 양쪽에서 실행된다.
	// 몽타주 재생 Task는 양쪽 공통(로컬 재생), Trace/GE/Event 송신은 서버 전용 가드 필요.
	const bool bIsServer = ActorInfo && ActorInfo->IsNetAuthority();

	ComboIndex       = 0;
	// 변형 재생배율은 활성화마다 초기화한다(VariantCursor 와 반대).
	// 이 값은 "이번에 고른 변형의 배율"이라 다음 활성화로 새어 나가면 안 된다.
	ActiveVariantRate = 1.0f;
	ActiveVariantDamage = 1.0f;
	bComboWindowOpen = false;
	bSaveAttack      = false;

	// 임팩트 쉐이크 프로필도 활성화마다 비운다. 필수다 —
	//  몽타주 부재 폴백 경로는 노티 없이 PerformAttackTrace 를 직접 호출하므로, 리셋이 없으면
	//  직전 스윙의 프로필이 그대로 남아 이번 스윙이 엉뚱한 방향으로 흔들린다.
	ActiveSwingProfile = nullptr;
	ActiveSwingCombatProfile = nullptr;

	// 무브셋 해석: MovesetConfig 할당 시 현재 무기 타입으로 leaf 선택, 아니면 레거시 폴백(회귀 안전).
	ActiveConfig  = AttackConfig;   // 레거시 폴백 기본값(GA에 직접 할당된 config)
	ActiveMontage = AnimMontage;    // 레거시 폴백 기본값(GA에 직접 할당된 montage)

	if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo()))
	{
		// 공격 = 자동 발도 (DEC-006 ③): 무기Off(평상/상태2)면 선택 무기(기억 없으면 기본 무기)를 꺼내며 상태3 직행.
		// 발도를 무브셋 해석보다 먼저 — 서버에선 CurrentWeaponType 이 동기 갱신되어 아래 resolve 가 새 무기를 읽는다.
		// (소유 클라는 복제 지연으로 첫 스윙 한정 레거시 폴백 가능 — FIND-023, MP 하드닝 배치에서 해소)
		if (bIsServer)
		{
			if (UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent())
			{
				const EVBWeaponType SummonType = (WSC->GetCurrentWeaponType() != EVBWeaponType::None)
					? WSC->GetCurrentWeaponType() : WSC->GetDefaultWeaponType();
				WSC->EnsureWeaponSummoned(SummonType);
			}
		}
		VBChar->NotifyCombatStimulus();

		if (MovesetConfig)
		{
			EVBWeaponType WeaponType = EVBWeaponType::None;
			if (const UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent())
			{
				WeaponType = WSC->GetCurrentWeaponType();
			}
			// 현재 무기 타입의 leaf. 미등록이면 nullptr → 레거시 폴백 유지.
			UVBAttackConfig* Leaf = MovesetConfig->GetAttackConfig(WeaponType);
			int32 VariantCount = 0;
			if (Leaf)
			{
				ActiveConfig = Leaf;

				// 변형 해석. 변형이 없으면 결과가 AttackMontage 그대로라 현행과 동일 동작이다.
				VariantCount = Leaf->GetMontageVariantCount();
				const FVBAttackMontageVariant Variant = Leaf->ResolveMontageVariant(VariantCursor);
				if (Variant.Montage)
				{
					ActiveMontage    = Variant.Montage;
					ActiveVariantRate   = Variant.PlayRateScale;
					ActiveVariantDamage = Variant.DamageScale;
				}

				// 변형이 2개 이상일 때만 커서를 민다.
				// 왜: 커서는 GA 하나에 하나인데 ActiveConfig 는 무기에 따라 바뀐다.
				//   변형이 1개인 무기(대검)가 커서를 밀면 카타나 반격이 A -> A 로 반복된다.
				if (VariantCount > 1)
				{
					++VariantCursor;
				}
			}
			// 무브셋 해석 1줄 계측 — 무기 타입/leaf/최종 몽타주/변형 (오선택 진단용)
			VB_LOG(Log, "%s: Moveset resolve — WeaponType=%d Leaf=%s Montage=%s variant=%d/%d rate=%.2f",
				*GetName(), (int32)WeaponType,
				Leaf ? *Leaf->GetName() : TEXT("null(legacy-fallback)"),
				ActiveMontage ? *ActiveMontage->GetName() : TEXT("null"),
				VariantCount > 0 ? ((VariantCursor - 1 + VariantCount) % VariantCount) : 0,
				VariantCount, ActiveVariantRate);
		}
	}

	// 2. 몽타주가 없으면 즉시 Trace 후 종료(폴백)
	if (!ActiveMontage)
	{
		VB_LOG(Warning, "%s: AttackMontage 미설정 -> 즉시 Trace 폴백", *GetName());
		if (bIsServer)
		{
			PerformAttackTrace(); // 권위 전용: 데미지/GE 이중 적용 방지
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	// 거대검 뚝심(SuperArmor): leaf 설정이 슈퍼아머를 요구하면 스윙 동안 피격 히트리액트를 억제할 태그 부여.
	// 억제 방식 = 피격자의 UVBGA_HitReact 가 이 태그를 ActivationBlockedTags 로 걸어 활성화 자체가 안 된다.
	//   (연출을 스킵하는 게 아니라 능력이 안 켜지는 것 — GAS 네이티브 게이트라 커스텀 판정 코드가 0이다.)
	// 로스태그가 복제되지 않아도 무방한 이유: 트리거 활성화는 서버 권위에서만 시도되므로 판정이 읽는 ASC 는 항상 서버 ASC 이고,
	//   서버엔 이 태그가 확실히 있다(이 함수가 서버에서도 실행된다).
	// 이력: 최초 GCN_DamagePhysical BP 그래프 Branch → DEBT-009 1단계 C++ GCN 베이스 → 2단계 GA 게이트(현재, 2026-07-16).
	// ServerInitiated라 이 함수가 서버+소유클라 양쪽에서 실행 → 양쪽 ASC에 부여(히트리액트는 소유클라 로컬 재생이라 클라 태그 필수).
	// 제거는 EndAbility(완료/중단/취소 3경로가 수렴 — BlendOut 은 EndAbility 를 호출하지 않으나 뒤이어 Completed 가 온다)에서 단일화. bGrantedSuperArmor로 부여한 경우만 제거.
	if (ActiveConfig && ActiveConfig->bSuperArmorDuringAttack)
	{
		if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
		{
			// 복제 상태를 명시한다. 생략하면 EGameplayTagReplicationState::None 이 되는데,
			//   그것은 이 프로젝트에서 이 한 곳만 쓰던 값이었다(나머지 로스태그 6곳은 전부 CountToOwner).
			//   판정 자체는 서버에서만 도므로 동작은 안 바뀌고, 같은 종류의 태그가 같은 규약을 갖게 된다.
			OwnerASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_SuperArmor, 1,
			                              EGameplayTagReplicationState::CountToOwner);
			bGrantedSuperArmor = true;
		}
	}

	// 워프 윈도우 진입 시 타겟이 이미 등록이 되어있어야 한다. (양쪽 공통 — 로컬 재생 정렬)
	SetWarpTargetLockedEnemy();

	// 3. PlayMontageAndWait Task — 양쪽 공통 (클라 로컬 재생이 Bug B 해결의 핵심)
	// 실효 재생속도 = GA 기본(전 무기 공유) x 무기 leaf(이 무기의 템포) x 변형(이 변형의 템포).
	// 세 층인 이유는 각각 입도가 다르기 때문이다 - 하나로 합치면 좁은 쪽을 표현할 수 없다.
	const float ConfigRateScale = ActiveConfig ? ActiveConfig->MontagePlayRateScale : 1.0f;
	UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
			                                                               this,
			                                                               NAME_None,
			                                                               ActiveMontage,
			                                                               MontagePlayRate * ConfigRateScale * ActiveVariantRate,
			                                                               NAME_None,
			                                                               true,
			                                                               1.0f,
			                                                               0.0f,
			                                                               true);

	// 4. 몽타주 델리게이트 바인딩
	MontageTask->OnCompleted.AddDynamic(this, &ThisClass::OnMontageCompleted);
	MontageTask->OnBlendOut.AddDynamic(this, &ThisClass::OnMontageBlendOut);
	MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::OnMontageInterrupted);
	MontageTask->OnCancelled.AddDynamic(this, &ThisClass::OnMontageCancelled);

	// 5. Task 활성화
	MontageTask->ReadyForActivation();


	// 6. AnimNotify에서 보내는 GameplayEvent 대기 — 서버 전용
	// (클라도 수신하면 PerformAttackTrace 이중 호출됨. OnAttackTraceEvent 내부 가드만으론 Task 자체가 낭비)
	if (bIsServer)
	{
		UAbilityTask_WaitGameplayEvent* EventTaskAttackTrace =
				UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
				                                                  this,
				                                                  VBGameplayTags::Event_Montage_AttackTrace,
				                                                  nullptr, // 자기 자신 대기
				                                                  false,			// 콤보의 각 섹션마다 AttackTrace 이벤트를 반복 수신해야 한다.
				                                                  true);

		EventTaskAttackTrace->EventReceived.AddDynamic(this, &ThisClass::OnAttackTraceEvent);
		EventTaskAttackTrace->ReadyForActivation();
	}

	if (ActiveConfig && ActiveConfig->MaxComboCount > 1)
	{
		UAbilityTask_WaitGameplayEvent* EventTaskWindowOpen =
				UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
				                                                  this,
				                                                  VBGameplayTags::Event_Combo_WindowOpen,
				                                                  nullptr, // 자기 자신 대기
				                                                  false,			// 콤보의 각 섹션마다 AttackTrace 이벤트를 반복 수신해야 한다.
				                                                  true);

		EventTaskWindowOpen->EventReceived.AddDynamic(this, &ThisClass::OnComboWindowOpen);
		EventTaskWindowOpen->ReadyForActivation();

		UAbilityTask_WaitGameplayEvent* EventTaskWindowClose =
				UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
				                                                  this,
				                                                  VBGameplayTags::Event_Combo_WindowClose,
				                                                  nullptr, // 자기 자신 대기
				                                                  false,			// 콤보의 각 섹션마다 AttackTrace 이벤트를 반복 수신해야 한다.
				                                                  true);

		EventTaskWindowClose->EventReceived.AddDynamic(this, &ThisClass::OnComboWindowClose);
		EventTaskWindowClose->ReadyForActivation();

		UAbilityTask_WaitGameplayEvent* EventTaskInputReceived =
				UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
				                                                  this,
				                                                  VBGameplayTags::Event_Combo_InputReceived,
				                                                  nullptr, // 자기 자신 대기
				                                                  false,			// 콤보의 각 섹션마다 AttackTrace 이벤트를 반복 수신해야 한다.
				                                                  true);

		EventTaskInputReceived->EventReceived.AddDynamic(this, &ThisClass::OnComboInputReceived);
		EventTaskInputReceived->ReadyForActivation();
	}


	VB_LOG(Log, "%s: 몽타주 재생 시작 (%s)", *GetName(), *ActiveMontage->GetName());
}


// AnimNotify에서 이벤트 수신 -> Trace 실행
void UVBGA_MeleeAttackBase::OnAttackTraceEvent(FGameplayEventData PayLoad)
{
	VB_LOG(Log, "%s: AttackTrace 이벤트 수신 -> Trace 실행", *GetName());

	// 이번 스윙의 임팩트 쉐이크 프로필을 캐시한다. 노티마다 덮어쓴다.
	// 왜 활성화 단위가 아니라 노티 단위인가: 한 몽타주 안의 2타가 1타 프로필을 물려받으면
	//  저작 입도가 스윙에서 공격으로 뭉개져, 노티에 프로필을 꽂은 의미가 사라진다.
	// null 을 그대로 받아들이는 것도 의도다 — 미저작 노티는 leaf 폴백을 타야 한다.
	ActiveSwingProfile = Cast<UVBSwingImpactProfile>(PayLoad.OptionalObject.Get());
	// 전투 수치 배율은 두 번째 슬롯에서 온다. 위와 같은 이유로 노티마다 덮어쓰고 null 도 그대로 받는다 -
	//  여기서 null 을 걸러 이전 값을 유지하면 저작 안 한 스윙이 직전 스윙의 무게를 물려받는다.
	ActiveSwingCombatProfile = Cast<UVBSwingCombatProfile>(PayLoad.OptionalObject2.Get());

	// 스타일 고유 타격(예: 마법 투사체)이 있으면 위임. 처리되면(true) 기본 SphereTrace 스킵.
	if (ActiveConfig && ActiveConfig->StyleBehavior && ActiveConfig->StyleBehavior->DeliverHit(this))
	{
		return;
	}
	PerformAttackTrace();
}

// 몽타주 정상 완료 -> GA 종료
void UVBGA_MeleeAttackBase::OnMontageCompleted()
{
	LogMontageMeshState(TEXT("Completed"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 완료 -> EndAbility", *GetName());
	ClearWarpTarget();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

// 몽타주 BlendOut 시작
void UVBGA_MeleeAttackBase::OnMontageBlendOut()
{
	// BlendOut 중에는 아직 GA가 살아있다.
	// 필요 시 여기서 "취소 가능" 상태 전환 (콤보 윈도우 등)
	LogMontageMeshState(TEXT("BlendOut"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 BlendOut", *GetName());
	// EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

// 다른 몽타주에 의해 중단
void UVBGA_MeleeAttackBase::OnMontageInterrupted()
{
	LogMontageMeshState(TEXT("Interrupted"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 중단됨 -> EndAbility(취소)", *GetName());
	ClearWarpTarget();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}


// GA 취소로 인한 중단
void UVBGA_MeleeAttackBase::OnMontageCancelled()
{
	LogMontageMeshState(TEXT("Cancelled"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 취소됨 -> EndAbility(취소)", *GetName());
	ClearWarpTarget();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
}

// 공격 종료 — 뚝심 로스태그 제거(부여했던 경우만).
// 수렴 경로: OnMontageCompleted / OnMontageInterrupted / OnMontageCancelled 3곳이 EndAbility 를 호출한다.
// (OnMontageBlendOut 은 EndAbility 를 호출하지 않는다 — 해당 줄은 주석 처리됨. BlendOut 뒤에 Completed 가
//  이어지므로 태그 누수는 없다. 과거 주석이 "4경로"라 적었으나 사실이 아니었다 — 2026-07-16 /verify 지적으로 정정.)
// GA 가 어떤 경로로 끝나든 엔진이 EndAbility 를 보장하므로 이 단일 지점 제거로 충분하다.
void UVBGA_MeleeAttackBase::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                                       const FGameplayAbilityActorInfo*     ActorInfo,
                                       const FGameplayAbilityActivationInfo ActivationInfo,
                                       bool                                 bReplicateEndAbility,
                                       bool                                 bWasCancelled)
{
	if (bGrantedSuperArmor)
	{
		if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
		{
			OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_SuperArmor);
		}
		bGrantedSuperArmor = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// 이 공격이 겨누는 대상. 하드락 우선, 없으면 넷-세이프 소프트 런지 스캔 폴백.
// 카메라를 쓰지 않으므로 서버·클라 결정론이 유지된다.
const USceneComponent* UVBGA_MeleeAttackBase::ResolveAimTargetComponent() const
{
	const AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo());
	if (!VBChar) { return nullptr; }   // 적 아바타는 AVBCharacter 아님 - 구조적 제외

	const UVBTargetLockComponent* TLC = VBChar->GetTargetLockComponent();
	if (!TLC) { return nullptr; }

	if (AActor* LockedTarget = TLC->GetLockedTarget(); IsValid(LockedTarget))
	{
		// 죽은 락 타겟이면 겨눌 대상이 없다(현행 가드 보존).
		if (const IVBTargetable* TargetBase = Cast<IVBTargetable>(LockedTarget))
		{
			if (!TargetBase->IsTargetable()) { return nullptr; }
		}
		return LockedTarget->GetRootComponent();
	}

	// 락온 없음 -> 액터 forward 소프트 런지 스캔. 소프트 타겟은 transient 라
	// 하드락 semantics(카메라/HUD)를 바꾸지 않는다.
	if (AActor* SoftTarget = TLC->FindSoftLungeTarget(); IsValid(SoftTarget))
	{
		return SoftTarget->GetRootComponent();
	}
	return nullptr;
}

void UVBGA_MeleeAttackBase::SetWarpTargetLockedEnemy()
{
	AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter());
	if (!VBChar) return; // 적 아바타는 AVBCharacter 아님 → 소프트 런지/워프 대상에서 구조적 제외

	UMotionWarpingComponent* MotionWarpingComponent = VBChar->GetMotionWarpingComponent();
	if (!MotionWarpingComponent) return;

	const UVBTargetLockComponent* TargetLockComponent = VBChar->GetTargetLockComponent();
	if (!TargetLockComponent) return; // 락 컴포넌트 없는 폰(적) 제외

	// 타겟 해석은 판정과 공유한다(ResolveAimTargetComponent) — 둘이 갈리면
	//   "워프는 저쪽으로 붙는데 판정은 정면으로 나가는" 어긋남이 생긴다.
	const USceneComponent* ResolvedComponent = ResolveAimTargetComponent();

	// ── write 단일화(락·소프트 공유) ──
	if (ResolvedComponent)
	{
		ApplyApproachWarpToTarget(ResolvedComponent);
	}
	else
	{
		MotionWarpingComponent->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget); // 현행 no-op 동일
	}
}

void UVBGA_MeleeAttackBase::ApplyApproachWarpToTarget(const USceneComponent* TargetComponent)
{
	// 락 경로와 소프트 런지 경로가 공유하는 워프 목적지 write(DRY).
	// 회귀 불변식: 아래 수치 거동은 리팩터 전 SetWarpTargetLockedEnemy 락 경로와 동일해야 한다.
	AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter());
	if (!VBChar || !TargetComponent) return;

	UMotionWarpingComponent* MotionWarpingComponent = VBChar->GetMotionWarpingComponent();
	if (!MotionWarpingComponent) return;

	// (B) 접근 간격: 워프 목적지를 적 정중앙에서 공격자 쪽으로 WarpApproachOffset 만큼 당겨 무기 리치에서 멈춘다.
	// 오프셋을 [0, AttackRange]로 clamp — 트레이스가 닿는 범위를 벗어나 헛치는 것을 방지.
	// 상한이 AttackRange 인 이유: 스윕은 Start 에서 Start+Forward*AttackRange 까지 반경 AttackRadius
	// 구체를 쓸므로, 적 중심이 정확히 AttackRange 지점에 있어도 끝점 구체가 덮는다. 적 캡슐 반경까지
	// 더하면 여유는 더 커진다. 종전 상한 AttackRange-AttackRadius 는 필요보다 AttackRadius 만큼 더
	// 당긴 값이었고, 그 과보수성이 리치가 가장 짧은 무기에서만 실제로 물렸다 — Fighter 는 140-50=90 이
	// 곧 상한이라 값을 올려도 반영되지 않아 적과 거의 맞닿은 채 싸웠다(2026-08-03 user FEEL).
	// 카타나(120<150)·대검(120<190)·반격 2종은 종전에도 상한 아래라 이 변경에 거동이 바뀌지 않는다.
	// (ActiveConfig 는 ExecuteAbility 에서 해석됨. 두 호출자 ExecuteAbility/OnComboWindowClose 모두 이 경로 경유.)
	float ApproachOffset = 0.0f;
	if (ActiveConfig)
	{
		ApproachOffset = FMath::Clamp(ActiveConfig->WarpApproachOffset, 0.0f,
		                              FMath::Max(0.0f, ActiveConfig->AttackRange));

		// 런타임 거리 clamp (BUG-007): 오프셋이 현재 플레이어-적 거리를 넘으면 워프 타겟이 플레이어 '뒤'에 생긴다.
		// 그러면 SkewWarp 의 Facing 회전(bWarpRotation, bWarpTranslation 과 무관하게 항상 동작)이 적 반대 방향을
		// 가리켜 캐릭터가 뒤로 돌아서고, 전진 루트모션이 '뒤로 돌진'하는 간헐 버그가 난다(빠른 접근/콤보 중 거리<오프셋 순간).
		// MinStandoff 여유를 빼 타겟이 항상 플레이어보다 살짝 적 쪽에 있게 → ToSyncPoint 가 항상 적을 향한다(degenerate 회피).
		const float DistToEnemy = FVector::Dist2D(VBChar->GetActorLocation(), TargetComponent->GetComponentLocation());
		ApproachOffset = FMath::Min(ApproachOffset, FMath::Max(0.0f, DistToEnemy - ActiveConfig->MinWarpStandoff));

		// 하한: 두 캡슐 반경 합보다 가까운 지점을 목표로 삼으면 안 된다.
		// 위 clamp 는 BUG-007 만 보고 하한이 없어, 이미 붙어 있는 상태에서 스윙하면 목표가
		// DistToEnemy-MinWarpStandoff 까지 내려간다. MinWarpStandoff(20)는 캡슐 합(34+34=68)보다
		// 훨씬 작아 목표가 캡슐 안쪽에 생기고, CMC 가 매 프레임 밀어내며 위치가 떨린다.
		// 그 떨림이 캡슐에 붙은 스프링암을 흔들어 "카메라가 가끔 흔들린다"로 보인다(user 2026-08-03).
		// 반경은 런타임에서 읽는다 - 적 종류마다 다르고 스케일도 걸릴 수 있어 상수로 굳히면 어긋난다.
		float MinSeparation = 0.0f;
		if (const UCapsuleComponent* SelfCap = VBChar->GetCapsuleComponent())
		{
			MinSeparation += SelfCap->GetScaledCapsuleRadius();
		}
		if (const ACharacter* TargetChar = Cast<ACharacter>(TargetComponent->GetOwner()))
		{
			if (const UCapsuleComponent* TargetCap = TargetChar->GetCapsuleComponent())
			{
				MinSeparation += TargetCap->GetScaledCapsuleRadius();
			}
		}
		// DistToEnemy 와 min 을 거는 이유: 이미 캡슐이 겹친 상태라면 하한을 그대로 적용했다가는
		// 오프셋이 현재 거리보다 커져 워프 타겟이 플레이어 뒤로 가고 BUG-007 이 되살아난다.
		// 그 경우엔 현재 거리를 그대로 써 전진량 0 으로 둔다 - 밀어내는 건 넉백의 몫이다.
		if (MinSeparation > 0.0f)
		{
			ApproachOffset = FMath::Max(ApproachOffset, FMath::Min(MinSeparation, DistToEnemy));
		}
	}

	// VectorFromTargetToOwner: LocationOffset.X 를 (적→공격자) 벡터 방향으로 이동 → 적 중심 앞 ApproachOffset cm.
	// bFollowComponent=true 유지 → 움직이는 적을 매 프레임 추종(정적 스냅샷 없음). RotationOffset=Zero → 회전/발밑 거동 현행 동일.
	// 전제: 몽타주 SkewWarp bWarpTranslation=1 이어야 이 위치 이동이 실제 착지에 반영된다. 0이면 무효(FIND-035).
	MotionWarpingComponent->AddOrUpdateWarpTargetFromComponent(
	                                                           VBMotionWarpingNames::AttackTarget,
	                                                           TargetComponent,
	                                                           FName(NAME_None),
	                                                           true,
	                                                           EWarpTargetLocationOffsetDirection::VectorFromTargetToOwner,
	                                                           FVector(ApproachOffset, 0.0f, 0.0f),
	                                                           FRotator::ZeroRotator);
}

void UVBGA_MeleeAttackBase::ClearWarpTarget()
{
	if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter()))
	{
		if (UMotionWarpingComponent* MotionWarpingComponent = VBChar->GetMotionWarpingComponent())
			MotionWarpingComponent->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	}
}

void UVBGA_MeleeAttackBase::OnComboWindowOpen(FGameplayEventData Payload)
{
	bComboWindowOpen = true;
}

void UVBGA_MeleeAttackBase::OnComboWindowClose(FGameplayEventData Payload)
{
	// 설계: window 끝나는 시점(= 현재 section 후반부)에 저장된 입력을 소비해 다음 section으로 전환.
	// 원래 설계 타이밍 — 콤보 리듬감 유지.
	bComboWindowOpen = false;

	if (!bSaveAttack) return;
	if (!ActiveConfig || ComboIndex >= ActiveConfig->MaxComboCount - 1) return;

	SetWarpTargetLockedEnemy();
	// 다음 콤보 섹션: ActiveConfig->ComboSectionNames 배열 우선(무기별 섹션명 대응),
	// 미설정/범위초과 시 레거시 "Swing%d" 규칙으로 폴백(회귀 안전).
	FName NextSection;
	const int32 NextSectionIdx = ComboIndex + 1;
	if (ActiveConfig && ActiveConfig->ComboSectionNames.IsValidIndex(NextSectionIdx))
	{
		NextSection = ActiveConfig->ComboSectionNames[NextSectionIdx];
	}
	else
	{
		NextSection = FName(*FString::Printf(TEXT("Swing%d"), ComboIndex + 2));
	}

	// 로컬 즉시 전환 (ASC 경로 — AnimInstance 직접 호출은 ASC tracking 괴리 → OnInterrupted → EndAbility 위험)
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->CurrentMontageJumpToSection(NextSection);
	}

	// Sim proxy 강제 동기화는 서버에서만 Multicast (RepAnimMontageInfo Position 기반 복제가 3+ 연속 jump에 불안정)
	if (CurrentActorInfo && CurrentActorInfo->IsNetAuthority())
	{
		if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter()))
		{
			VBChar->MulticastJumpToMontageSection(ActiveMontage, NextSection);
		}
	}

	ComboIndex++;
	bSaveAttack = false;
}

void UVBGA_MeleeAttackBase::OnComboInputReceived(FGameplayEventData Payload)
{
	// 입력을 저장만 함. 실제 section 전환은 OnComboWindowClose 시점에 일괄 실행.
	// window 중 여러 번 눌러도 한 번만 저장 (idempotent).
	if (bComboWindowOpen)
	{
		bSaveAttack = true;
	}
}


void UVBGA_MeleeAttackBase::ApplyAdditionalEffects(UAbilitySystemComponent* TargetASC, const FHitResult& Hit)
{
	// 데이터 기반 추가 효과: ActiveConfig->OnHitExtraEffects의 GE를 대상에 적용(출혈/스태거 등).
	// 서버 권위 경로(PerformAttackTrace)에서만 호출되므로 권위 가드 별도 불필요.
	if (!TargetASC || !ActiveConfig)
	{
		return;
	}
	for (const TSubclassOf<UGameplayEffect>& EffectClass : ActiveConfig->OnHitExtraEffects)
	{
		if (!EffectClass)
		{
			continue;
		}

		// 한도 판정은 적용 '전'에 한다 - 오버플로가 "한도에서 한 번 더 맞을 때" 터지던 원 설계와 같은 시점이다.
		// 엔진 OverflowEffects 를 쓰지 않는 이유: UE 5.8 이 GE 최초 생성 경로에서도 오버플로 핸들러를
		//  호출하는데(GameplayEffect.cpp - HandleActiveGameplayEffectStackOverflow 호출 지점, 5.8 은 2곳) 그 핸들러의 OverflowEffects 루프가 스택 한도 도달 여부를
		//  보지 않아 첫 타격부터 터진다. 판정을 여기로 가져와 엔진 판본에 의존하지 않게 했다.
		// 임계값의 홈은 GE 자신의 StackLimitCount 다 - 자산이 한도를 바꾸면 코드 수정 없이 따라온다.
		const UGameplayEffect* EffectCDO = EffectClass->GetDefaultObject<UGameplayEffect>();
		const int32 StackLimit = EffectCDO ? EffectCDO->StackLimitCount : 0;

		if (StackLimit > 0)
		{
			// 판정과 제거가 같은 스택을 가리켜야 한다 - 아래 제거는 우리 ASC 가 건 것만 지우므로
			// 세는 쪽도 같은 필터를 쓴다. nullptr(전체 합산)로 세면 공격자가 둘일 때 합이 먼저 한도를
			// 넘어 남의 스택 때문에 내 첫 타가 터지고, 제거는 내 것만 지우므로 남의 스택이 남아
			// 이후 모든 타격이 폭발한다(GE 는 AGGREGATE_BY_SOURCE 라 스택 자체가 소스별이다).
			UAbilitySystemComponent* SourceASC = GetAbilitySystemComponentFromActorInfo();
			const int32 CurrentStacks = TargetASC->GetGameplayEffectCount(EffectClass, SourceASC);
			if (CurrentStacks >= StackLimit)
			{
				// 한도 도달: 터뜨리고 스택을 비운다. 이번 적용분은 소비된 것으로 보고 다시 쌓지 않는다.
				ApplyStackFullEffects(TargetASC, EffectClass);
				TargetASC->RemoveActiveGameplayEffectBySourceEffect(EffectClass, SourceASC);
				continue;
			}
		}

		FGameplayEffectSpecHandle ExtraSpec = MakeOutgoingGameplayEffectSpec(EffectClass, GetAbilityLevel());
		if (ExtraSpec.IsValid())
		{
			TargetASC->ApplyGameplayEffectSpecToSelf(*ExtraSpec.Data.Get());
		}
	}
}

// 스택 한도 도달 시의 폭발. 어느 스택형 효과의 짝인지는 데이터가 정한다 -
//  미저작이면 아무 일도 없이 넘어간다(출혈을 안 쓰는 무기가 정상 경로다).
void UVBGA_MeleeAttackBase::ApplyStackFullEffects(UAbilitySystemComponent* TargetASC,
                                                  TSubclassOf<UGameplayEffect> StackingEffect)
{
	if (!TargetASC || !ActiveConfig || !StackingEffect)
	{
		return;
	}

	for (const FVBStackFullEffect& Entry : ActiveConfig->StackFullEffects)
	{
		if (Entry.StackingEffect != StackingEffect)
		{
			continue;
		}

		for (const TSubclassOf<UGameplayEffect>& BurstClass : Entry.BurstEffects)
		{
			if (!BurstClass)
			{
				continue;
			}
			FGameplayEffectSpecHandle BurstSpec = MakeOutgoingGameplayEffectSpec(BurstClass, GetAbilityLevel());
			if (BurstSpec.IsValid())
			{
				TargetASC->ApplyGameplayEffectSpecToSelf(*BurstSpec.Data.Get());
			}
		}

		VB_LOG(Log, "스택 한도 폭발: %s 한도 도달 -> %d종 적용",
		       *StackingEffect->GetName(), Entry.BurstEffects.Num());
		return;
	}
}

// 임팩트 쉐이크 프로필 해석: 노티(스윙 입도) -> 공격 leaf(무기 입도).
const UVBSwingImpactProfile* UVBGA_MeleeAttackBase::ResolveSwingImpactProfile() const
{
	// 1순위: 직전 트레이스 노티가 실어 보낸 프로필. 저작 입도가 스윙이라 콤보 타마다 방향이 달라질 수 있다.
	if (ActiveSwingProfile)
	{
		return ActiveSwingProfile;
	}

	// 2순위: 무기 leaf 의 기본 프로필. 입도가 공격당 1개라 콤보 전 타가 같은 방향으로 굳는다 — 그래서 폴백이다.
	if (ActiveConfig)
	{
		return ActiveConfig->DefaultSwingImpactProfile;
	}

	// 3단째 기본값을 두지 않는다. 여기서 정면 같은 값을 돌려주면 방향이 데이터가 아니라 코드가 되고(규칙 26),
	//  동시에 폴백 층이 하나 더 늘어 층마다 값이 갈라질 자리가 생긴다.
	// 미저작의 올바른 결과는 '아무것도 하지 않음'이다 — 부재는 값이 아니다.
	return nullptr;
}

FVBSwingCombatScales UVBGA_MeleeAttackBase::ResolveSwingCombatScales() const
{
	// 노티가 프로필을 실어 보냈으면 그 배율을 쓴다.
	if (ActiveSwingCombatProfile)
	{
		return ActiveSwingCombatProfile->Scales;
	}

	// 아니면 곱하지 않는다. 기본 생성값이 전부 1.0 이고 x * 1.0f 는 비트 동일이라,
	//  저작하지 않은 스윙은 leaf 절대값을 그대로 받는다 - 이 함수가 도입되기 전과 같은 값이다.
	// 여기서 "기본 프로필"을 조회하지 않는 것이 요점이다. 조회할 곳을 만드는 순간 같은 배율이
	//  두 곳에서 결정될 수 있고, 그게 폴백 층을 늘리지 않는다는 규칙의 실질이다.
	return FVBSwingCombatScales();
}

void UVBGA_MeleeAttackBase::PerformAttackTrace()
{
	// 서버 권위 함수: Trace + GE(Damage) 적용.
	// ServerInitiated로 전환된 이상 클라에서도 호출될 가능성이 있으므로 입구에서 차단.
	// (ExecuteAbility와 OnAttackTraceEvent 쪽 호출자 가드 이중화 — 미래 새 호출자 추가 시에도 안전)
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return;
	}

	ACharacter* AvatarCharacter = GetAvatarCharacter();
	if (!AvatarCharacter || !ActiveConfig || !ActiveConfig->DamageEffectClass)
	{
		return;
	}

	// Trace 시작/끝 위치
	const FVector Start = AvatarCharacter->GetActorLocation();

	// 조준 보정: 겨눈 대상이 AimAssistMaxAngle 안에 있으면 판정을 그쪽으로 돌린다.
	// 왜: 트레이스는 정면 일직선 스윕이라 캡슐이 살짝 어긋나면 그대로 빗나간다.
	//   캡슐은 락온 strafe 로 컨트롤 회전(카메라)을 따라가므로 카메라가 조금만 어긋나도 스쳐 지나간다.
	// 각도 밖이면 정면 그대로 - 등 뒤나 옆의 적이 갑자기 맞는 일은 없다.
	FVector Forward = AvatarCharacter->GetActorForwardVector();
	Forward.Z = 0.0f;
	Forward.Normalize();
	if (ActiveConfig->AimAssistMaxAngle > 0.0f)
	{
		if (const USceneComponent* AimTarget = ResolveAimTargetComponent())
		{
			FVector ToTarget = AimTarget->GetComponentLocation() - Start;
			ToTarget.Z = 0.0f;
			if (!ToTarget.IsNearlyZero())
			{
				ToTarget.Normalize();
				const float AngleDeg = FMath::RadiansToDegrees(
					FMath::Acos(FMath::Clamp(FVector::DotProduct(Forward, ToTarget), -1.0, 1.0)));
				if (AngleDeg <= ActiveConfig->AimAssistMaxAngle)
				{
					Forward = ToTarget;
				}
			}
		}
	}
	const FVector End = Start + (Forward * ActiveConfig->AttackRange);

	// 넉백이 밀 방향. 이 스윙이 실제로 나간 방향과 같아야 한다.
	// 왜 여기서 한 번만 구하나: 대상 루프 안에서 대상마다 구하면 다중 타격에서 적들이 방사형으로
	//   흩어져 스윙 한 번의 그림이 깨진다. 한 번 휘두른 것이면 모두 같은 쪽으로 밀려야 한다.
	// 우선순위: 스윙 프로필(저작된 방향) -> 조준 보정된 Forward(판정이 실제로 간 방향).
	//   생 GetActorForwardVector 는 쓰지 않는다 - 조준 보정이 판정을 최대 AimAssistMaxAngle 만큼
	//   꺾어 놓는데 넉백만 안 꺾인 정면으로 밀면 그만큼 어긋난다(2026-08-06 user 실측 신고).
	//   공격 경로에 방향이 셋(판정/넉백/쉐이크) 있었고 서로 달랐던 것이 원인이다.
	// 넉백은 항상 판정이 나간 방향으로 민다. 스윙 축(SwingImpactProfile)을 쓰지 않는다 - 의도적이다.
	// 한때 스윙 축을 공유하게 했다가 되돌렸다. 가로베기가 적을 옆으로 날리면 적이 공격선에서 벗어나
	//   다음 스윙의 접근 워프가 엉뚱한 쪽을 향하고 간격이 흔들린다 - 위치선 넉백을 폐기했던 것과 같은 이유다.
	//   무브별로 갈려야 하는 것은 방향 벡터가 아니라 반응 종류다(런처는 위로, 내려찍기는 아래로).
	//   그 상하 성분은 KnockbackZ 가 이미 따로 담당하므로 여기서 정할 것은 수평 방향 하나뿐이다.
	// 스윙 축은 카메라 쉐이크 전용으로 남는다 - 그쪽은 순수 연출이라 적의 위치를 흔들지 않는다.
	// 생 GetActorForwardVector 가 아니라 Forward 인 것이 요점이다. 조준 보정이 판정을 최대
	//   AimAssistMaxAngle 만큼 꺾어 놓는데 넉백만 안 꺾이면 그만큼 어긋난다.
	FVector KnockbackDir = Forward;
	KnockbackDir.Z = 0.0f;
	const float KnockbackYaw = KnockbackDir.IsNearlyZero() ? 0.0f : KnockbackDir.Rotation().Yaw;

	// SphereTrace 설정
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(AvatarCharacter);

	TArray<FHitResult> HitResults;
	const bool         bHit = AvatarCharacter->GetWorld()->SweepMultiByChannel(
	                                                                   HitResults,
	                                                                   Start,
	                                                                   End,
	                                                                   FQuat::Identity,
	                                                                   ActiveConfig->HitTraceChannel,
	                                                                   // 반경도 ActiveConfig 기준 (레거시 AttackConfig 직접 참조 시 Moveset-only 할당에서 null deref + 반경 불일치)
	                                                                   FCollisionShape::MakeSphere(ActiveConfig->AttackRadius),
	                                                                   QueryParams);


#if ENABLE_DRAW_DEBUG
	// 스윕 전체를 그린다. 종전에는 End 한 점만 그려서 "스피어가 적에게 안 겹친다"로 보였다
	// (2026-07-31 user 오독 유발). 실제 판정은 Start->End 구간의 스윕이고 적은 그 중간에서 맞는다.
	// 캡슐 = 스윕 볼륨 그 자체 / 양 끝 구 = 시작·종료 지점 / 명중 지점 = 실제로 맞은 자리.
	{
		const FVector  Mid  = (Start + End) * 0.5f;
		const float    Half = FMath::Max((End - Start).Size() * 0.5f, KINDA_SMALL_NUMBER);
		const FQuat    Rot  = FRotationMatrix::MakeFromZ(Forward).ToQuat();
		const FColor   Col  = bHit ? FColor::Red : FColor::Green;
		DrawDebugCapsule(AvatarCharacter->GetWorld(), Mid, Half, ActiveConfig->AttackRadius, Rot, Col, false, 1.0f);
		DrawDebugSphere(AvatarCharacter->GetWorld(), Start, ActiveConfig->AttackRadius, 12, Col, false, 1.0f);
		DrawDebugSphere(AvatarCharacter->GetWorld(), End,   ActiveConfig->AttackRadius, 12, Col, false, 1.0f);
		for (const FHitResult& H : HitResults)
		{
			if (H.GetActor())
			{
				DrawDebugSphere(AvatarCharacter->GetWorld(), H.ImpactPoint, 12.0f, 8, FColor::Yellow, false, 1.0f);
			}
		}
	}
#endif


	if (!bHit)
	{
		return;
	}


	VB_LOG(Log, "공격 Trace: Hit %d actors", HitResults.Num());

	if (HitResults.Num() == 0)
	{
		return;
	}

	// 1. AttackPower 가져오기
	UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC) return;

	float AttackPower = 0.0f;

	AttackPower = OwnerASC->GetNumericAttribute(UVBCombatAttributeSet::GetAttackPowerAttribute());


	// 이번 스윙의 전투 수치 배율. 대상 루프 진입 전에 한 번만 해석한다 - 넉백 방향을 루프 밖에서
	//  한 번만 구하는 것과 같은 이유다. 스윙 한 번의 무게는 맞은 대상 수와 무관하게 하나여야 한다.
	const FVBSwingCombatScales SwingScales = ResolveSwingCombatScales();

	// 2. 최종 데미지 계산
	// 변형 배율을 곱한다. 타격 수가 다른 변형끼리 총 데미지를 맞추기 위한 것이다(DamageScale 주석 참조).
	// 스윙 배율이 마지막 층이다 - 캐릭터/무기/변형/스윙 네 층은 입도가 각각 달라서 나뉘어 있다.
	const float FinalDamage =
		AttackPower * ActiveConfig->DamageMultiplier * ActiveVariantDamage * SwingScales.DamageScale;

	TSet<AActor*> ProcessedActors;
	bool bAutoLockedThisSwing = false; // 스윙당 자동 락온 1회 가드(첫 유효 데미지 타겟이 시도권)

	// 3. 맞은 대상마다 Spec/Context/GE/큐를 개별 처리
	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor)
		{
			continue;
		}

		// 이미 데미지를 준 액터는 스킵 (스윙당 1회 보존)
		if (ProcessedActors.Contains(HitActor))
		{
			continue;
		}

		// 같은 편은 때리지 않는다(FIND-090). 밀집 전투에서만 발현하던 결함이라 에워싸기보다 먼저 막는다.
		// 판정의 홈은 UVBAbilitySystemStatics 하나다 - 공격 경로가 늘어도 여기 한 곳만 부른다.
		// ASC 를 필요로 하지 않는 판정이고 밀집 상황에서 가장 많은 대상을 걸러 내므로 스펙 생성 전에 떨어뜨린다.
		// ProcessedActors 에는 등록하지 않는다 - '만남'과 '데미지 줌'의 분리(FIND-041)를 그대로 지킨다.
		if (UVBAbilitySystemStatics::IsFriendlyFire(AvatarCharacter, HitActor))
		{
			continue;
		}

		// 대상의 ASC 가져오기
		UAbilitySystemComponent* TargetASC =
				UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);

		if (!TargetASC)
		{
			continue;
		}
		if (UVBAbilitySystemStatics::IsDead(TargetASC)) continue;
		if (!Hit.bBlockingHit) continue;

		// ProcessedActors 등록은 반드시 여기 — 모든 skip-gate 통과 후, 데미지 확정 직전 (FIND-041)
		// 왜: 이 집합의 의미는 "이번 스윙에서 데미지를 준 액터"다. 한 스윕이 한 액터에 대해
		//     여러 HitResult(비관통 overlap + 관통 blocking, 순서 무관)를 낼 수 있는데, 등록을
		//     루프 진입 즉시(Contains 검사 직후) 하면 비관통 히트가 먼저 온 경우 그 히트가
		//     bBlockingHit 게이트에서 스킵되면서도 액터를 '소비'해, 뒤따르는 진짜 관통 히트가
		//     Contains 에서 통째로 탈락한다(= 그 액터 데미지 0). '만남'과 '데미지 줌'을 분리해 해소.
		ProcessedActors.Add(HitActor);

		// 대상마다 새 Spec/Context 를 만든다 (구 코드는 루프 밖에서 1개를 만들어 공유했다)
		// 왜: FGameplayEffectContext::AddHitResult 는 check(!HitResult.IsValid()) 를 건다
		//     (엔진 GameplayEffectTypes.cpp::FGameplayEffectContext::AddHitResult, bReset 기본값 false).
		//     공유 Context 에 두 번째 AddHitResult 를 호출하면 Development/PIE 에서 assert 로 죽는다
		//     → 폰 2체가 같은 스윕에 잡히면 크래시. Shipping 은 check 가 컴파일 아웃되어
		//       마지막 HitResult 로 덮어써지고, 그 결과 큐가 1명분만 발동했다(광역 시 1명만 반응).
		// 부수효과: 대상 수만큼 Spec 생성 — 스윕에 잡히는 액터 수 한정이라 비용은 무시 가능.
		FGameplayEffectSpecHandle SpecHandle =
				MakeOutgoingGameplayEffectSpec(ActiveConfig->DamageEffectClass, GetAbilityLevel());
		if (!SpecHandle.IsValid())
		{
			continue;
		}

		// HitResult를 GE Context에 추가해야 GCN이 피격자를 역추출할 수 있다.
		FGameplayEffectContextHandle ContextHandle = SpecHandle.Data.Get()->GetContext();
		ContextHandle.AddHitResult(Hit);

		// 대상 ASC에 직접 적용
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		                                               VBGameplayTags::Effect_Damage_Physical, FinalDamage);
		// (A) 넉백 세기를 같은 데미지 스펙에 동봉 — 공격자 스탠스값이 피격자 hub(PostGameplayEffectExecute)로 전달된다.
		//     이벤트 스키마 변경 없이 데미지를 이미 나르는 권위 객체를 재사용. 0(현행)이면 피격자측 가드가 스킵.
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		                                               VBGameplayTags::Data_KnockbackStrength,
		                                               ActiveConfig->KnockbackStrength * SwingScales.KnockbackStrengthScale);
		//     (B) 상방 성분도 같은 스펙에 동봉. Z 가 0 이면 캐릭터가 실제로 안 떠서
		//     다음 프레임에 Walking 으로 복귀하고 지상 마찰이 수평 속도를 지운다(KnockbackZ 주석 참조).
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		                                               VBGameplayTags::Data_KnockbackZ,
		                                               ActiveConfig->KnockbackZ * SwingScales.KnockbackZScale);
		//     (C) 미는 방향(월드 yaw). 피격자 hub 는 공격자의 회전만 알 수 있어서 조준 보정과
		//     스윙 축을 볼 수 없다 - 방향을 여기서 확정해 실어 보내야 판정과 넉백이 같은 방향이 된다.
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		                                               VBGameplayTags::Data_KnockbackYaw, KnockbackYaw);
		//     (D) 경직(Poise) 데미지. 넉백과 같은 스펙에 실어 보낸다 - 이벤트 스키마를 늘리지 않고
		//     이미 데미지를 나르는 권위 객체를 재사용한다. 0 이면 받는 쪽(UVBCombatAttributeSet)이
		//     파이프라인을 타지 않아 현행 동작이 그대로 보존된다.
		//     스윙 배율을 곱하지 않는 것은 의도다 - "이 무브가 얼마나 무거운가"는 무기 leaf 의 성질이고,
		//     스윙 단위 보정(FVBSwingCombatScales)에는 poise 축이 없다. 없는 축을 지어내면 홈이 둘이 된다.
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		                                               VBGameplayTags::Data_PoiseDamage, ActiveConfig->PoiseDamage);
		TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

		ApplyAdditionalEffects(TargetASC, Hit);

		// 자동 락온: 이 스윙이 처음 유효 데미지를 준 적으로 락 시도(서버 권위).
		// 적 아바타는 AVBCharacter 가 아니므로 Cast 실패 → 구조적 제외. 컴포넌트 getter 도 null-safe.
		// 시도 후(수락/거절 무관) 잠금 → 뒤따르는 다중 히트 타겟이 락을 훔치지 않게(결정론적 '첫 타겟').
		if (!bAutoLockedThisSwing)
		{
			if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter()))
			{
				if (UVBTargetLockComponent* TLC = VBChar->GetTargetLockComponent())
				{
					TLC->TryAutoLockOnTarget(HitActor);
				}
			}
			bAutoLockedThisSwing = true;
		}

		// 큐는 루프 '안'에서 대상마다 실행한다 (구 코드는 루프 밖 1회 → 광역 시 1명만 반응)
		// 실행 주체는 여전히 공격자 ASC 라 멀티캐스트 경로는 불변 — 대상별 Context 만 달라진다.
		FGameplayCueParameters CueParams;
		CueParams.EffectContext = ContextHandle;
		OwnerASC->ExecuteGameplayCue(ActiveConfig->GameplayCueTag, CueParams);
	}

	if (ProcessedActors.Num() > 0)
	{
		// 곱셈 사슬을 통째로 남긴다. 이 줄이 없어서 "데미지가 세진 것 같다"를 정적 감사로만 쫓아야 했고,
		//  네 층 중 어디가 움직였는지 알 수 없었다(2026-08-10). 최종 수치는 게임에서 제일 중요한 숫자인데
		//  관측 지점이 하나도 없던 것이 결함이다. 적중한 스윙에서만 나오므로 헛스윙 노이즈는 없다.
		VB_LOG(Log, "%s: 데미지 %.1f = AttackPower %.1f x cfg %.2f x variant %.2f x swing %.2f (대상 %d)",
		       *GetName(), FinalDamage, AttackPower, ActiveConfig->DamageMultiplier,
		       ActiveVariantDamage, SwingScales.DamageScale, ProcessedActors.Num());

		// 저작한 스윙에서만 1줄. 미저작이 정상 상태이므로 매 스윙 로그는 노이즈다.
		//  꽂았는데 이 줄이 안 뜨면 배선이 안 된 것 - 그것이 이 로그의 유일한 목적이다.
		if (ActiveSwingCombatProfile)
		{
			VB_LOG(Log, "%s: 스윙 배율 적용 profile=%s dmg=%.2f kbS=%.2f kbZ=%.2f hitstop=%.2f",
			       *GetName(), *ActiveSwingCombatProfile->GetName(),
			       SwingScales.DamageScale, SwingScales.KnockbackStrengthScale,
			       SwingScales.KnockbackZScale, SwingScales.HitStopDurationScale);
		}

		// 히트스톱은 전역 시간 감속이라 대상 수와 무관하게 1회만 — 루프 밖 유지.
		PlayHitStop(ActiveConfig->HitStopDuration * SwingScales.HitStopDurationScale,
		            ActiveConfig->HitStopTimeDilation);

		// 방향성 임팩트 쉐이크도 같은 이유로 루프 밖 1회다.
		// 파티클과 사운드는 맞은 대상마다 나야 옳지만 카메라는 대상 수와 무관하게 하나뿐이다.
		//  루프 안에서 쏘면 적 2체를 동시에 벨 때 쉐이크가 두 번 겹쳐 세기가 두 배가 된다
		//  — 지금 GCN_DamagePhysical BP 의 쉐이크 노드가 정확히 그 상태이고, 이 위치가 그것을 구조적으로 없앤다.
		if (const UVBSwingImpactProfile* Profile = ResolveSwingImpactProfile())
		{
			// 왜 위에서 만든 Forward 를 쓰지 않는가: Forward 는 AimAssist 로 꺾인 판정 방향이지 스윙 방향이 아니다.
			//  조준 보정이 내려베기를 옆으로 바꿔 버리면 안 된다. 밀림 방향의 근거는 아바타의 자세 하나뿐이다.
			const FVector WorldSwingDir = AvatarCharacter->GetActorQuat().RotateVector(Profile->GetNormalizedAxis());

			// Normal 이 영벡터면 복제 시 생략되고, 수신측 Rotation() 이 ZeroRotator(= 월드 +X)가 되어
			//  캐릭터가 어디를 보든 항상 월드 +X 로 밀린다. 영벡터는 여기서 끊는다.
			if (!WorldSwingDir.IsNearlyZero())
			{
				FGameplayCueParameters SwingCueParams;
				SwingCueParams.Normal       = WorldSwingDir;   // 복제되는 단위벡터. 서버 산출이라 전 클라가 같은 값을 본다
				SwingCueParams.Instigator   = AvatarCharacter; // GCN 의 로컬 플레이어 게이트 판정 근거
				SwingCueParams.SourceObject = Profile;         // 쉐이크 클래스와 세기의 데이터 홈을 통째로 전달
				OwnerASC->ExecuteGameplayCue(VBGameplayTags::GameplayCue_Combat_SwingImpact, SwingCueParams);
			}
		}
		else
		{
			// Verbose 가 아니라 Warning 인 이유: BP 의 쉐이크 노드를 제거했으므로 미저작은 정상 상태가 아니라
			//  "쉐이크가 통째로 사라진" 설정 오류다. 노티와 leaf 어느 쪽에도 프로필이 없다는 뜻이다.
			VB_LOG(Warning, "%s: SwingImpactProfile 미할당(노티/leaf 모두) - 임팩트 쉐이크 스킵", *GetName());
		}
	}
}
