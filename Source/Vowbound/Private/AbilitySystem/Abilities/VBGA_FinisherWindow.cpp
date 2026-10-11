// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_FinisherWindow.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h" // ShouldReplicateActivationOwnedTags - 창 태그를 엔진과 같은 복제 설정으로 붙이고 뗀다
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBFinisherComponent.h"
#include "Data/VBFinisherWindowConfig.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Vowbound/Vowbound.h"

UVBGA_FinisherWindow::UVBGA_FinisherWindow()
{
	// 입력 GA 가 아니다. 활성화는 아래 트리거로만 온다.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Combat_FinisherWindow;
	AbilityTriggers.Add(Trigger);

	// 이 GA 가 처형 창 태그의 단독 생산자다. 공격자(UVBGA_Execute)는 대상의 이 태그 하나만 보고 처형 가능 여부를
	//   판정한다 - 체력을 다시 계산하지 않는다.
	// ActivationOwnedTags 로 두지 않는다(2026-09-29) - 켜지는 순간이 아니라 반응이 끝나 창이 «열리는» 순간(OpenWindow)에
	//   붙이고 EndAbility 에서 뗀다. 켜지자마자 붙이면 넘어져 있는 대기 중에 처형이 걸린다(헤더 주석).
	// 대신 «켜져 있다» 는 따로 드러낸다(대기 + 열림). 경직 GA 가 이것을 보고 기절 게이지 소진을 거절한다 - 창 태그만 보던 시절에는
	//   대기 구간에 경직이 들어와 마무리 타의 반응을 끊고 0.1초 뒤 창에 끊겼다(2026-09-29 PIE 로그 13:32:06, VBGameplayTags.h).
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_FinisherWindowArmed);

	// 창 동안 신규 공격 차단. 엔진이 후보 GA 의 AssetTags 를 부모까지 확장해 비교하므로
	// 부모 하나로 Ability.Attack.* 전부가 걸린다(UVBGA_Stagger 와 같은 수단).
	// 주의: 이것은 신규 활성화만 막는다. 이미 도는 스윙은 ExecuteAbility 에서 따로 취소한다 -
	//   그 둘을 구분하지 않은 것이 2026-09-11 「처형에 공격 애니가 겹친다」의 절반이었다.
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Attack);

	// 시체는 처형 창에 들어가지 않는다. 죽는 타격이 문턱을 넘기면서 동시에 죽이는 경우가 실제로 있다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);

	// 엎드려 있어도 막지 않는다 - 켜져서 기다린다(OnReactionRecheck). 막으면 넘어뜨린 그 타격의 창 요청이 버려져
	//   다음 타격이 올 때까지 창이 안 열린다(2026-09-29 첫 판이 그렇게 막았다가 되돌렸다).

	// 켜지 않는다(기본값 false 를 명시). 켜면 창 중의 추가 타격이 매번 창을 다시 열어 사실상 영구 무력화가 된다.
	bRetriggerInstancedAbility = false;
}

bool UVBGA_FinisherWindow::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* /*ActorInfo*/,
                                                       const FGameplayEventData*        Payload) const
{
	if (!Payload)
	{
		return false;
	}

	// 설정이 없으면 열지 않는다. 여기서 기본값으로 통과시키면 ExecuteAbility 의 미할당 경고가
	// 쿨다운을 태운 뒤에야 나온다 - 막을 수 있는 자리에서 막는다(규칙 27).
	if (!FinisherWindowConfig)
	{
		return false;
	}

	// EventMagnitude = 남은 체력 비율(UVBHealthAttributeSet 이 싣는다).
	return Payload->EventMagnitude <= FinisherWindowConfig->HealthThreshold;
}

void UVBGA_FinisherWindow::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                          const FGameplayAbilityActorInfo*     ActorInfo,
                                          const FGameplayAbilityActivationInfo ActivationInfo,
                                          const FGameplayEventData*            TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// InstancedPerActor 라 인스턴스가 액터 수명 내내 산다. 초기화하지 않으면 두 번째 창부터
	// 장면 보류 로그가 영영 안 찍힌다(2026-09-11 /verify). 창 태그 표지도 같은 이유로 여기서 비운다.
	bDeferredForScene = false;
	bWindowOpen       = false;

	// 데이터 홈 미할당은 설정 오류다. 기본값으로 열면 「문턱이 어디인지 아무도 모르는 창」이 생긴다.
	if (!FinisherWindowConfig)
	{
		VB_LOG(Warning, "%s: FinisherWindowConfig 미할당 -> 창을 열지 않는다. "
		                "BP_GA_FinisherWindow 에 설정 자산을 할당하라", *GetName());
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 사망 감시. 처형이 성공하면 이 액터는 장면 끝에 죽는데, 그때 창이 남아 있으면
	//   시체가 처형 가능 태그를 든 채로 서 있게 되어 두 번째 처형이 활성화된다.
	// 왜 장면 종료가 아니라 사망을 보는가: 장면 종료를 보려면 플러그인 델리게이트가 이 GA 까지 새어 나오고,
	//   조기 이탈(대상이 살아남는 처형)에서는 오히려 창을 닫으면 안 된다 - 남은 시간은 여전히 유효하다.
	//   죽음은 두 경우를 정확히 가르는 단 하나의 신호다.
	// 맨 앞에 두는 이유는 UVBGA_EnemyMeleeAttack 의 경직 감시와 같다 - 이미 죽어 있으면 엔진이 그 자리에서
	//   Added 를 쏘고 아래가 끝난 어빌리티에 태스크를 더 만들게 된다. 아래의 IsActive 가드가 그것을 끊는다.
	UAbilityTask_WaitGameplayTagAdded* DeathWatch =
		UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(
			this, VBGameplayTags::State_Dead, /*InOptionalExternalTarget=*/nullptr, /*OnlyTriggerOnce=*/true);
	DeathWatch->Added.AddDynamic(this, &UVBGA_FinisherWindow::OnOwnerDied);
	DeathWatch->ReadyForActivation();

	if (!IsActive())
	{
		return;
	}

	// 문턱을 넘긴 그 타격의 반응이 도는 중이면 끝날 때까지 기다린다(헤더 주석). 허브는 반응 이벤트를 창 이벤트보다
	//   먼저 쏘므로(UVBHealthAttributeSet 4단계 -> 6단계) 이 자리에서는 반응 GA 가 이미 서 있다.
	if (IsOwnerReacting())
	{
		VB_LOG(Log, "처형 창 대기(피격 반응 또는 안 끊기는 공격 중): %s - 끝나면 연다", *GetNameSafe(GetAvatarActorFromActorInfo()));
		ReactionWaitStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		OnReactionRecheck();
		return;
	}

	OpenWindow();
}

bool UVBGA_FinisherWindow::IsOwnerReacting() const
{
	const UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo();
	if (!OwnerASC)
	{
		return false;
	}
	// 기다리는 것은 «짧은 피격 반응» 과 «누웠다 일어나는 동안» 뿐이다.
	// 경직(기절 게이지 소진으로 선 기절, Staggered 만)은 기다리지 않는다 (2026-09-29 user PIE: 「처형 준비상태가 (기절이) 끝난 뒤에 와야 한다.. 이건 아닌 듯」).
	//   기절은 3.7초짜리 «무력한 상태» 라 그 자체가 처형할 때다. 기다리면 창이 한참 늦게 열린다. 창이 열리면 대기 몽타주가 기절 몽타주를
	//   이어받는다(같은 슬롯) - 둘 다 무력한 자세라 튀지 않는다.
	// 넘어짐(UVBGA_Knockdown)은 Staggered 도 들지만 Downed 로 가른다 - 누운 몸에는 선 피해자를 전제로 한 처형 장면을 걸 수 없다.
	// GettingUp 은 처형에서 살아남아 일어나는 중(UVBGA_GetUp)과 플레이어 래그돌 기상이다. 선 자세 대기가 그 위에 덮이면 안 된다.
	// SuperArmor(맞아도 안 끊기는 공격 - 병사콤보의 연속 피격 반격, 규칙 B)도 기다린다 (2026-09-29 user 결정 「반격은 끝까지 휘두르고 끝난 뒤 처형」).
	//   종전에는 문턱을 넘긴 타격이 그 반격 도중이면 창이 즉시 열려 반격을 끊었다 - 끊기지 않는다고 표시한 공격이 끊긴 것이다.
	//   피해·기절 게이지 소진 보류는 그대로다. 공격이 끝나 태그가 걷히면 다음 재확인에서 창이 열린다.
	FGameplayTagContainer ReactionTags;
	ReactionTags.AddTag(VBGameplayTags::State_Combat_HitReacting);
	ReactionTags.AddTag(VBGameplayTags::State_Combat_Downed);
	ReactionTags.AddTag(VBGameplayTags::State_Combat_GettingUp);
	ReactionTags.AddTag(VBGameplayTags::State_Combat_SuperArmor);
	return OwnerASC->HasAnyMatchingGameplayTags(ReactionTags);
}

void UVBGA_FinisherWindow::OnReactionRecheck()
{
	if (!IsActive())
	{
		return;
	}
	if (!IsOwnerReacting())
	{
		OpenWindow();
		return;
	}
	// 상한. 반응 태그가 새면(슈퍼아머를 못 떼고 끝난 공격 GA 등) 창도 안 열리고, 켜짐 태그가 서 있는 동안 기절도 영영 안 나온다.
	//   그래서 설정 시간을 넘기면 창을 포기하고 소리낸다 - 조용히 기다리면 «그 적만 처형이 안 된다» 로만 보인다(2026-09-30 /sw:verify).
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (Now - ReactionWaitStartTime > FinisherWindowConfig->MaxReactionWaitSeconds)
	{
		VB_LOG(Warning, "처형 창 포기: %s - %.1f초 동안 반응·슈퍼아머 태그가 안 걷혔다(새는 태그를 의심하라)",
		       *GetNameSafe(GetAvatarActorFromActorInfo()), FinisherWindowConfig->MaxReactionWaitSeconds);
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true);
		return;
	}
	// 태그 제거 이벤트 대신 짧은 재확인을 쓴다. 연타 중 플린치는 재트리거(bRetriggerInstancedAbility)라 같은 호출 안에서
	//   태그가 1 -> 0 -> 1 로 오간다 - 제거 이벤트에 걸면 새 플린치가 막 선 순간에 창이 열려 그것을 덮는다.
	//   간격은 설정값이 아니라 «조금 있다 다시 본다» 는 구현 세부다(장면 만료 재확인과 같은 판단).
	//   반응은 보통 스스로 끝나고(몽타주 수명) 사망하면 DeathWatch 가 끝낸다. 새는 태그는 위 상한이 끊는다.
	constexpr float ReactionRecheckSeconds = 0.1f;
	UAbilityTask_WaitDelay* Recheck = UAbilityTask_WaitDelay::WaitDelay(this, ReactionRecheckSeconds);
	Recheck->OnFinish.AddDynamic(this, &UVBGA_FinisherWindow::OnReactionRecheck);
	Recheck->ReadyForActivation();
}

void UVBGA_FinisherWindow::OpenWindow()
{
	if (bWindowOpen)
	{
		return;
	}
	bWindowOpen = true;

	// 창 태그. 복제 여부는 엔진이 ActivationOwnedTags 에 쓰는 것과 같은 설정을 따른다
	//   (GameplayAbility.cpp::UGameplayAbility::PreActivate - ShouldReplicateActivationOwnedTags) - 종전과 동작이 같아야 한다.
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		OwnerASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow, 1,
			UAbilitySystemGlobals::Get().ShouldReplicateActivationOwnedTags()
				? EGameplayTagReplicationState::CountToOwner : EGameplayTagReplicationState::None);
	}

	// 창이 열린 동안 몸을 빛나게 한다. 큐가 대는 이유는 발광이 «보는 모든 사람» 에게 보여야 하는데
	//   이 본문은 서버에서만 돌기 때문이다. 루프 큐는 엔진이 복제한다.
	// bRemoveOnAbilityEnd 를 켜서 끄는 자리를 한 곳으로 모은다 - 창이 닫히는 길이 여럿이고
	//   (시간 만료·처형 성립·사망) 전부 EndAbility 를 지난다. 손으로 끄면 한 길을 빠뜨린다.
	K2_AddGameplayCue(VBGameplayTags::GameplayCue_Combat_FinisherWindow,
	                  FGameplayEffectContextHandle(), /*bRemoveOnAbilityEnd=*/true);

	// 진행 중인 공격을 끊는다. BlockAbilitiesWithTag 는 신규만 막으므로 이 한 줄이 없으면
	// 예고·스윙 중이던 적이 처형 장면 위로 계속 스윙한다(user 가 본 겹침).
	// this 를 Ignore 로 넘긴다 - 이 GA 는 Ability.Attack 이 아니지만 계약을 명시해 둔다.
	if (UAbilitySystemComponent* OwnerASC = GetAbilitySystemComponentFromActorInfo())
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(VBGameplayTags::Ability_Attack);
		OwnerASC->CancelAbilities(&AttackTags, nullptr, this);
	}

	// 속도와 가속을 지운다. 「지금 멈춤」이지 「못 움직임」이 아니다 - 경계는 설정 자산의 주석에 적어 두었다.
	if (FinisherWindowConfig->bStopMovementOnOpen)
	{
		if (const ACharacter* AvatarCharacter = GetAvatarCharacter())
		{
			if (UCharacterMovementComponent* MoveComp = AvatarCharacter->GetCharacterMovement())
			{
				MoveComp->StopMovementImmediately();
			}
		}
	}

	// 대기 자세. 미할당이 정상이라 태스크를 만들지 않고 넘어간다 - 이 경로에서도 창은 성립한다.
	// 몽타주 끝을 창의 끝으로 삼지 않는다. 창의 길이는 WindowDuration 하나가 정해야 하고,
	//   몽타주 길이가 그 판단에 끼면 자산을 바꿀 때마다 게임 규칙이 조용히 바뀐다.
	if (FinisherWindowConfig->WaitMontage)
	{
		UAbilityTask_PlayMontageAndWait* MontageTask =
			UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
				this, NAME_None, FinisherWindowConfig->WaitMontage, 1.0f, NAME_None,
				/*bStopWhenAbilityEnds=*/true, 1.0f, 0.0f, false);
		MontageTask->ReadyForActivation();
	}

	UAbilityTask_WaitDelay* WindowTimer =
		UAbilityTask_WaitDelay::WaitDelay(this, FinisherWindowConfig->WindowDuration);
	WindowTimer->OnFinish.AddDynamic(this, &UVBGA_FinisherWindow::OnWindowExpired);
	WindowTimer->ReadyForActivation();

	VB_LOG(Log, "처형 창 개방: %s (%.1f초)", *GetNameSafe(GetAvatarActorFromActorInfo()),
	       FinisherWindowConfig->WindowDuration);
}

void UVBGA_FinisherWindow::OnWindowExpired()
{
	// 장면이 도는 동안에는 창을 닫지 않는다.
	//
	// 왜: 창이 닫히면 State.Combat.FinisherWindow 가 사라지고, 그 태그에 걸려 있던 것들이 한꺼번에 풀린다 -
	//   적 BT 의 이동 게이트와 공격 차단이다. 처형 장면 중간에 그것이 풀리면 적이 제 로코모션으로
	//   돌아가서 상체는 처형 애니, 다리는 걷기가 된다(2026-09-11 user: 「다리는 처형 애니메이션이 진행 안되는 듯」).
	//   실측: 창 개방 07:00:05.473 -> 장면 시작 :07.115 -> 창 만료 :09.479 로, 3.33초짜리 장면이
	//   1.7초 남은 시점에 창이 먼저 닫혔다.
	// 왜 창 길이를 늘리지 않는가: 장면은 플레이어가 F 를 누른 시점에 시작하므로 언제 시작할지 모른다.
	//   창을 장면 길이만큼 늘려도 창 끝에 눌리면 같은 일이 난다. 「장면이 도는 중인가」를 직접 보는 것이 답이다.
	// 왜 장면 종료 델리게이트가 아닌가: 피해자에는 우리 컴포넌트가 없고(주석 IsActorInScene 참조),
	//   구독을 걸려면 플러그인 타입이 이 GA 까지 새어 나온다. 짧은 재확인이 더 싸고 접점이 안 늘어난다.
	if (UVBFinisherComponent::IsActorInScene(GetAvatarActorFromActorInfo()))
	{
		if (!bDeferredForScene)
		{
			bDeferredForScene = true;
			VB_LOG(Log, "처형 창 만료 보류(장면 진행 중): %s", *GetNameSafe(GetAvatarActorFromActorInfo()));
		}

		// 설정값으로 빼지 않는다 - 이것은 연출 노브가 아니라 「조금 있다 다시 본다」는 구현 세부다.
		// 장면이 끝나거나 소유자가 죽으면 끝나므로 무한히 돌지 않는다.
		constexpr float SceneRecheckSeconds = 0.25f;
		UAbilityTask_WaitDelay* Recheck = UAbilityTask_WaitDelay::WaitDelay(this, SceneRecheckSeconds);
		Recheck->OnFinish.AddDynamic(this, &UVBGA_FinisherWindow::OnWindowExpired);
		Recheck->ReadyForActivation();
		return;
	}

	// 처형당하지 않고 닫힌 정상 경로다. bWasCancelled=false - 취소가 아니라 만료다.
	VB_LOG(Log, "처형 창 만료: %s", *GetNameSafe(GetAvatarActorFromActorInfo()));
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UVBGA_FinisherWindow::OnOwnerDied()
{
	// 처형이 성사됐거나 다른 이유로 죽었다. 어느 쪽이든 창은 닫힌다.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}

void UVBGA_FinisherWindow::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                                      const FGameplayAbilityActorInfo*     ActorInfo,
                                      const FGameplayAbilityActivationInfo ActivationInfo,
                                      bool                                 bReplicateEndAbility,
                                      bool                                 bWasCancelled)
{
	// 손으로 붙인 창 태그를 뗀다. 붙일 때와 같은 복제 상태로 떼야 개수가 맞는다.
	//   대기 중(반응이 안 끝난 채)에 끝났으면 붙인 적이 없다 - 떼면 다른 생산자가 없는데도 개수가 음수로 간다.
	if (bWindowOpen)
	{
		if (UAbilitySystemComponent* OwnerASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr)
		{
			OwnerASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow, 1,
				UAbilitySystemGlobals::Get().ShouldReplicateActivationOwnedTags()
					? EGameplayTagReplicationState::CountToOwner : EGameplayTagReplicationState::None);
		}
		bWindowOpen = false;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
