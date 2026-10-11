// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h" // EWarpTargetLocationOffsetDirection, URootMotionModifier_Warp::WarpTargetName
#include "AnimNotifyState_MotionWarping.h" // MeasureMontageLungeCm - 접근 워프 창의 끝 시각
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBTargetable.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponMovesetConfig.h"
#include "Enemy/VBEnemyBase.h" // ResolveStrideScale - 공격 자산의 거리는 애니 고유 단위라 공격자 몸 배율을 곱한다
#include "Data/VBSwingImpactProfile.h" // Cast<> 대상 + LocalSwingAxis 읽기 — 완전 타입 필요
#include "AbilitySystem/Abilities/VBWeaponStyleBehavior.h"
#include "GameFramework/Character.h"
#include "Animation/AnimMontage.h" // ExtractRootMotionFromTrackRange
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

bool UVBGA_MeleeAttackBase::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                               const FGameplayAbilityActorInfo* ActorInfo,
                                               const FGameplayTagContainer* SourceTags,
                                               const FGameplayTagContainer* TargetTags,
                                               FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const AVBCharacter* VBChar = ActorInfo ? Cast<AVBCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	const UVBWeaponStateComponent* WSC = VBChar ? VBChar->GetWeaponStateComponent() : nullptr;
	if (WSC && WSC->GetResummonWeaponType() == EVBWeaponType::None)
	{
		// 막힌 이유를 남긴다 - 안 그러면 «공격 키가 안 먹는다» 가 유일한 증상이다. 소유 목록은 소유 클라에도 복제되므로 클라에서도 같은 판정이다.
		VB_LOG(Log, "%s: 가진 무기가 없어 공격하지 않는다", *GetNameSafe(VBChar));
		return false;
	}
	return true;
}

void UVBGA_MeleeAttackBase::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                           const FGameplayAbilityActorInfo*     ActorInfo,
                                           const FGameplayAbilityActivationInfo ActivationInfo,
                                           const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	++ActivationSerial;   // 맞은 뒤 처리가 «같은 스윙» 인지 가르는 번호(ApplySwingHits)

	// ServerInitiated 전환으로 이 함수는 서버/클라 양쪽에서 실행된다.
	// 몽타주 재생 Task는 양쪽 공통(로컬 재생), Trace/GE/Event 송신은 서버 전용 가드 필요.
	const bool bIsServer = ActorInfo && ActorInfo->IsNetAuthority();

	// 콤보 «첫 타» 의 처형 관문. 다음 타 쪽은 TryChainToNextSection 이 맡는다.
	if (TryOpenFinisher(ActorInfo))
	{
		// 처형이 열렸다. 이 공격은 몽타주를 틀지 않고 조용히 끝낸다.
		// 취소가 아니라 정상 종료다 - 입력은 의도대로 처리됐고 다른 어빌리티가 그 일을 맡았다.
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility=*/true,
		           /*bWasCancelled=*/false);
		return;
	}

	ComboIndex       = 0;
	// 변형 재생배율은 활성화마다 초기화한다(VariantCursor 와 반대).
	// 이 값은 "이번에 고른 변형의 배율"이라 다음 활성화로 새어 나가면 안 된다.
	ActiveVariantRate = 1.0f;
	ActiveVariantDamage = 1.0f;
	ActiveVariantApproachOffset = -1.0f;
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
				// 기본 무기를 안 가졌으면 가진 것 중에서 고른다(GetResummonWeaponType). 하나도 없으면 CanActivateAbility 가 이미 막았다.
				const EVBWeaponType SummonType = (WSC->GetCurrentWeaponType() != EVBWeaponType::None)
					? WSC->GetCurrentWeaponType()
					: (WSC->IsWeaponOwned(WSC->GetDefaultWeaponType()) ? WSC->GetDefaultWeaponType() : WSC->GetResummonWeaponType());
				WSC->EnsureWeaponSummoned(SummonType);
			}
		}
		VBChar->NotifyCombatStimulus();
	}

	// 무브셋·공격 자산 해석은 아바타 종류와 무관하다 - 적(AVBCharacter 아님)도 여기서 leaf 를 잡는다.
	//   종전에는 위 캐릭터 가드 안에 있어 적 GA 는 이 블록을 아예 안 거쳤다(2026-09-06 PIE 세 판 연속 «미설정» 의 실제 원인).
	{
		// 현재 무기 타입의 leaf. 무브셋이 없거나 미등록이면 GA 에 직접 할당된 AttackConfig 가 그 자리다 -
		//   PeekAttackConfig 와 같은 규칙(예고 단계가 읽는 자산과 스윙이 재생하는 자산이 갈리면 안 된다).
		// 종전(2026-09-06 이전)은 무브셋이 있을 때만 여기 들어와 적 GA 는 AttackConfig->AttackMontage 를 영영 안 읽었다.
		//   그래서 몽타주의 홈이 «적 = GA 의 AnimMontage / 플레이어 = 공격 자산» 둘로 갈렸고, 공격 자산에만 몽타주를
		//   적은 병사 GA 가 «AttackMontage 미설정» 폴백으로 떨어져 애니 없이 즉시 데미지를 넣었다(PIE 실측).
		//   지금은 공격 자산이 먼저고, 거기 몽타주가 없을 때만 GA 의 AnimMontage 가 남는다(감염체 BP 가 그 경로).
		UVBAttackConfig* Leaf = ResolveMovesetLeaf();
		if (!Leaf)
		{
			Leaf = AttackConfig;
		}
		int32 VariantCount = 0;
		int32 ChosenVariantIndex = 0;
		if (Leaf)
		{
			ActiveConfig = Leaf;

			// 변형 해석. 변형이 없으면 결과가 AttackMontage 그대로라 현행과 동일 동작이다.
			VariantCount = Leaf->GetMontageVariantCount();
			ChosenVariantIndex = SelectVariantIndex(*Leaf, VariantCount);
			const FVBAttackMontageVariant Variant = Leaf->ResolveMontageVariant(ChosenVariantIndex);
			if (Variant.Montage)
			{
				ActiveMontage    = Variant.Montage;
				ActiveVariantRate   = Variant.PlayRateScale;
				ActiveVariantDamage = Variant.DamageScale;
				ActiveVariantApproachOffset = Leaf->GetVariantApproachOffset(ChosenVariantIndex, AVBEnemyBase::ResolveStrideScale(GetAvatarActorFromActorInfo()));
			}

		}
		// 무브셋 해석 1줄 계측 — 무기 타입/leaf/최종 몽타주/변형 (오선택 진단용)
		VB_LOG(Log, "%s: Moveset resolve — WeaponType=%d Leaf=%s Montage=%s variant=%d/%d rate=%.2f",
			*GetName(), (int32)GetCurrentWeaponType(),
			Leaf ? *Leaf->GetName() : TEXT("null(legacy-fallback)"),
			ActiveMontage ? *ActiveMontage->GetName() : TEXT("null"),
			VariantCount > 0 ? ((ChosenVariantIndex % VariantCount) + VariantCount) % VariantCount : 0,
			VariantCount, ActiveVariantRate);
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

	// 거대검 슈퍼아머(SuperArmor): leaf 설정이 슈퍼아머를 요구하면 스윙 동안 피격 히트리액트를 억제할 태그 부여.
	// 억제 방식 = 피격자의 UVBGA_HitReact 가 이 태그를 ActivationBlockedTags 로 걸어 활성화 자체가 안 된다.
	//   (연출을 스킵하는 게 아니라 능력이 안 켜지는 것 — GAS 네이티브 게이트라 커스텀 판정 코드가 0이다.)
	// 로스태그가 복제되지 않아도 무방한 이유: 트리거 활성화는 서버 권위에서만 시도되므로 판정이 읽는 ASC 는 항상 서버 ASC 이고,
	//   서버엔 이 태그가 확실히 있다(이 함수가 서버에서도 실행된다).
	// 이력: 최초 GCN_DamagePhysical BP 그래프 Branch → DEBT-009 1단계 C++ GCN 베이스 → 2단계 GA 게이트(현재, 2026-07-16).
	// ServerInitiated라 이 함수가 서버+소유클라 양쪽에서 실행 → 양쪽 ASC에 부여(히트리액트는 소유클라 로컬 재생이라 클라 태그 필수).
	// 제거는 EndAbility(완료/중단/취소 3경로가 수렴 — BlendOut 은 EndAbility 를 호출하지 않으나 뒤이어 Completed 가 온다)에서 단일화. bGrantedSuperArmor로 부여한 경우만 제거.
	if (ActiveConfig && ActiveConfig->bSuperArmorDuringAttack)
	{
		GrantSuperArmorOnce();
	}

	// 회피 오프셋 (design §3.4): 회피가 끊은 콤보를 유예 안에 같은 무기로 이어 받는다. 시작 섹션으로 표현하므로 몽타주는 그대로다.
	// 워프 등록보다 먼저 - 워프 로그의 «몇 타» 가 재개한 타를 가리키게(2026-09-06 로그에서 1타로 찍히던 것 정정).
	FName StartSection = NAME_None;
	{
		const double Now   = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		const float  Grace = ActiveConfig ? ActiveConfig->DodgeOffsetGraceSeconds : 0.0f;
		// DodgeOffsetComboIndex 는 «재개할 타» 다. 끊긴 타가 아직 안 닿았으면 그 타, 닿았으면 다음 타(EndAbility 에서 정함).
		//   종전(같은 날 첫 판)은 무조건 다음 타라 닿기 전에 끊은 타를 건너뛰었다(user 「그 콤보는 스킵되고 다음 콤보로」).
		if (DodgeOffsetComboIndex > 0 && Grace > 0.0f && (Now - DodgeOffsetTime) <= Grace
		    && DodgeOffsetWeapon == GetCurrentWeaponType()
		    && ActiveConfig && DodgeOffsetComboIndex < ActiveConfig->MaxComboCount
		    && ActiveConfig->ComboSectionNames.IsValidIndex(DodgeOffsetComboIndex))
		{
			ComboIndex   = DodgeOffsetComboIndex;
			StartSection = ActiveConfig->ComboSectionNames[ComboIndex];
			VB_LOG(Log, "[Combo] 회피 오프셋 - %.2f초 전 끊김 -> %d타(%s)부터 재개",
			       Now - DodgeOffsetTime, ComboIndex + 1, *StartSection.ToString());
		}
		DodgeOffsetComboIndex = -1;   // 한 번 쓰거나 유예를 넘기면 버린다
	}
	ComboInputTime     = -1.0;
	bChainedThisWindow = false;
	ActiveComboWindowNotify.Reset();
	bHitFrameReached   = false;
	HitFrameTime       = -1.0;

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
			                                                               StartSection,
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

	// ReadyForActivation 이 이 어빌리티를 «그 자리에서» 끝낼 수 있다.
	//   엔진이 몽타주를 못 틀면(UAbilityTask_PlayMontageAndWait::Activate 의 `if (!bPlayedMontage)`)
	//   Activate 안에서 OnCancelled 를 쏘고, 위에서 건 OnMontageCancelled -> EndAbility 가 동기로 돈다.
	//   그때 아래 이벤트 대기 Task 들을 그대로 만들면 EndAbility 가 ActiveTasks 를 비운 «뒤» 에
	//   ASC 콜백에 등록되어 고아가 된다. 이 GA 는 InstancedPerActor 라 그 고아가 다음 활성화까지 살아
	//   같은 노티파이 하나에 핸들러가 두 번 돌고, 한 번의 휘두름이 피해를 두 번 넣는다
	//   (ProcessedActors 는 호출마다 새로 만드는 지역 변수라 이중 적용을 못 막는다).
	// 못 트는 경우가 실제로 있다: VBAbilitySystemComponent::PlayMontage 는 ExternalMontageOwner 가
	//   잡혀 있으면 0 을 돌려주고, 처형은 두 참가자 모두에게 그것을 건다.
	// 같은 가드가 VBGA_GetUp · VBGA_EnemyMeleeAttack · VBGA_FinisherWindow 에 이미 있다 - 여기만 빠져 있었다.
	if (!IsActive())
	{
		return;
	}


	// 6. AnimNotify에서 보내는 GameplayEvent 대기 — 양쪽 공통 (2026-09-05 개정)
	// 종전엔 서버 전용이었다(클라 이중 트레이스 방지). 지금은 클라도 «닿는 프레임» 을 알아야 조기 전환을 같은 시점에 하므로
	//   양쪽이 듣고, 트레이스·스타일 훅은 핸들러 안에서 권위 가드로 서버만 돈다.
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

	// 판정 창 닫힘 - 서버만 듣는다. 창은 서버에서만 열린다(UVBMeleeTraceComponent::BeginWindow 의 권위 관문).
	if (bIsServer)
	{
		UAbilityTask_WaitGameplayEvent* EventTaskAttackTraceEnd =
				UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
				                                                  this,
				                                                  VBGameplayTags::Event_Montage_AttackTraceEnd,
				                                                  nullptr,
				                                                  false,	// 섹션마다 창이 하나씩 닫힌다
				                                                  true);
		EventTaskAttackTraceEnd->EventReceived.AddDynamic(this, &ThisClass::OnAttackTraceEndEvent);
		EventTaskAttackTraceEnd->ReadyForActivation();
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
	// 닿는 프레임 도달 - 양쪽 공통. 조기 전환의 두 번째 조건이다(design §3.2 정정).
	bHitFrameReached = true;
	HitFrameTime     = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// 트레이스·스타일 훅은 서버만. 클라는 표시만 하고 전환 시도로 간다.
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		TryChainAfterHit();
		return;
	}
	VB_LOG(Log, "%s: AttackTrace 이벤트 수신 -> Trace 실행", *GetName());
	// 계측: 닿는 프레임에 목표까지 남은 거리. [Warp] 시작 거리와의 차가 «워프가 실제로 붙여 준 거리» 다.
	if (const USceneComponent* AimTarget = ResolveAimTargetComponent())
	{
		if (const AActor* Avatar = GetAvatarActorFromActorInfo())
		{
			VB_LOG(Log, "[Warp] %d타 닿는 프레임 - 목표 %s 까지 %.0fcm", ComboIndex + 1, *GetNameSafe(AimTarget->GetOwner()),
			       FVector::Dist2D(Avatar->GetActorLocation(), AimTarget->GetComponentLocation()));
		}
	}

	// 이번 스윙의 임팩트 쉐이크 프로필을 캐시한다. 노티마다 덮어쓴다.
	// 왜 활성화 단위가 아니라 노티 단위인가: 한 몽타주 안의 2타가 1타 프로필을 물려받으면
	//  저작 입도가 스윙에서 공격으로 뭉개져, 노티에 프로필을 꽂은 의미가 사라진다.
	// null 을 그대로 받아들이는 것도 의도다 — 미저작 노티는 leaf 폴백을 타야 한다.
	ActiveSwingProfile = Cast<UVBSwingImpactProfile>(PayLoad.OptionalObject.Get());
	// 전투 수치 배율은 두 번째 슬롯에서 온다. 위와 같은 이유로 노티마다 덮어쓰고 null 도 그대로 받는다 -
	//  여기서 null 을 걸러 이전 값을 유지하면 저작 안 한 스윙이 직전 스윙의 무게를 물려받는다.
	ActiveSwingCombatProfile = Cast<UVBSwingCombatProfile>(PayLoad.OptionalObject2.Get());

	// 스타일 고유 타격(예: 마법 투사체)이 있으면 위임. 처리되면(true) 기본 판정 스킵. 판정 창이면 창 시작에서 한 번 묻는다.
	if (ActiveConfig && ActiveConfig->StyleBehavior && ActiveConfig->StyleBehavior->DeliverHit(this))
	{
		TryChainAfterHit();
		return;
	}
	// 판정 창(UVBANS_AttackTraceWindow)은 Melee.Window 표지를 싣는다. 없으면 옛 한 프레임 노티다 - 창으로 안 바꾼 몽타주는 옛 방식 그대로 돈다.
	if (PayLoad.InstigatorTags.HasTagExact(VBGameplayTags::Melee_Window))
	{
		BeginTraceWindow(PayLoad);
	}
	else
	{
		PerformAttackTrace();
	}
	TryChainAfterHit();
}

void UVBGA_MeleeAttackBase::OnAttackTraceEndEvent(FGameplayEventData PayLoad)
{
	FGameplayTag Shape;
	for (const FGameplayTag& Tag : PayLoad.InstigatorTags)
	{
		if (Tag.MatchesTag(VBGameplayTags::Melee_Shape) && Tag != VBGameplayTags::Melee_Shape)
		{
			Shape = Tag;
			break;
		}
	}
	// 모양 없는 창은 BeginTraceWindow 가 열지 않았다(경고 후 옛 판정). 닫을 것이 없다 - 여기서 전부 닫으면 겹쳐 열린 다른 창을 끊는다.
	//   남는 창은 섹션 전환·EndAbility 가 닫는다.
	if (Shape.IsValid())
	{
		EndTraceWindowByShape(Shape, PayLoad.EventMagnitude > 0.0f);
	}
}

void UVBGA_MeleeAttackBase::TryChainAfterHit()
{
	// 네 조건: 저장 입력 / 조기 전환 스위치 / 창 열림 / 닿음. 여운이 남았으면 타이머로 돌아온다.
	if (!bSaveAttack || !ActiveConfig || !ActiveConfig->bChainAtWindowOpen) return;
	if (!bComboWindowOpen || !bHitFrameReached) return;
	const double Now       = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double Remaining = (HitFrameTime + ActiveConfig->ChainDelayAfterHitSeconds) - Now;
	if (Remaining > 0.0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(ChainDelayTimer, this, &UVBGA_MeleeAttackBase::TryChainAfterHit, static_cast<float>(Remaining), false);
		}
		return;
	}
	TryChainToNextSection();
}

// 몽타주 정상 완료 -> GA 종료
void UVBGA_MeleeAttackBase::OnMontageCompleted()
{
	if (AVBCharacter* Character = Cast<AVBCharacter>(GetAvatarCharacter()))
		Character->StopResidualAttackMotion(0.0f, ActiveMontage);
	LogMontageMeshState(TEXT("Completed"), GetAvatarCharacter());
	VB_LOG(Log, "%s: 몽타주 완료 -> EndAbility", *GetName());
	ClearWarpTarget();
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

// 몽타주 BlendOut 시작
void UVBGA_MeleeAttackBase::OnMontageBlendOut()
{
	// 저작된 복귀는 자연 블렌드아웃까지 유지한다. 무입력 잔여 이동만 이 몽타주의 수명에 묶어 정리한다.
	if (AVBCharacter* Character = Cast<AVBCharacter>(GetAvatarCharacter()))
	{
		const float Frame = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.0f;
		Character->StopResidualAttackMotion(2.0f * Frame, ActiveMontage);
		Character->BeginRecoveryCancelTrace();
	}
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

// 공격 종료 — 슈퍼아머 로스태그 제거(부여했던 경우만).
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

	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ChainDelayTimer); }

	// 열린 판정 창을 닫는다. 몽타주가 끊기면 노티의 끝 알림도 오지만, 그보다 먼저 이 GA 가 끝나면 끝 이벤트 대기 태스크가 이미 없다.
	//   GA 는 InstancedPerActor 라 닫지 않으면 다음 활성화에 옛 창의 콜백이 들어온다.
	EndAllTraceWindows();

	// 회피 오프셋 기억 (design §3.4): 회피가 끊었을 때만. 피격·가드·완료·회복 캔슬은 기억하지 않는다.
	if (bWasCancelled && ActiveConfig && ActiveConfig->DodgeOffsetGraceSeconds > 0.0f)
	{
		const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
		if (OwnerASC && OwnerASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Dodging))
		{
			// 재개할 타: 끊긴 타가 아직 안 닿았으면 그 타를 다시, 닿았으면 다음 타. 닿음은 bHitFrameReached 가 이 섹션 기준으로 들고 있다.
			DodgeOffsetComboIndex = bHitFrameReached ? ComboIndex + 1 : ComboIndex;
			DodgeOffsetTime       = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			DodgeOffsetWeapon     = GetCurrentWeaponType();
			VB_LOG(Log, "[Combo] 회피가 %d타를 끊음(닿음 %s) - %.1f초 안에 %d타부터 재개",
			       ComboIndex + 1, bHitFrameReached ? TEXT("O") : TEXT("X"), ActiveConfig->DodgeOffsetGraceSeconds, DodgeOffsetComboIndex + 1);
		}
	}

	ClearWarpTarget();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// 스윙 진입에서 확정한 대상만 읽는다. 도중 락 전환은 다음 스윙에 반영한다.
const USceneComponent* UVBGA_MeleeAttackBase::ResolveAimTargetComponent() const
{
	AActor* Target = SwingTarget.Get();
	if (!IsValid(Target)) return nullptr;
	const IVBTargetable* Targetable = Cast<IVBTargetable>(Target);
	return Targetable && Targetable->IsTargetable() ? Target->GetRootComponent() : nullptr;
}

void UVBGA_MeleeAttackBase::ResetSwingTarget()
{
	if (UAbilitySystemComponent* ASC = SwingTargetASC.Get(); ASC && SwingTargetDeathHandle.IsValid())
		ASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(SwingTargetDeathHandle);
	if (AActor* Target = SwingTarget.Get())
		Target->OnDestroyed.RemoveDynamic(this, &ThisClass::OnSwingTargetDestroyed);
	SwingTargetDeathHandle.Reset();
	SwingTargetASC.Reset();
	SwingTarget.Reset();
}

void UVBGA_MeleeAttackBase::CaptureSwingTarget()
{
	ResetSwingTarget();
	const AVBCharacter* Character = Cast<AVBCharacter>(GetAvatarActorFromActorInfo());
	const UVBTargetLockComponent* Lock = Character ? Character->GetTargetLockComponent() : nullptr;
	if (!Lock) return;
	AActor* Target = Lock->GetLockedTarget();
	if (!IsValid(Target)) Target = Lock->FindSoftLungeTarget();
	const IVBTargetable* Targetable = Cast<IVBTargetable>(Target);
	if (!Targetable || !Targetable->IsTargetable()) return;
	SwingTarget = Target;
	Target->OnDestroyed.AddDynamic(this, &ThisClass::OnSwingTargetDestroyed);
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Target))
	{
		SwingTargetASC = ASC;
		SwingTargetDeathHandle = ASC->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::OnSwingTargetDeath);
	}
}

void UVBGA_MeleeAttackBase::OnSwingTargetDeath(const FGameplayTag /*Tag*/, int32 NewCount)
{
	if (NewCount > 0) ClearWarpTarget();
}

void UVBGA_MeleeAttackBase::OnSwingTargetDestroyed(AActor* /*DestroyedActor*/)
{
	ClearWarpTarget();
}

void UVBGA_MeleeAttackBase::SetWarpTargetLockedEnemy()
{
	CaptureSwingTarget();
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
		VB_LOG(Log, "[Warp] %d타 목표 없음 - 접근 워프 없음 (락온 X, 소프트 콘 밖)", ComboIndex + 1);
	}
}

float UVBGA_MeleeAttackBase::ComputeApproachOffset(const USceneComponent* TargetComponent) const
{
	// 변형이 해석된 뒤면 그 변형의 멈춤(이미 몸 배율을 곱했다), 아니면 자산 값에 몸 배율을 곱한 것. 두 경우 다 아래 사슬은 같다.
	const float BaseOffset = ActiveVariantApproachOffset >= 0.0f
		                         ? ActiveVariantApproachOffset
		                         : (ActiveConfig ? ActiveConfig->WarpApproachOffset * AVBEnemyBase::ResolveStrideScale(GetAvatarActorFromActorInfo()) : 0.0f);
	return ComputeApproachOffset(TargetComponent, BaseOffset);
}

float UVBGA_MeleeAttackBase::ComputeApproachOffset(const USceneComponent* TargetComponent, float BaseOffset) const
{
	const ACharacter* VBChar = GetAvatarCharacter();
	if (!VBChar || !TargetComponent) return 0.0f;
	float ApproachOffset = 0.0f;
	if (ActiveConfig)
	{
		// BaseOffset 은 이미 몸 배율이 곱해진 값이다 - 상한인 사거리도 같은 단위로 맞춘다.
		const float Range = ActiveConfig->GetScaledAttackRange(AVBEnemyBase::ResolveStrideScale(VBChar));
		ApproachOffset = FMath::Clamp(BaseOffset, 0.0f, FMath::Max(0.0f, Range));
		// 먼 시작(갭 클로저): 적이 사거리 밖에서 시작한 스윙은 더 가까이 붙는다 - 붙은 것이 눈에 보이게. FarStartApproachOffset=0 이면 없음.
		{
			const float StartDist = FVector::Dist2D(VBChar->GetActorLocation(), TargetComponent->GetComponentLocation());
			if (ActiveConfig->FarStartApproachOffset > 0.0f && StartDist > Range)
			{
				ApproachOffset = FMath::Clamp(ActiveConfig->FarStartApproachOffset, 0.0f, ApproachOffset);
			}
		}

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
		const float MinSeparation = ComputeMinSeparation(TargetComponent);
		// DistToEnemy 와 min 을 거는 이유: 이미 캡슐이 겹친 상태라면 하한을 그대로 적용했다가는
		// 오프셋이 현재 거리보다 커져 워프 타겟이 플레이어 뒤로 가고 BUG-007 이 되살아난다.
		// 그 경우엔 현재 거리를 그대로 써 전진량 0 으로 둔다 - 밀어내는 건 넉백의 몫이다.
		if (MinSeparation > 0.0f)
		{
			ApproachOffset = FMath::Max(ApproachOffset, FMath::Min(MinSeparation, DistToEnemy));
		}
	}

	return ApproachOffset;
}

int32 UVBGA_MeleeAttackBase::SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount)
{
	const int32 Chosen = VariantCursor;
	// 변형이 2개 이상일 때만 커서를 민다.
	// 왜: 커서는 GA 하나에 하나인데 ActiveConfig 는 무기에 따라 바뀐다.
	//   변형이 1개인 무기(대검)가 커서를 밀면 카타나 반격이 A -> A 로 반복된다.
	if (VariantCount > 1)
	{
		++VariantCursor;
	}
	return Chosen;
}

float UVBGA_MeleeAttackBase::MeasureMontageLungeCm(const UAnimMontage* Montage, float MeshScale)
{
	if (!Montage) return 0.0f;
	// 재는 구간 = 처음부터 접근 워프 창(AttackTarget 을 겨눈 MotionWarping 노티)이 닫히는 시각까지. 워프는 그 창 안의 루트모션만
	//   목적지에 맞추고, 창 뒤의 루트모션은 생으로 돈다. 트롤 클립은 타격 뒤에도 한참 걷는다(2026-09-17 실측: 01 은 창까지 118cm,
	//   창 뒤 276cm / 03 은 44cm 와 306cm). 클립 전체를 재면 그 뒷걸음까지 «전진» 으로 세어 시작 거리가 실제 닿는 거리보다 멀어지고,
	//   워프는 창 안의 작은 이동(03 은 42cm)을 그 큰 값으로 늘여야 해서 순간이동처럼 보인다(user 「아직 순간이동」).
	//   창이 없으면 클립 끝까지(제자리 클립은 어차피 0 근처). 세그먼트의 시퀀스에 EnableRootMotion 이 꺼져 있으면 0 - 그것도 «안 간다» 는 사실이다.
	float EndTime = Montage->GetPlayLength();
	float WindowEnd = 0.0f;
	if (FindApproachWarpModifier(Montage, &WindowEnd))
	{
		EndTime = FMath::Min(EndTime, WindowEnd);
	}
	FAnimExtractContext Context(0.0, /*bExtractRootMotion*/ true);
	const FTransform RootMotion = Montage->ExtractRootMotionFromTrackRange(0.0f, EndTime, Context);
	return static_cast<float>(RootMotion.GetTranslation().Size2D()) * MeshScale;
}

const URootMotionModifier_Warp* UVBGA_MeleeAttackBase::FindApproachWarpModifier(const UAnimMontage* Montage, float* OutWindowEndTime)
{
	if (!Montage) return nullptr;
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		const UAnimNotifyState_MotionWarping* WarpState = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
		const URootMotionModifier_Warp*       Modifier  = WarpState ? Cast<URootMotionModifier_Warp>(WarpState->RootMotionModifier) : nullptr;
		if (Modifier && Modifier->WarpTargetName == VBMotionWarpingNames::AttackTarget)
		{
			if (OutWindowEndTime)
			{
				*OutWindowEndTime = Event.GetTriggerTime() + Event.GetDuration();
			}
			return Modifier;
		}
	}
	return nullptr;
}

float UVBGA_MeleeAttackBase::ComputeMinSeparation(const USceneComponent* TargetComponent) const
{
	const ACharacter* VBChar = GetAvatarCharacter();
	if (!VBChar || !TargetComponent) return 0.0f;
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
	return MinSeparation;
}

void UVBGA_MeleeAttackBase::ApplyApproachWarpToTarget(const USceneComponent* TargetComponent)
{
	ApplyApproachWarpWithOffset(TargetComponent, ComputeApproachOffset(TargetComponent));
}

void UVBGA_MeleeAttackBase::ApplyApproachWarpWithOffset(const USceneComponent* TargetComponent, float ApproachOffset, bool bFollowTarget)
{
	// 락 경로와 소프트 런지 경로가 공유하는 워프 목적지 write(DRY).
	// 회귀 불변식: 아래 수치 거동은 리팩터 전 SetWarpTargetLockedEnemy 락 경로와 동일해야 한다.
	// 아바타를 ACharacter 로 받는다(2026-09-15): 적 공격 GA 도 이 경로로 워프를 세운다(design .sw/boss/design.md §12).
	//   종전 AVBCharacter 캐스트는 플레이어 전용이라 적 몽타주의 SkewWarp 구간이 목표 없이 돌았다.
	//   컴포넌트는 FindComponentByClass 로 찾는다 - 플레이어는 같은 컴포넌트가 나오므로 거동이 같다.
	ACharacter* VBChar = GetAvatarCharacter();
	if (!VBChar || !TargetComponent) return;

	UMotionWarpingComponent* MotionWarpingComponent = VBChar->FindComponentByClass<UMotionWarpingComponent>();
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
	// 계측(2026-09-06 user 「회피로 멀어졌을 때 붙는 보정이 없나」): 목표·거리·오프셋·락온 여부를 한 줄로. 닿는 프레임의 거리와 짝을 이룬다.
	{
		// 락온 컴포넌트는 플레이어에게만 있다. 적이면 «적» 으로 적어 같은 한 줄이 양쪽 경로를 덮게 한다.
		const AVBCharacter* PlayerChar = Cast<AVBCharacter>(VBChar);
		const UVBTargetLockComponent* TLC = PlayerChar ? PlayerChar->GetTargetLockComponent() : nullptr;
		VB_LOG(Log, "[Warp] %d타 목표=%s 거리=%.0fcm 오프셋=%.0fcm 락온=%s",
		       ComboIndex + 1, *GetNameSafe(TargetComponent->GetOwner()),
		       FVector::Dist2D(VBChar->GetActorLocation(), TargetComponent->GetComponentLocation()), ApproachOffset,
		       !PlayerChar ? TEXT("적") : ((TLC && TLC->IsLockedOn()) ? TEXT("O") : TEXT("X(소프트)")));
	}
	// 고정 목표(적): 지금 대상 위치에서 공격자 쪽으로 ApproachOffset 인 점, 대상을 보는 회전. 휘두르는 도중 대상이 물러나도 목표는 안 움직인다 -
	//   따라가게 두면 오프셋을 정한 대역 계산이 무너져 클립보다 멀리 끌려갔다(design .sw/boss/design.md §16). 회전은 몽타주 SkewWarp 의 Facing 이 이 점을 본다.
	if (!bFollowTarget)
	{
		const FVector TargetLocation = TargetComponent->GetComponentLocation();
		const FVector ToOwner        = (VBChar->GetActorLocation() - TargetLocation).GetSafeNormal2D();
		const FVector Goal           = FVector(TargetLocation.X, TargetLocation.Y, VBChar->GetActorLocation().Z) + ToOwner * ApproachOffset;
		const FRotator Facing        = ToOwner.IsNearlyZero() ? VBChar->GetActorRotation() : (-ToOwner).Rotation();
		MotionWarpingComponent->AddOrUpdateWarpTargetFromLocationAndRotation(VBMotionWarpingNames::AttackTarget, Goal, Facing);
		return;
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
	ResetSwingTarget();
	// 세우는 쪽(ApplyApproachWarpToTarget)과 같은 탐색 - 적도 지운다.
	if (ACharacter* Avatar = GetAvatarCharacter())
	{
		if (UMotionWarpingComponent* MotionWarpingComponent = Avatar->FindComponentByClass<UMotionWarpingComponent>())
			MotionWarpingComponent->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	}
}

void UVBGA_MeleeAttackBase::OnComboWindowOpen(FGameplayEventData Payload)
{
	ActiveComboWindowNotify = Payload.OptionalObject.Get();
	bComboWindowOpen   = true;
	bChainedThisWindow = false;

	// AI 자동 콤보: 공격 자산이 «버튼 없이 잇는다» 고 하면 창이 열리는 순간이 곧 입력이다.
	//   아래 버퍼·전환 규칙은 그대로 탄다 - 적과 플레이어의 콤보 타이밍 홈이 하나다.
	if (ActiveConfig && ActiveConfig->bAutoChainCombo && !bSaveAttack)
	{
		bSaveAttack = true;
		VB_LOG(Log, "[Combo] 자동 잇기 - %d타 창 열림", ComboIndex + 1);
	}

	// 버퍼 소비 (design §3.1): 창이 열리기 전에 눌린 입력은 나이가 버퍼 안이면 지금 쓴다.
	if (ComboInputTime >= 0.0 && !bSaveAttack)
	{
		const double Age    = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) - ComboInputTime;
		const float  Buffer = ActiveConfig ? ActiveConfig->ComboInputBufferSeconds : 0.0f;
		if (Age <= Buffer)
		{
			bSaveAttack = true;
			VB_LOG(Log, "[Combo] 버퍼 소비 - 입력 나이 %.2f초 (버퍼 %.2f)", Age, Buffer);
		}
		else
		{
			VB_LOG(Log, "[Combo] 버퍼 만료 - 입력 나이 %.2f초 > %.2f, 버림", Age, Buffer);
			ComboInputTime = -1.0;
		}
	}
	// 조기 전환 (design §3.2 정정): 창 열림만으로 넘기지 않는다 - 닿음이 지났고 여운이 끝났을 때. 아직이면 닿는 이벤트·타이머가 다시 부른다.
	TryChainAfterHit();
}

void UVBGA_MeleeAttackBase::OnComboWindowClose(FGameplayEventData Payload)
{
	// 점프가 옛 창의 끝을 늦게 보내도 새 창의 입력/상태를 소비하지 않는다.
	if (Payload.OptionalObject.Get())
	{
		if (Payload.OptionalObject.Get() != ActiveComboWindowNotify.Get()) return;
		ActiveComboWindowNotify.Reset();
		if (Payload.EventMagnitude <= 0.0f)
		{
			bComboWindowOpen = false;
			return; // 취소된 창은 다음 공격으로 잇거나 복귀를 취소할 수 없다.
		}
	}
	bComboWindowOpen = false;

	// 섹션 점프가 낸 옛 창의 닫힘 - 이미 다음 타로 넘어갔으므로 아무것도 하지 않는다.
	if (bChainedThisWindow)
	{
		bChainedThisWindow = false;
		return;
	}
	// 닫힐 때 전환: bChainAtWindowOpen 이 꺼진 무기, 또는 창 안 입력이 아직 소비되지 않은 경우.
	// 종전(2026-09-05 이전)에는 이것이 유일한 전환 시점이었다 - «콤보 리듬감» 을 위한 선택이었으나 최대 0.3초 늦게 반응했다.
	if (bSaveAttack && TryChainToNextSection())
	{
		bChainedThisWindow = false;   // 이 닫힘이 곧 옛 창이라 추가 닫힘은 오지 않는다
		return;
	}
	// 회복 캔슬 (design §3.3): 다음 타가 없으면 여기서 자유롭게 한다. 종전엔 섹션 끝까지 굳었다(카타나 0.57~1.38초).
	if (ActiveConfig && ActiveConfig->bCancelRecoveryAtWindowClose)
	{
		CancelRecovery();
	}
}

bool UVBGA_MeleeAttackBase::TryOpenFinisher(const FGameplayAbilityActorInfo* ActorInfo)
{
	if (!bCanOpenFinisher)
	{
		return false;
	}
	// 서버에서만 쏜다 - 처형은 대상 선정·즉사 피해·명성이 전부 서버 몫이다(UVBGA_Execute 첫머리).
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return false;
	}

	UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC)
	{
		return false;
	}

	FGameplayEventData Payload;
	Payload.EventTag   = VBGameplayTags::Event_Combat_ExecuteRequested;
	Payload.Instigator = ActorInfo->AvatarActor.Get();

	// ★대상과 사거리를 여기서 판정하지 않는다.★ 그것은 UVBGA_Execute 의 설정
	//   (UVBMedicalConfig::MedicalRange)이 정하고, 이벤트를 받은 그쪽이 FindFinisherTarget 으로 최종
	//   판단한다. 여기서 같은 판정을 한 벌 더 하면 두 사거리가 어긋나는 날 「가끔 안 걸린다」가 된다.
	// 성공 여부는 «활성화된 어빌리티 수» 로 받는다 - 0 이면 안 열린 것이고 부르는 쪽이 제 갈 길 간다.
	if (OwnerASC->HandleGameplayEvent(Payload.EventTag, &Payload) <= 0)
	{
		return false;
	}

	VB_LOG(Log, "%s: 처형 창이 열려 있어 공격 대신 처형으로 넘긴다 (%d타)", *GetName(), ComboIndex + 1);
	return true;
}

bool UVBGA_MeleeAttackBase::TryChainToNextSection()
{
	if (!bSaveAttack) return false;

	// ★자동 체인용 관문★ — 입력으로 들어온 것은 OnComboInputReceived 가 이미 «즉시» 잡는다.
	// 여기가 남는 이유는 bAutoChainCombo 다: 그 설정이 켜진 무기는 입력 없이도 다음 타로 이어지므로
	// 입력 관문을 지나가지 않는다. 그 경로에도 처형이 열려야 한다.
	// (관문이 셋인 이유 = 첫 타 / 입력 / 자동 체인. 상태가 넘어가는 자리가 셋이다.)
	if (TryOpenFinisher(GetCurrentActorInfo()))
	{
		bSaveAttack    = false;
		ComboInputTime = -1.0;
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
		// true 를 돌려주는 이유: 부르는 쪽(창 닫힘 처리)이 «넘겼다» 로 보고 회복 캔슬 같은
		//   뒷정리를 건너뛰게 하려는 것이다. 어빌리티가 이미 끝났으므로 그 정리는 의미가 없다.
		return true;
	}
	if (!ActiveConfig || ComboIndex >= ActiveConfig->MaxComboCount - 1)
	{
		bSaveAttack    = false;   // 마지막 타 뒤의 입력은 버린다 - 다음 활성화가 처음부터 시작
		ComboInputTime = -1.0;
		return false;
	}
	bChainedThisWindow = true;   // 아래 점프가 옛 창의 닫힘을 동기로 낼 수 있으므로 먼저 세운다
	ActiveComboWindowNotify.Reset();
	bComboWindowOpen   = false;
	bHitFrameReached   = false;  // 다음 섹션의 닿음은 아직
	HitFrameTime       = -1.0;
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ChainDelayTimer); }
	// 옛 타의 판정 창은 여기서 끝난다. 점프 뒤 엔진이 옛 창의 끝 알림을 내지만 그때는 닫을 것이 없다.
	//   조기 전환이 창의 뒤쪽을 자른다는 뜻이다 - ChainDelayAfterHitSeconds 는 «창 시작 → 칼이 실제로 닿는 시각» 보다 길어야 한다(설계 16절).
	EndAllTraceWindows();

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

	VB_LOG(Log, "[Combo] 전환 - %d타 -> %d타(%s)", ComboIndex + 1, ComboIndex + 2, *NextSection.ToString());
	ComboIndex++;
	bSaveAttack    = false;
	ComboInputTime = -1.0;
	return true;
}

void UVBGA_MeleeAttackBase::CancelRecovery()
{
	const float Blend = ActiveConfig ? ActiveConfig->RecoveryBlendOutSeconds : 0.15f;

	// 캔슬 시점의 이동 상태를 함께 찍는다. 몽타주를 자연 종료보다 일찍 끊으므로 그 순간의 루트모션 속도가
	//   캡슐에 남을 수 있고, 남으면 모션매칭 쿼리가 그 구간을 «달리는 중» 으로 읽는다.
	//   정적 실측(2026-09-07): AM_LightAttack 3타의 창 닫힘은 클립 루트모션 203cm/s 지점이다(1/2/4타는 85/12/87).
	AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo());
	const UCharacterMovementComponent* CancelCMC = VBChar ? VBChar->GetCharacterMovement() : nullptr;
	VB_LOG(Log, "[Combo] 회복 캔슬 - %d타 창 닫힘, %.2f초 블렌드아웃 (Speed2D %.0f, 가속 %.0f)",
	       ComboIndex + 1, Blend,
	       CancelCMC ? CancelCMC->Velocity.Size2D() : -1.0f,
	       CancelCMC ? CancelCMC->GetCurrentAcceleration().Size2D() : -1.0f);
	if (VBChar)
	{
		VBChar->BeginRecoveryCancelTrace();

		// 이동 입력이 없으면 남은 이동은 플레이어 의도가 아니다 - 버린다.
		//   입력이 있으면 그 속도는 의도한 관성이므로 건드리지 않는다(회피·이동 캔슬로 이어지는 경로).
		if (CancelCMC && CancelCMC->GetCurrentAcceleration().IsNearlyZero())
		{
			VBChar->StopResidualAttackMotion(Blend, ActiveMontage);
		}
	}
	ClearWarpTarget();
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->CurrentMontageStop(Blend);
	}
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

EVBWeaponType UVBGA_MeleeAttackBase::GetCurrentWeaponType() const
{
	if (const AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarActorFromActorInfo()))
	{
		if (const UVBWeaponStateComponent* WSC = VBChar->GetWeaponStateComponent())
		{
			return WSC->GetCurrentWeaponType();
		}
	}
	return EVBWeaponType::None;
}

void UVBGA_MeleeAttackBase::GrantSuperArmorOnce()
{
	if (bGrantedSuperArmor)
	{
		return;
	}
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

UVBAttackConfig* UVBGA_MeleeAttackBase::ResolveMovesetLeaf() const
{
	// 적(AVBCharacter 아님)은 무기 타입이 None 이라 leaf 가 없다 - 레거시 AttackConfig 폴백. 종전 동작 그대로다.
	return MovesetConfig ? MovesetConfig->GetAttackConfig(GetCurrentWeaponType()) : nullptr;
}

const UVBAttackConfig* UVBGA_MeleeAttackBase::PeekAttackConfig() const
{
	const UVBAttackConfig* Leaf = ResolveMovesetLeaf();
	return Leaf ? Leaf : AttackConfig.Get();
}

void UVBGA_MeleeAttackBase::OnComboInputReceived(FGameplayEventData Payload)
{
	// ★처형은 콤보 리듬을 기다리지 않는다 — 입력 «순간» 에 묻는다.	//
	// 왜 여기가 필요한가(2026-09-19 user PIE 2회차): 관문을 TryChainToNextSection 에만 뒀더니
	//   콤보 중 전환이 「살짝 슬로우 걸린 것처럼」 나왔다. 그 경로가 입력에서 바로 오지 않기
	//   때문이다 — OnComboInputReceived -> TryChainAfterHit 이 ChainDelayAfterHitSeconds 만큼
	//   ★타이머로 기다린 뒤★ 다음 섹션으로 넘긴다(창이 안 열렸으면 열릴 때까지 더 기다린다).
	//   그 지연은 «콤보 리듬감» 을 위해 일부러 넣은 것이고 처형에는 해당되지 않는다.
	// 창이 열렸는지도 보지 않는다: 처형은 콤보 타이밍의 일부가 아니다.
	if (TryOpenFinisher(GetCurrentActorInfo()))
	{
		bSaveAttack    = false;
		ComboInputTime = -1.0;
		if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(ChainDelayTimer); }
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
		           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/false);
		return;
	}

	// 입력 버퍼 (design §3.1): 창과 무관하게 시각을 기억한다. 창이 열려 있으면 즉시 전환(§3.2), 아니면 열릴 때 나이로 판정.
	ComboInputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (bComboWindowOpen)
	{
		bSaveAttack = true;
		TryChainAfterHit();
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

bool UVBGA_MeleeAttackBase::BuildSwingHitContext(FSwingHitContext& OutContext) const
{
	const ACharacter* AvatarCharacter = GetAvatarCharacter();
	if (!AvatarCharacter || !ActiveConfig || !ActiveConfig->DamageEffectClass)
	{
		return false;
	}
	const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC)
	{
		return false;
	}

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
	// 판정 창에서는 칼 궤적이 판정이라 이 정면은 넉백 방향과 옛 구 스윕에만 쓰인다. 창이 열릴 때 한 번 정한다(설계 7절).
	OutContext.Forward = Forward;

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
	OutContext.KnockbackYaw = KnockbackDir.IsNearlyZero() ? 0.0f : KnockbackDir.Rotation().Yaw;

	// 이번 스윙의 전투 수치 배율. 대상 루프 진입 전에 한 번만 해석한다 - 넉백 방향을 루프 밖에서
	//  한 번만 구하는 것과 같은 이유다. 스윙 한 번의 무게는 맞은 대상 수와 무관하게 하나여야 한다.
	OutContext.SwingScales = ResolveSwingCombatScales();

	// 이번 스윙이 선언하는 반응 등급(DEC-025). 스윙 프로필이 덮었으면 그것, 아니면 무기 leaf 값.
	//  배율과 같은 이유로 루프 밖에서 한 번만 정한다 - 한 스윙의 무게는 대상 수와 무관하게 하나다.
	//  받는 쪽 저항은 여기서 안 본다. 그것은 피격자 허브가 한다(대상마다 저항이 다르다).
	OutContext.SwingImpact = (ActiveSwingCombatProfile && ActiveSwingCombatProfile->bOverrideHitImpact)
		? ActiveSwingCombatProfile->HitImpact
		: ActiveConfig->HitImpact;

	// 최종 데미지. 변형 배율을 곱한다 - 타격 수가 다른 변형끼리 총 데미지를 맞추기 위한 것이다(DamageScale 주석 참조).
	// 스윙 배율이 마지막 층이다 - 캐릭터/무기/변형/스윙 네 층은 입도가 각각 달라서 나뉘어 있다.
	OutContext.AttackPower = OwnerASC->GetNumericAttribute(UVBCombatAttributeSet::GetAttackPowerAttribute());
	OutContext.FinalDamage =
		OutContext.AttackPower * ActiveConfig->DamageMultiplier * ActiveVariantDamage * OutContext.SwingScales.DamageScale;

	// 노티 프로필은 노티마다 덮어써지는 GA 멤버라, 창이 열린 동안 다른 창이 덮어쓸 수 있다. 이 스윙 것을 붙들어 둔다.
	OutContext.ImpactProfile = ResolveSwingImpactProfile();
	OutContext.CombatProfile = ActiveSwingCombatProfile.Get();
	return true;
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
	FSwingHitContext Context;
	if (!AvatarCharacter || !BuildSwingHitContext(Context))
	{
		return;
	}

	// Trace 시작/끝 위치
	const FVector  Start   = AvatarCharacter->GetActorLocation();
	const FVector& Forward = Context.Forward;
	// 사거리·반경은 애니 고유 단위라 공격자 몸 배율을 곱한다(UVBAttackConfig::GetScaledAttackRange 주석) - 큰 보스는 같은 클립으로 더 멀리 닿는다.
	const float   BodyScale    = AVBEnemyBase::ResolveStrideScale(AvatarCharacter);
	const float   TraceRadius  = ActiveConfig->GetScaledAttackRadius(BodyScale);
	const FVector End = Start + (Forward * ActiveConfig->GetScaledAttackRange(BodyScale));

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
	                                                                   FCollisionShape::MakeSphere(TraceRadius),
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
		DrawDebugCapsule(AvatarCharacter->GetWorld(), Mid, Half, TraceRadius, Rot, Col, false, 1.0f);
		DrawDebugSphere(AvatarCharacter->GetWorld(), Start, TraceRadius, 12, Col, false, 1.0f);
		DrawDebugSphere(AvatarCharacter->GetWorld(), End,   TraceRadius, 12, Col, false, 1.0f);
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

	ApplySwingHits(HitResults, Context);
}

void UVBGA_MeleeAttackBase::ApplySwingHits(const TArray<FHitResult>& Hits, FSwingHitContext& Context)
{
	// 두 판정 경로(옛 구 스윕·판정 창) 모두 서버에서만 여기 온다. 새 호출자가 생겨도 클라가 피해를 내지 않게 입구에서 한 번 더 막는다(FIND-085).
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return;
	}
	ACharacter* AvatarCharacter = GetAvatarCharacter();
	UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!AvatarCharacter || !OwnerASC || !ActiveConfig || !ActiveConfig->DamageEffectClass)
	{
		return;
	}

	// 스윙 값은 문맥에서 온다 - 창이면 창이 열릴 때 정한 값이다. 아래 대상별 처리는 옛 PerformAttackTrace 의 루프 그대로다.
	const FVBSwingCombatScales& SwingScales = Context.SwingScales;
	const float        FinalDamage  = Context.FinalDamage;
	const float        KnockbackYaw = Context.KnockbackYaw;
	const EVBHitImpact SwingImpact  = Context.SwingImpact;

	// 이번 호출에서 데미지를 준 액터. 옛 구 스윕은 한 액터에서 히트가 여럿 나오므로 필요하다.
	//   판정 창은 컴포넌트가 이미 표적당 하나로 묶어 보내고, 창 안의 중복(다단 히트 간격 포함)도 컴포넌트가 가른다.
	TSet<AActor*> ProcessedActors;

	// 표적에 효과를 거는 것이 이 스윙을 끊을 수 있다 - 패링이 공격자를 경직시키면 EndAbility 가 그 자리에서 돈다.
	//   끊겼으면 그 표적의 남은 처리(락온·큐)와 남은 표적, 첫 접촉 히트스톱·쉐이크를 모두 멈춘다(옛 한 프레임 판정 루프부터 있던 구멍, Codex 리뷰 2026-09-30).
	//   이 GA 는 액터당 한 인스턴스라 끊긴 뒤 같은 호출 안에서 다시 켜질 수 있다 - 활성 여부만 보면 새 활성화를 옛 스윙으로 잇는다. 활성화 번호까지 본다.
	const uint32 SwingActivation = ActivationSerial;
	const auto IsSameSwing = [this, SwingActivation]() { return IsActive() && ActivationSerial == SwingActivation; };
	bool bSwingCut = false;

	// 맞은 대상마다 Spec/Context/GE/큐를 개별 처리
	for (const FHitResult& Hit : Hits)
	{
		if (!IsSameSwing())
		{
			bSwingCut = true;
			break;
		}
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
		//     (F) 반응 등급과 띄우는 높이(DEC-025). 넉백·기절 게이지 피해와 같은 운반 방식이다. 등급은 enum 서수로 싣고
		//     받는 쪽 허브가 저항을 빼서 실효 등급을 정한다 - 공격자는 대상의 저항을 모른다.
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		                                               VBGameplayTags::Data_HitImpact, static_cast<float>(SwingImpact));
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(
		                                               VBGameplayTags::Data_LaunchHeight, ActiveConfig->LaunchHeight);
		//     (E) 답 태그(design §11). 피격자 허브가 DodgeOnly 를 읽어 가드·패링을 건너뛴다.
		//     예고 큐가 색으로 한 «약속» 과 같은 자산 값이라 이행이 어긋날 수 없다. 스펙의 동적 태그는 복제되지 않지만
		//     허브 판정은 서버에서만 돈다.
		if (ActiveConfig->AttackAnswer.IsValid())
		{
			SpecHandle.Data.Get()->AddDynamicAssetTag(ActiveConfig->AttackAnswer);
		}
		// 겹침/무적/완전 방어만으로 카메라가 돌지 않게 실제 체력 또는 실드 감소를 확인한다.
		const float HealthBefore = TargetASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
		const float ShieldBefore = TargetASC->GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute());
		TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		const bool bDealtDamage = TargetASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()) < HealthBefore ||
			TargetASC->GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute()) < ShieldBefore;
		if (!IsSameSwing())
		{
			bSwingCut = true;
			break;
		}

		// 피해가 0 인 타격(패링·회피 무적)에는 추가 효과(출혈 등)를 걸지 않는다(피격 반응 갈래 판단 2026-09-30).
		//   가드 감쇠 타격은 피해가 0 보다 커서 여기서 못 가른다 - 가드 브레이크 단계(DEC-025 단계 3)에서 정한다.
		if (bDealtDamage)
		{
			ApplyAdditionalEffects(TargetASC, Hit);
			if (!IsSameSwing())
			{
				bSwingCut = true;
				break;
			}
		}

		// 자동 락온: 이 스윙이 처음 유효 데미지를 준 적으로 락 시도(서버 권위).
		// 적 아바타는 AVBCharacter 가 아니므로 Cast 실패 → 구조적 제외. 컴포넌트 getter 도 null-safe.
		// 시도 후(수락/거절 무관) 잠금 → 뒤따르는 다중 히트 타겟이 락을 훔치지 않게(결정론적 '첫 타겟').
		//   판정 창에서는 창의 첫 표적이다 - 뒤 프레임에 새로 맞는 표적도 락을 훔치지 않는다.
		if (!Context.bAutoLocked)
		{
			if (AVBCharacter* VBChar = Cast<AVBCharacter>(GetAvatarCharacter()))
			{
				if (UVBTargetLockComponent* TLC = VBChar->GetTargetLockComponent())
				{
					TLC->TryAutoLockOnTarget(HitActor);
				}
			}
			Context.bAutoLocked = true;
		}

		if (bDealtDamage && !Context.bCameraReported)
		{
			if (AVBCharacter* VBChar = Cast<AVBCharacter>(AvatarCharacter))
				if (UVBTargetLockComponent* TLC = VBChar->GetTargetLockComponent())
					TLC->NotifyMeleeHitForCamera(HitActor);
			Context.bCameraReported = true;
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
		VB_LOG(Log, "%s: 데미지 %.1f = AttackPower %.1f x cfg %.2f x variant %.2f x swing %.2f (대상 %d) / 선언 등급 %s",
		       *GetName(), FinalDamage, Context.AttackPower, ActiveConfig->DamageMultiplier,
		       ActiveVariantDamage, SwingScales.DamageScale, ProcessedActors.Num(), VBHitImpactName(SwingImpact));

		// 저작한 스윙에서만 1줄. 미저작이 정상 상태이므로 매 스윙 로그는 노이즈다.
		//  꽂았는데 이 줄이 안 뜨면 배선이 안 된 것 - 그것이 이 로그의 유일한 목적이다.
		if (const UVBSwingCombatProfile* CombatProfile = Context.CombatProfile.Get())
		{
			VB_LOG(Log, "%s: 스윙 배율 적용 profile=%s dmg=%.2f kbS=%.2f kbZ=%.2f hitstop=%.2f impact=%s",
			       *GetName(), *CombatProfile->GetName(),
			       SwingScales.DamageScale, SwingScales.KnockbackStrengthScale,
			       SwingScales.KnockbackZScale, SwingScales.HitStopDurationScale,
			       CombatProfile->bOverrideHitImpact ? VBHitImpactName(CombatProfile->HitImpact) : TEXT("상속"));
		}
	}

	// 히트스톱과 스윙 쉐이크는 스윙의 첫 접촉에서 한 번만 낸다(설계 7절).
	//   옛 구 스윕은 호출 한 번이 곧 스윙이라 종전과 같다. 판정 창에서 뒤 프레임에 새로 맞는 표적은 피해·반응만 받는다 -
	//   창 동안 여러 번 걸면 공격자가 멈췄다 풀렸다를 반복해 스윙이 끊겨 보인다. 뒤 표적도 멈출지는 체감 확인 뒤 정한다.
	if (ProcessedActors.Num() > 0 && !Context.bContactFinished && !bSwingCut)
	{
		Context.bContactFinished = true;

		// 히트스톱은 대상 수와 무관하게 1회만 — 루프 밖 유지. 맞은 액터 전부를 실어 «전투원만» 멈추게 한다(design §8).
		PlayHitStop(ActiveConfig->HitStopDuration * SwingScales.HitStopDurationScale,
		            ActiveConfig->HitStopTimeDilation, ProcessedActors.Array(), ActiveConfig);

		// 방향성 임팩트 쉐이크도 같은 이유로 루프 밖 1회다.
		// 파티클과 사운드는 맞은 대상마다 나야 옳지만 카메라는 대상 수와 무관하게 하나뿐이다.
		//  루프 안에서 쏘면 적 2체를 동시에 벨 때 쉐이크가 두 번 겹쳐 세기가 두 배가 된다
		//  — 지금 GCN_DamagePhysical BP 의 쉐이크 노드가 정확히 그 상태이고, 이 위치가 그것을 구조적으로 없앤다.
		if (const UVBSwingImpactProfile* Profile = Context.ImpactProfile.Get())
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

UVBMeleeTraceComponent* UVBGA_MeleeAttackBase::GetMeleeTraceComponent() const
{
	const AActor* Avatar = GetAvatarActorFromActorInfo();
	return Avatar ? Avatar->FindComponentByClass<UVBMeleeTraceComponent>() : nullptr;
}

void UVBGA_MeleeAttackBase::BeginTraceWindow(const FGameplayEventData& Payload)
{
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority())
	{
		return;
	}

	FGameplayTag Shape;
	for (const FGameplayTag& Tag : Payload.InstigatorTags)
	{
		if (Tag.MatchesTag(VBGameplayTags::Melee_Shape) && Tag != VBGameplayTags::Melee_Shape)
		{
			Shape = Tag;
			break;
		}
	}

	// 못 여는 경우는 옛 구 스윕으로 떨어진다. 몽타주는 창으로 바꿨는데 그 몽타주를 쓰는 캐릭터에 컴포넌트·모양이 아직 없을 때다
	//   (변환은 캐릭터별로 나눠 한다 - 설계 12절). 조용히 피해가 사라지면 «칼이 안 맞는다» 로만 보여 원인을 못 찾는다. 경고는 매 스윙 낸다.
	UVBMeleeTraceComponent* Trace = GetMeleeTraceComponent();
	if (!Trace || !Shape.IsValid())
	{
		VB_LOG(Warning, "%s: 판정 창인데 %s - 옛 구 판정으로 대신한다", *GetName(),
		       !Trace ? TEXT("아바타에 UVBMeleeTraceComponent 가 없다") : TEXT("노티에 모양 태그가 없다"));
		PerformAttackTrace();
		return;
	}

	FSwingHitContext Context;
	if (!BuildSwingHitContext(Context))
	{
		return;   // 옛 판정도 이 경우 조용히 끝났다 - 피해를 낼 수 없는 공격(피해 GE 없음 등)
	}

	const int32 Key = NextTraceWindowKey++;
	const int32 WindowId = Trace->BeginWindow(Shape, Payload.EventMagnitude,
	                                          FVBMeleeTraceHits::CreateUObject(this, &ThisClass::OnTraceWindowHits, Key));
	if (WindowId == INDEX_NONE)
	{
		// 컴포넌트가 이유를 이미 경고했다(모양 없음·뼈 없음).
		VB_LOG(Warning, "%s: 판정 창 %s 을 못 열었다 - 옛 구 판정으로 대신한다", *GetName(), *Shape.ToString());
		PerformAttackTrace();
		return;
	}

	FOpenTraceWindow& Open = OpenTraceWindows.AddDefaulted_GetRef();
	Open.Key      = Key;
	Open.WindowId = WindowId;
	Open.Shape    = Shape;
	Open.Context  = MoveTemp(Context);
	VB_LOG(Log, "%s: 판정 창 열림 - %d타 %s (창 %d, 다단 간격 %.2f초)", *GetName(), ComboIndex + 1, *Shape.ToString(), WindowId,
	       Payload.EventMagnitude);
}

void UVBGA_MeleeAttackBase::OnTraceWindowHits(const TArray<FHitResult>& Hits, int32 Key)
{
	FOpenTraceWindow* Open = OpenTraceWindows.FindByPredicate([Key](const FOpenTraceWindow& W) { return W.Key == Key; });
	if (!Open)
	{
		return;   // 이미 닫은 창
	}

	for (const FHitResult& Hit : Hits)
	{
		VB_LOG(Log, "%s: 판정 창 %d 접촉 - %s 의 %s", *GetName(), Open->WindowId, *GetNameSafe(Hit.GetActor()),
		       Hit.BoneName.IsNone() ? TEXT("(뼈 없음)") : *Hit.BoneName.ToString());
	}

	// 사본으로 적용하고 되돌려 쓴다. 적용 도중 이 GA 가 끝날 수 있다(예: 패링이 공격자를 끊음 -> EndAbility -> 창 목록이 비워짐).
	//   목록의 원소를 참조로 넘기면 그때 사라진 메모리를 쓴다.
	FSwingHitContext Context = Open->Context;
	ApplySwingHits(Hits, Context);
	if (FOpenTraceWindow* Still = OpenTraceWindows.FindByPredicate([Key](const FOpenTraceWindow& W) { return W.Key == Key; }))
	{
		Still->Context = Context;
	}
}

void UVBGA_MeleeAttackBase::EndTraceWindowByShape(FGameplayTag Shape, bool bSweepFinalPose)
{
	const int32 Index = OpenTraceWindows.IndexOfByPredicate([&Shape](const FOpenTraceWindow& W) { return W.Shape == Shape; });
	if (Index == INDEX_NONE)
	{
		return;   // 섹션 전환이 이미 닫았거나, 못 열어서 옛 판정으로 대신한 창
	}
	const int32 WindowId = OpenTraceWindows[Index].WindowId;
	const int32 Key = OpenTraceWindows[Index].Key;
	if (UVBMeleeTraceComponent* Trace = GetMeleeTraceComponent())
	{
		// 마지막 접촉의 피해 문맥은 콜백 동안 살아 있어야 한다. 패링으로 GA 가 끝나면 이 목록도 없어지므로 번호로 다시 찾는다.
		Trace->EndWindow(WindowId, bSweepFinalPose);
	}
	OpenTraceWindows.RemoveAll([Key](const FOpenTraceWindow& W) { return W.Key == Key; });
}

void UVBGA_MeleeAttackBase::EndAllTraceWindows()
{
	if (OpenTraceWindows.Num() == 0)
	{
		return;
	}
	// 목록을 먼저 비운다 - EndWindow 가 무엇을 부르든 이 GA 의 콜백이 옛 창을 다시 찾지 못하게.
	TArray<FOpenTraceWindow> Closing = MoveTemp(OpenTraceWindows);
	OpenTraceWindows.Reset();
	if (UVBMeleeTraceComponent* Trace = GetMeleeTraceComponent())
	{
		for (const FOpenTraceWindow& W : Closing)
		{
			Trace->EndWindow(W.WindowId);
		}
	}
}
