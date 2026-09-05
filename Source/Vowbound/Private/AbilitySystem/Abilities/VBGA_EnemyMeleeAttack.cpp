// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_EnemyMeleeAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "Animation/AnimMontage.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Data/VBEnemyConfig.h"
#include "Vowbound/Vowbound.h"

namespace
{
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
	UAbilityTask_WaitGameplayTagAdded* StaggerWatch =
			UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(
					this,
					VBGameplayTags::State_Combat_Staggered,
					/*InOptionalExternalTarget=*/nullptr,
					/*OnlyTriggerOnce=*/true);
	StaggerWatch->Added.AddDynamic(this, &UVBGA_EnemyMeleeAttack::OnStaggeredDuringAttack);
	StaggerWatch->ReadyForActivation();

	// 엔진 UAbilityTask_WaitGameplayTagAdded::Activate 는 태그가 이미 붙어 있으면 그 자리에서
	//   Added 를 브로드캐스트한다. 즉 위 한 줄이 OnStaggeredDuringAttack -> EndAbility 까지 동기로 끝냈을 수 있다.
	//   그 뒤로 진행하면 끝난 어빌리티가 스윙을 예약하거나 태스크를 더 만든다(그 태스크는 이미 지나간
	//   ActiveTasks 정리에 안 걸려 고아가 된다). 그래서 여기서 한 번 끊는다.
	if (!IsActive())
	{
		return;
	}

	if (Config->TelegraphDuration <= 0.0f)
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

		// 예고 시작 에지. 비주얼/사운드/경고 UI 가 붙는 훅이다 - 이 GA 는 이벤트만 쏘고 연출은 자산이 한다.
		// 주의: 이 이벤트는 서버 로컬이다. UAbilitySystemComponent::HandleGameplayEvent 는 트리거 순회와
		//   콜백 브로드캐스트뿐이라 RPC 가 없다. 멀티플레이어에서 원격 클라 연출을 붙일 자리는 이 이벤트가
		//   아니라 바로 위에서 복제시킨 State.Combat.Telegraphing 태그 또는 GameplayCue 다.
		FGameplayEventData TelegraphEvent;
		TelegraphEvent.EventTag   = VBGameplayTags::Event_Combat_TelegraphStart;
		TelegraphEvent.Instigator = Avatar;
		TelegraphEvent.Target     = Avatar;
		OwnerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_TelegraphStart, &TelegraphEvent);
	}

	if (Config->TelegraphMontage)
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
				                                                               Config->TelegraphMontage,
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
			UAbilityTask_WaitDelay::WaitDelay(this, Config->TelegraphDuration);
	TelegraphDelay->OnFinish.AddDynamic(this, &UVBGA_EnemyMeleeAttack::BeginSwing);
	TelegraphDelay->ReadyForActivation();

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

void UVBGA_EnemyMeleeAttack::BeginSwing()
{
	if (bGrantedTelegraph)
	{
		if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
		{
			OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing, 1,
			                                 EGameplayTagReplicationState::CountToOwner);
		}
		bGrantedTelegraph = false;
	}

	// 베이스의 본 스윙(무브셋 해석 -> 몽타주 -> 트레이스 노티 -> GE)을 현재 컨텍스트로 진입시킨다.
	// TriggerEventData 를 nullptr 로 넘기는 것은 정확하다 - 베이스는 그 인자를 쓰지 않는다.
	Super::ExecuteAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, nullptr);
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

	if (bGrantedTelegraph)
	{
		if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
		{
			OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing, 1,
			                                 EGameplayTagReplicationState::CountToOwner);
		}
		bGrantedTelegraph = false;
	}

	// 베이스가 뚝심(SuperArmor) 로스태그를 정리한다.
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
