// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBGA_EnemyMeleeAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h" // IsDowned - 다운 반격 판별
#include "Data/VBAttackConfig.h"   // PeekAttackConfig()->AttackAnswer (예고 큐의 답 태그)
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"   // ResolveStrideScale - 보폭 배율의 홈
#include "Animation/VBRootMotionModifier_ScaledSkewWarp.h" // 배율을 아는 워프인지 검사(design .sw/boss/design.md §17)
#include "GameplayEffectTypes.h"   // FGameplayCueParameters
#include "Vowbound/Vowbound.h"

namespace
{
	// 보폭 배율. 적이 아니면 1.0 - 이 GA 는 적 전용이지만 아바타 타입을 가정하지 않는다. 홈은 AVBEnemyBase::ResolveStrideScale 이다(베이스 GA 도 같은 것을 쓴다).
	float ResolveStrideScale(const ACharacter* Avatar)
	{
		return AVBEnemyBase::ResolveStrideScale(Avatar);
	}

	// 이 아바타의 아키타입 튜닝. 컴포넌트를 경유하는 이유는 그것이 적의 전투용 조회 창구이고,
	// 미할당/비-적 액터에서도 CDO 를 돌려줘 절대 null 이 아니기 때문이다.
	const UVBEnemyConfig* ResolveEnemyConfig(const AActor* Avatar)
	{
		if (Avatar)
		{
			if (const UVBEnemyCombatComponent* CombatComponent = Avatar->FindComponentByClass<UVBEnemyCombatComponent>())
			{
				return CombatComponent->GetCombatConfig();
			}
		}
		return GetDefault<UVBEnemyConfig>();
	}
}

UVBGA_EnemyMeleeAttack::UVBGA_EnemyMeleeAttack()
{
	// 아래 셋은 이 GA 가 UVBGA_LightAttack 이 아니게 되면서 잃는 것들을 되살린다.
	// (적 공격 BP 는 종전에 UVBGA_LightAttack 파생이라 그 생성자에서 이 값들을 받고 있었다.)

	// BT 태스크가 이 태그로 스펙을 찾는다. 비면 "매칭 Ability 없음" Warning 후 적이 영영 안 때린다.
	// BP 에서 덮어쓸 수 있는 디폴트다 - 아키타입별로 갈리면 UVBEnemyConfig::AttackAbilityInputTag 와 함께 바꾼다.
	InputTag = VBGameplayTags::Input_Attack_Light;

	// 이 GA 의 식별자. 경직(UVBGA_Stagger)의 BlockAbilitiesWithTag 가 매칭할 대상이며,
	// 없으면 아무 로그 없이 무음 실패한다(적도 MaxPoise 를 저작하므로 적 경직이 실재한다).
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VBGameplayTags::Ability_Attack_Light);
	SetAssetTags(AssetTags);

	// 이 태그가 임대 시계의 양끝을 모두 만든다. 상승 에지가 NotifyAttackStarted 로 AttackStartedTime 을
	//   찍고, 하강 에지가 ReleaseAttackToken 을 부른다(UVBEnemyCombatComponent::OnAttackingTagChanged).
	// 이 줄이 빠지면 두 에지가 다 사라진다. 그러면 토큰이 반납되지 않는 데서 그치지 않고,
	//   MaxTokenHoldSeconds(4초) 만료 안전망이 이 멤버를 아예 보지 못한다 - 그 스윕은
	//   AttackStartedTime >= 0 인 임대만 훑기 때문이다(공격 구간을 재려고 그렇게 만들었다).
	// 남는 구제는 접근 구간 회수(MaxApproachHoldSeconds)뿐이고 그마저 넘겨받을 후보가 있을 때만 돈다.
	//   즉 혼자 붙은 적은 영원히 토큰을 쥔다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);

	// 사망 게이트. CancelAllAbilities 는 이미 활성인 GA 만 끄고 신규 활성화는 못 막는다.
	// 적 BT 는 DeathCleanupDelay 동안 계속 돌므로 이 게이트가 없으면 시체가 스윙한다(FIND-067 과 같은 계열).
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);

	// 보스 페이즈 전환 게이트. BT 의 데코레이터가 이동·공격 분기를 닫지만 그것은 다음 틱이고, 코디네이터가 그 사이에
	//   토큰을 주면 태스크가 이 GA 를 켠다. 잡몹은 이 태그를 절대 갖지 않으므로 종전과 같다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_PhaseTransition);

	// 조우 보류 게이트(시네마틱 장면 동안 - .sw/cinematic-playback 10절 4). 행동 트리는 멈추지만, 멈추기 직전에 토큰을 받은
	//   태스크가 이 GA 를 켤 수 있어 같은 이유로 여기서도 막는다. 붙이는 쪽은 UVBEncounterGateComponent 하나다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Encounter_Held);
}

void UVBGA_EnemyMeleeAttack::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                            const FGameplayAbilityActorInfo*     ActorInfo,
                                            const FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData*            TriggerEventData)
{
	// Super 를 여기서 부르지 않는다 - 베이스는 진입 즉시 공격 몽타주를 재생하기 때문이다.
	// 예고가 끝난 뒤 BeginSwing 이 부른다.
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const UVBEnemyConfig* Config = ResolveEnemyConfig(Avatar);

	// Poise 파탄 경직 감시. State.Combat.Staggered 는 UVBGA_Stagger 만 부여하므로,
	// 이 태그가 붙는 즉시 공격을 끝내면 일반 히트리액트와 무관하게 Poise 판단만 따른다.
	// 왜 맨 앞인가: 이것은 예고 기능의 일부가 아니라 "적이 경직을 뚫고 정상 타격한다"(2026-08-20 감사 C2)를
	//   막는 안전망이다. 예고 분기 뒤에 두면 TelegraphDuration=0 인 아키타입에서 통째로 사라져
	//   자산 값 하나로 그 결함이 되살아난다. 안전망은 그것이 지키려는 조건보다 먼저 서야 한다.
	// 왜 몽타주 중단에 기대지 않는가: 예고 몽타주는 미할당일 수 있고(DA_Enemy_Default 가 실제로 그렇다),
	//   경직 몽타주도 해석에 실패하면 재생되지 않는다. 두 경우 모두 덮을 몽타주가 없다. 태그는 온다.
	// 왜 아키타입 노브로 빼지 않는가: "이 적은 한 대로는 안 끊긴다"의 홈은 이미 MaxPoise 다.
	//   태그 컨테이너로 한 번 더 표현하면 같은 판단의 홈이 둘이 되고 서로를 조용히 상쇄한다.
	// 엎드린 채 휘두르는 반격(design .sw/boss/design.md §14.3)은 Staggered 가 «전제» 다 - UVBGA_Knockdown 이 그 태그를 든 채 이 GA 를 켠다.
	//   그때 이 감시를 걸면 엔진이 이미 붙은 태그를 그 자리에서 알려(아래 IsActive 주석) 반격이 시작 프레임에 끝난다.
	//   판별은 Downed 다 - Staggered 만 보면 «경직 중에 켜진 공격» 과 «다운 반격» 이 안 갈린다. 잡몹은 이 태그를 절대 갖지 않는다.
	const UAbilitySystemComponent* GateASC      = GetAbilitySystemComponentFromActorInfo();
	const bool                     bDownCounter = UVBAbilitySystemStatics::IsDowned(GateASC);
	if (!bDownCounter)
	{
		UAbilityTask_WaitGameplayTagAdded* StaggerWatch =
				UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(
						this,
						VBGameplayTags::State_Combat_Staggered,
						/*InOptionalExternalTarget=*/nullptr,
						/*OnlyTriggerOnce=*/true);
		StaggerWatch->Added.AddDynamic(this, &UVBGA_EnemyMeleeAttack::OnStaggeredDuringAttack);
		StaggerWatch->ReadyForActivation();
	}

	// 이 활성화가 쓸 공격 설정. 스윙 전이라 ActiveConfig 는 아직 직전 값이다 - PeekAttackConfig 가 그 자리다.
	//   예고 길이·몽타주는 «공격 값이 있으면 공격, 없으면 아키타입»(design .sw/enemy-soldier/design.md §3.6).
	const UVBAttackConfig* Pending = PeekAttackConfig();
	// 계측: 예고 시작 시점의 대상 거리. 스윙 시점([워프 대역] 의 «거리»)과의 차이가 «예고 동안 누가 움직였나» 를 가른다.
	if (const UVBEnemyCombatComponent* CombatForLog = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr)
	{
		if (const AActor* LogTarget = CombatForLog->GetCombatTarget())
		{
			VB_LOG(Log, "[예고 시작] %s 거리 %.0f", *GetNameSafe(Avatar), FVector::Dist2D(Avatar->GetActorLocation(), LogTarget->GetActorLocation()));
		}
	}
	const float TelegraphDuration = (Pending && Pending->TelegraphDurationOverride >= 0.0f)
		                                ? Pending->TelegraphDurationOverride : Config->TelegraphDuration;
	UAnimMontage* TelegraphMontage = (Pending && Pending->TelegraphMontage) ? Pending->TelegraphMontage.Get() : Config->TelegraphMontage.Get();

	// 슈퍼아머 공격은 예고부터 슈퍼아머가 선다(design §3.5). 베이스는 스윙 진입에서 같은 함수를 부르고 래치가 두 번째를 무시한다.
	//   이 줄이 아래 HitReacting 감시보다 먼저인 이유: 슈퍼아머가 서면 히트리액트가 안 켜져 그 감시는 영영 안 울린다 - 그것이 «예외» 의 표현이다.
	if (Pending && Pending->bSuperArmorDuringAttack)
	{
		GrantSuperArmorOnce();
	}

	// 규칙 A: 맞으면 예고도 끊긴다. 참고작(오디세이·발할라·GoW·시푸) 공통 - 잡몹은 맞으면 끊기고, 예외는 표시된다.
	UAbilityTask_WaitGameplayTagAdded* HitWatch =
			UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(
					this,
					VBGameplayTags::State_Combat_HitReacting,
					/*InOptionalExternalTarget=*/nullptr,
					/*OnlyTriggerOnce=*/true);
	HitWatch->Added.AddDynamic(this, &UVBGA_EnemyMeleeAttack::OnHitDuringAttack);
	HitWatch->ReadyForActivation();

	// 엔진 UAbilityTask_WaitGameplayTagAdded::Activate 는 태그가 이미 붙어 있으면 그 자리에서
	//   Added 를 브로드캐스트한다. 즉 위 감시가 OnStaggeredDuringAttack -> EndAbility 까지 동기로 끝냈을 수 있다.
	//   그 뒤로 진행하면 끝난 어빌리티가 스윙을 예약하거나 태스크를 더 만든다(그 태스크는 이미 지나간
	//   ActiveTasks 정리에 안 걸려 고아가 된다). 그래서 여기서 한 번 끊는다.
	if (!IsActive())
	{
		return;
	}

	if (TelegraphDuration <= 0.0f)
	{
		// 예고 없음. 예고 구간만 사라지고 스윙은 종전(베이스) 동작 그대로다 - 되돌리기 노브가 자산 값 하나인 이유.
		// 경직 감시는 위에서 이미 걸었으므로 이 경로도 경직에 끊긴다. 되돌리기가 안전망까지 되돌리지는 않는다.
		BeginSwing();
		return;
	}

	UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (OwnerASC)
	{
		// 복제 상태를 명시한다. 생략하면 EGameplayTagReplicationState::None 이 되어
		// 원격 클라가 이 상태를 영영 못 보고, 나중에 예고 반응(UI/애님)을 붙일 때 조용히 안 뜬다.
		OwnerASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing, 1,
		                              EGameplayTagReplicationState::CountToOwner);
		bGrantedTelegraph = true;

		// 예고 큐 (design §11): 색·소리·화면 밖 경고의 홈. Add/Remove 큐라 예고 수명과 같이 살고 멀티캐스트된다.
		//   RawMagnitude = 예고 길이(클라는 UVBEnemyConfig 를 모른다), AggregatedSourceTags = 답 태그(공격 자산의 성질).
		//   답을 스윙 진입 전에 읽어야 해서 PeekAttackConfig 를 쓴다 - ActiveConfig 는 아직 직전 활성화 값이다.
		{
			FGameplayCueParameters CueParams;
			CueParams.Instigator   = Avatar;
			CueParams.RawMagnitude = TelegraphDuration;   // 해석된 길이 - 슈퍼아머 반격의 긴 예고까지 빨강이 간다
			if (Pending)
			{
				CueParams.SourceObject = Pending;
				if (Pending->AttackAnswer.IsValid())
				{
					CueParams.AggregatedSourceTags.AddTag(Pending->AttackAnswer);
				}
			}
			OwnerASC->AddGameplayCue(VBGameplayTags::GameplayCue_Combat_Telegraph, CueParams);
		}

		// 예고 시작 에지. 서버 쪽 훅(회피 보조 등)이 붙는 자리다.
		// 주의: 이 이벤트는 서버 로컬이다. UAbilitySystemComponent::HandleGameplayEvent 는 트리거 순회와
		//   콜백 브로드캐스트뿐이라 RPC 가 없다. 클라 연출은 위의 GameplayCue 가 맡는다(2026-09-06 부터 실제로 그렇다).
		FGameplayEventData TelegraphEvent;
		TelegraphEvent.EventTag   = VBGameplayTags::Event_Combat_TelegraphStart;
		TelegraphEvent.Instigator = Avatar;
		TelegraphEvent.Target     = Avatar;
		OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_TelegraphStart, &TelegraphEvent);
	}

	if (TelegraphMontage)
	{
		// 델리게이트를 걸지 않는다. 타이밍의 주인은 TelegraphDuration 을 재는 아래 UAbilityTask_WaitDelay 이고
		//   이 클립은 순수 연출이다 - 클립 종료로 스윙을 시작하면 타이밍의 홈이 수치와 클립 둘이 된다.
		// 재생속도를 1.0 고정으로 두는 것도 같은 이유다. 이 클립은 어차피 TelegraphDuration 에 잘리므로
		//   재생속도 노브를 두면 예고 길이를 정하는 두 번째 값이 생긴다(1.0 은 항등원이지 튜닝값이 아니다).
		// bStopWhenAbilityEnds=true 라 취소되면 이 클립도 함께 멎는다.
		UAbilityTask_PlayMontageAndWait* TelegraphTask =
				UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				                                                               this,
				                                                               NAME_None,
				                                                               TelegraphMontage,
				                                                               1.0f,
				                                                               NAME_None,
				                                                               true,
				                                                               1.0f,
				                                                               0.0f,
				                                                               false);
		TelegraphTask->ReadyForActivation();
	}

	if (!GetWorld())
	{
		// 월드가 없으면 어빌리티 태스크의 시간 대기가 성립하지 않아 이 활성화가 스윙 없이 매달린다.
		// 즉시 스윙으로 떨어뜨린다(종전 폴백 동작 그대로).
		VB_LOG(Warning, "%s: 월드 없음 -> 예고 태스크를 걸 수 없다. 즉시 스윙으로 폴백", *GetName());
		BeginSwing();
		return;
	}

	// 예고 지연. raw 월드 타이머 대신 어빌리티 태스크를 쓴다 - 어빌리티가 끝나면 엔진이 함께 끝낸다
	//   (UGameplayAbility::EndAbility 가 ActiveTasks 를 순회해 TaskOwnerEnded 를 부른다).
	//   수동 ClearTimer 와 FTimerHandle 멤버가 함께 사라져, '지우기를 빠뜨리면 끝난 어빌리티가
	//   뒤늦게 스윙한다'는 실수 가능성이 타입 수준에서 없어진다.
	// 게임 시간 기준인 것은 종전과 같다 - 히트스톱 중에는 예고 창도 함께 늘어나야 옳다.
	UAbilityTask_WaitDelay* TelegraphDelay =
			UAbilityTask_WaitDelay::WaitDelay(this, TelegraphDuration);
	TelegraphDelay->OnFinish.AddDynamic(this, &UVBGA_EnemyMeleeAttack::BeginSwing);
	TelegraphDelay->ReadyForActivation();

}

void UVBGA_EnemyMeleeAttack::OnHitDuringAttack()
{
	// 예고 중이면 «맞으면 끊긴다». 스윙에 들어간 뒤 오는 피격은 히트리액트 몽타주가 이미 공격 몽타주를 멈춰 Interrupted 로
	//   끝나는 중이라 여기서 한 번 더 끝내도 무해하지만, 로그가 이중으로 남지 않게 예고 구간만 말한다.
	if (bGrantedTelegraph)
	{
		VB_LOG(Log, "[Telegraph] %s 예고 끊김(피격)", *GetNameSafe(GetAvatarActorFromActorInfo()));
	}
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
	           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
}

void UVBGA_EnemyMeleeAttack::OnStaggeredDuringAttack()
{
	VB_LOG(Log, "%s: 경직 -> 공격 취소", *GetName());
	// bWasCancelled=true 로 끝낸다. 여기 하나로 전부 수렴한다 - 엔진이 ActivationOwnedTags 의
	// State.Combat.Attacking 을 제거하고, 그 하강 에지를 UVBEnemyCombatComponent 가 받아 토큰을 반납한다.
	// BT 태스크는 bSucceedOnCancel=true 라 Sequence 를 정상 종료시켜 트리가 재탐색한다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo,
	           /*bReplicateEndAbility=*/true, /*bWasCancelled=*/true);
}

void UVBGA_EnemyMeleeAttack::ReleaseTelegraph()
{
	if (!bGrantedTelegraph)
	{
		return;
	}
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing, 1,
		                                 EGameplayTagReplicationState::CountToOwner);
		// 큐 Remove -> 각 클라 OnRemove(발광 정지·위협 등록 해제). 취소로 들어와도 같은 길이라 «예고 색이 남는» 경우가 없다.
		OwnerASC->RemoveGameplayCue(VBGameplayTags::GameplayCue_Combat_Telegraph);
	}
	bGrantedTelegraph = false;
}

void UVBGA_EnemyMeleeAttack::BeginSwing()
{
	ReleaseTelegraph();

	// 베이스의 본 스윙(무브셋 해석 -> 몽타주 -> 트레이스 노티 -> GE)을 현재 컨텍스트로 진입시킨다.
	// TriggerEventData 를 nullptr 로 넘기는 것은 정확하다 - 베이스는 그 인자를 쓰지 않는다.
	Super::ExecuteAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, nullptr);

	// 베이스가 세운 것은 플레이어 경로(락온)라 적에서는 비어 있다. 몽타주가 막 시작된 지금 전투 타겟으로 덮어 세운다.
	SetWarpTargetToCombatTarget();
}

void UVBGA_EnemyMeleeAttack::SetWarpTargetToCombatTarget()
{
	const ACharacter* Avatar = GetAvatarCharacter();
	const UVBEnemyCombatComponent* Combat = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr;
	const AActor* Target = Combat ? Combat->GetCombatTarget() : nullptr;
	if (Target && Target->GetRootComponent())
	{
		// 워프 «대역» (design .sw/boss/design.md §12.6): 클립이 원래 가는 거리를 허용 압축비 안에서만 늘이거나 줄인다.
		//   변형 선택이 거리에 맞는 클립을 고른 뒤라 보통은 대역 안이고, 밖이면(플레이어가 붙었거나 물러난 뒤) 목적지를 경계에
		//   세워 «못 닿을지언정 미끄러지지는 않는다» 를 택한다. 경계 안쪽 하한은 캡슐 합 - 그 안으로 세우면 CMC 가 밀어내 떨린다.
		const USceneComponent* Root = Target->GetRootComponent();
		const float Distance = FVector::Dist2D(Avatar->GetActorLocation(), Target->GetActorLocation());
		const float Offset0  = ComputeApproachOffset(Root);
		const float Needed   = FMath::Max(0.0f, Distance - Offset0);
		// 배율의 홈은 AVBEnemyBase::GetStrideScale 하나다 - 0/음수 메시 배율 가드를 거기가 들고 있어서,
		//   여기서 ComponentScale 을 다시 유도하면 계획(그 홈을 쓴다)과 스윙이 갈린다.
		const float MeshScale = ResolveStrideScale(Avatar);
		const float Lunge    = MeasureMontageLungeCm(GetActiveMontage(), MeshScale);
#if !UE_BUILD_SHIPPING
		// 몸 배율이 1 이 아닌데 이 몽타주의 워프가 배율을 모르면, 아래 대역이 맞아도 창 안에서 몸이 발보다 먼저 미끄러진다
		//   (design .sw/boss/design.md §17). 자산 설정이라 여기서 막을 수 없으니 말한다 - 몽타주당 한 번.
		if (!FMath::IsNearlyEqual(MeshScale, 1.0f, 1.0e-3f))
		{
			const UAnimMontage*             Montage = GetActiveMontage();
			const URootMotionModifier_Warp* Warp    = FindApproachWarpModifier(Montage);
			static TSet<FName> WarnedMontages;
			if (Warp && !Warp->IsA<UVBRootMotionModifier_ScaledSkewWarp>() && !WarnedMontages.Contains(Montage->GetFName()))
			{
				WarnedMontages.Add(Montage->GetFName());
				VB_LOG(Warning, "[워프 배율] %s 배율 %.2f 인데 %s 의 접근 워프가 %s 다 - 배율을 두 번 곱해 미끄러진다. "
				                "노티 모디파이어를 «Skew Warp (메시 배율 보정)» 으로 바꿔라",
				       *GetNameSafe(Avatar), MeshScale, *GetNameSafe(Montage), *Warp->GetClass()->GetName());
			}
		}
#endif
		const float Ratio    = FMath::Max(1.0f, ResolveEnemyConfig(Avatar)->WarpLungeToleranceRatio);
		float Travel = Needed;
		if (Lunge <= 1.0f)
		{
			Travel = 0.0f; // 제자리 클립 - 회전만 맞춘다
		}
		else
		{
			Travel = FMath::Clamp(Needed, Lunge / Ratio, Lunge * Ratio);
		}
		float Offset = Distance - Travel;
		Offset = FMath::Max(Offset, FMath::Min(ComputeMinSeparation(Root), Distance));
		VB_LOG(Log, "[워프 대역] %s 거리 %.0f 필요 %.0f 전진 %.0f 허용 %.0f~%.0f -> 이동 %.0f (오프셋 %.0f, 고정 목표)",
		       *GetNameSafe(Avatar), Distance, Needed, Lunge, Lunge / Ratio, Lunge * Ratio, Distance - Offset, Offset);
		// 목표를 스윙 시작의 «한 점» 에 박는다 - 플레이어를 따라가지 않는다(2026-09-30 user 「엄청 멀리서 미끄러지면서 오다가 공격을 맞춘다」
		//   「앞 거리를 꼭 맞출 게 아니라 뚫지만 않으면 된다」). 따라가게 두면 휘두르는 도중 플레이어가 물러날 때 목표도 같은 오프셋으로 물러나,
		//   위 대역 계산을 무시하고 클립보다 훨씬 멀리 끌고 갔다. 위 로그의 «이동» 은 시작 순간 값이라 그 끌림이 찍히지 않았다.
		//   추종은 플레이어 공격의 몫으로 남긴다(움직이는 적을 따라 베는 것이 그쪽 의도다). 목적지 write 의 홈은 베이스 한 곳이다.
		ApplyApproachWarpWithOffset(Root, Offset, /*bFollowTarget=*/false);
	}
	else
	{
		// 타겟이 없으면 구간이 목표 없이 남는다 - 그때 SkewWarp 는 아무것도 안 하므로 생 루트모션이 돈다. 지워서 그 상태를 명시한다.
		ClearWarpTarget();
	}
}

void UVBGA_EnemyMeleeAttack::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                                        const FGameplayAbilityActorInfo*     ActorInfo,
                                        const FGameplayAbilityActivationInfo ActivationInfo,
                                        bool                                 bReplicateEndAbility,
                                        bool                                 bWasCancelled)
{
	// 예고 지연을 어빌리티 태스크로 옮긴 뒤로 여기서 지울 타이머가 없다.
	// 엔진 UGameplayAbility::EndAbility 가 ActiveTasks 를 순회해 태스크를 함께 끝내므로,
	// '지우기를 빠뜨리면 끝난 어빌리티가 뒤늦게 스윙한다'는 실수 자체가 표현 불가능해졌다.

	ReleaseTelegraph();

	// 베이스가 슈퍼아머(SuperArmor) 로스태그를 정리한다.
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

int32 UVBGA_EnemyMeleeAttack::SelectVariantIndex(const UVBAttackConfig& Leaf, int32 VariantCount)
{
	if (VariantCount <= 1)
	{
		return Super::SelectVariantIndex(Leaf, VariantCount);
	}
	const ACharacter* Avatar = GetAvatarCharacter();
	UVBEnemyCombatComponent* Combat = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr;
	const AActor* Target = Combat ? Combat->GetCombatTarget() : nullptr;
	if (!Target || !Target->GetRootComponent())
	{
		return Super::SelectVariantIndex(Leaf, VariantCount);
	}

	// 좌우 필터 (design .sw/boss/design.md §14.3). Side 가 Any 아닌 변형이 하나라도 있으면 표적이 있는 쪽의 변형만 후보다.
	//   표적 쪽 = 공격자 로컬 Y 부호(VBComputeHitDirection 과 같은 규약: +Y 가 오른쪽). 맞는 쪽이 하나도 없으면 전부 허용하고 소리낸다 -
	//   «반대쪽 팔을 휘두른다» 가 «휘두르지 않는다» 보다는 낫고, 자산이 한쪽만 채워진 것을 로그가 알려 준다. Any 만 있는 자산은 종전과 같다.
	TArray<bool> Allowed;
	Allowed.Init(true, VariantCount);
	{
		bool bAnySided = false;
		for (int32 Index = 0; Index < VariantCount && !bAnySided; ++Index)
		{
			bAnySided = Leaf.ResolveMontageVariant(Index).Side != EVBAttackSide::Any;
		}
		if (bAnySided)
		{
			const float         LocalY     = Avatar->GetActorTransform().InverseTransformPositionNoScale(Target->GetActorLocation()).Y;
			const EVBAttackSide TargetSide = LocalY >= 0.0f ? EVBAttackSide::Right : EVBAttackSide::Left;
			int32               AllowedCount = 0;
			for (int32 Index = 0; Index < VariantCount; ++Index)
			{
				const EVBAttackSide Side = Leaf.ResolveMontageVariant(Index).Side;
				Allowed[Index] = Side == EVBAttackSide::Any || Side == TargetSide;
				AllowedCount += Allowed[Index] ? 1 : 0;
			}
			if (AllowedCount == 0)
			{
				VB_LOG(Warning, "[변형] %s 표적이 %s 쪽인데 그쪽 변형이 없다 - 전부 허용. %s 의 Side 저작을 보라",
				       *GetNameSafe(Avatar), TargetSide == EVBAttackSide::Right ? TEXT("오른") : TEXT("왼"), *GetNameSafe(&Leaf));
				Allowed.Init(true, VariantCount);
			}
		}
	}

	// 변형마다 «가야 하는 거리» 가 다르다 - 멈추는 거리가 변형별이라서(design .sw/boss/design.md §12.8). 멈춤은 워프가 쓰는 것과
	//   같은 사슬(ComputeApproachOffset)로 잰다. 전진은 자산에서 직접(MeasureMontageLungeCm, 메시 배율 반영).
	const USceneComponent* Root      = Target->GetRootComponent();
	const float            Distance  = FVector::Dist2D(Avatar->GetActorLocation(), Target->GetActorLocation());
	const float            MeshScale = ResolveStrideScale(Avatar);
	const float            Ratio     = FMath::Max(1.0f, ResolveEnemyConfig(Avatar)->WarpLungeToleranceRatio);

	TArray<float> Lunges;
	TArray<float> Needed;
	FString       Candidates;
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		const FVBAttackMontageVariant Variant = Leaf.ResolveMontageVariant(Index);
		Lunges.Add(MeasureMontageLungeCm(Variant.Montage, MeshScale));
		Needed.Add(FMath::Max(0.0f, Distance - ComputeApproachOffset(Root, Leaf.GetVariantApproachOffset(Index, MeshScale))));
		Candidates += FString::Printf(TEXT("%s%s %.0f/%.0f"), Index ? TEXT(" / ") : TEXT(""), *GetNameSafe(Variant.Montage), Lunges[Index], Needed[Index]);
	}
	// 대역 안 = 그 클립을 허용비보다 더 누르지도(전진/허용비 <= 필요) 더 늘이지도(필요 <= 전진 x 허용비) 않고 닿는다.
	//   위쪽 경계가 없으면 제자리 클립(전진 3cm)이 언제나 «안» 이 되어 4m 밖에서 헛친다(2026-09-17 첫 PIE: 413cm 에서 04 를 골라 이동 4).
	auto InBand = [&Lunges, &Needed, Ratio](int32 Index)
	{
		return Lunges[Index] / Ratio <= Needed[Index] && Needed[Index] <= Lunges[Index] * Ratio;
	};
	// 대역 밖일 때의 «덜 틀린 정도» = |ln(전진/필요)|. 누름과 늘임을 같은 자로 잰다(2배 누름 = 절반 늘임). +1 은 0 나눗셈 방지.
	auto Distortion = [&Lunges, &Needed](int32 Index)
	{
		return FMath::Abs(FMath::Loge((Lunges[Index] + 1.0f) / (Needed[Index] + 1.0f)));
	};

	// 토큰을 받을 때 고른 공격(§12.7)이 지금 거리에서도 대역 안이면 그대로 - 접근 정지 거리가 그 클립에 맞춰져 있어 워프는 잔차만.
	//   예고 동안 플레이어가 걸어 들어와 대역 밖이면 기각하고 아래에서 다시 고른다(눌러서 미끄러지느니 다른 클립).
	if (Combat)
	{
		const int32 Planned = Combat->ConsumePlannedVariantIndex();
		if (Planned != INDEX_NONE && Planned < VariantCount && Allowed[Planned])
		{
			if (InBand(Planned))
			{
				VB_LOG(Log, "[변형] %s 계획된 %d번 사용 (필요 %.0f, 전진 %.0f)", *GetNameSafe(Avatar), Planned, Needed[Planned], Lunges[Planned]);
				return Planned;
			}
			VB_LOG(Log, "[변형] %s 계획된 %d번 기각 - 필요 %.0f 가 허용 %.0f~%.0f 밖 (예고 중 거리가 바뀌었다) -> 다시 고른다",
			       *GetNameSafe(Avatar), Planned, Needed[Planned], Lunges[Planned] / Ratio, Lunges[Planned] * Ratio);
		}
	}

	// §12.8: 대역 안에 드는 변형 중 전진이 가장 긴 것 - 닿고도 안 미끄러지는 것 중 큰 공격. 하나도 없으면 «덜 틀린» 것 -
	//   플레이어가 바짝 붙었으면 가장 짧은 클립(그래도 눌린다), 멀면 가장 긴 클립(그래도 늘어난다). 워프 대역이 목적지를 경계에
	//   세우므로 눌림·늘임은 허용비에서 멈추고 나머지는 헛침이다. 종전 «|전진 - 필요| 최소» 는 압축 2.63 을 골랐다(2026-09-16 밤).
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < VariantCount; ++Index)
	{
		if (Allowed[Index] && InBand(Index) && (Best == INDEX_NONE || Lunges[Index] > Lunges[Best]))
		{
			Best = Index;
		}
	}
	const bool bInBand = Best != INDEX_NONE;
	if (!bInBand)
	{
		// 좌우 필터가 최소 하나는 남기므로(위) 여기서 Best 가 INDEX_NONE 으로 끝나는 길은 없다.
		for (int32 Index = 0; Index < VariantCount; ++Index)
		{
			if (Allowed[Index] && (Best == INDEX_NONE || Distortion(Index) < Distortion(Best)))
			{
				Best = Index;
			}
		}
	}
	// 압축 = 클립이 가는 거리 / 가야 하는 거리. 1 에 가까울수록 워프가 할 일이 없고 발이 안 미끄러진다. 후보 항목 = 전진/필요.
	VB_LOG(Log, "[변형] %s 거리 %.0f -> %d번 (전진 %.0f, 압축 %.2f, %s) 후보 [%s]",
	       *GetNameSafe(Avatar), Distance, Best, Lunges[Best], Needed[Best] > 1.0f ? Lunges[Best] / Needed[Best] : 0.0f,
	       bInBand ? TEXT("대역 안") : TEXT("대역 밖-덜 틀린 것"), *Candidates);
	return Best;
}
