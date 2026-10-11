// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBGA_UseItem.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitAbilityActivate.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Traversal/VBTraversalComponent.h"
#include "Vowbound/Vowbound.h"

UVBGA_UseItem::UVBGA_UseItem()
{
	// 입력 GA 가 아니다 -> InputTag 없음. 정책은 베이스 기본값 그대로(UVBGA_HitReact 생성자 주석과 같은 이유 - OnSpawn 이면 부여 즉시 켜진다).
	// 비용·쿨다운 없음(설계 6절). 쓰는 값은 아이템 수다.

	// 외부에서 이 GA 를 끊거나 막으려면 AssetTag 가 있어야 한다 - 엔진의 CancelAbilities 계열은 대상 GA 의 AssetTags 를 맞춘다.
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VBGameplayTags::Ability_Item_Use);
	SetAssetTags(AssetTags);

	// 배선은 ASC 가 한다(UAbilitySystemComponent::OnGiveAbility 가 AbilityTriggers 를 이벤트 표에 등록). 부여만 하면 된다.
	FAbilityTriggerData Trigger;
	Trigger.TriggerTag = VBGameplayTags::Event_Item_Use;
	AbilityTriggers.Add(Trigger);

	// 몸이 다른 일에 묶여 있으면 시작하지 않는다 - 상호작용 GA(UVBGA_Interact)와 같은 목록에 가드·상호작용 중을 더했다.
	//   가드 중에 시작하면 다음 순간 가드가 사용을 끊는 것이 아니라(가드는 이미 켜져 있다) 가드 자세 위에 쓰는 몽타주가 얹힌다.
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Finishing);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Dodging);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Guarding);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_Staggered);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_HitReacting);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Combat_GettingUp);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Weapon_Summoning);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Weapon_Sheathing);
	ActivationBlockedTags.AddTag(VBGameplayTags::State_Interacting);

	// 쓰는 동안 공격·상호작용은 막는다(상호작용 GA 가 홀드 동안 공격·방어를 막는 것과 짝). 그 몽타주가 쓰는 몸짓을 덮으면 손이 튄다.
	//   회피·가드는 막지 않는다 - 켜지면 사용을 끊는다(HandleDefenseActivated, user 결정 2026-10-05).
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Attack);
	BlockAbilitiesWithTag.AddTag(VBGameplayTags::Ability_Interact);
}

bool UVBGA_UseItem::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	if (!Character)
	{
		return false;
	}
	// 몽타주가 매달림·등반 자세를 덮고, 공중에서는 쓰는 몸짓이 떨어지는 몸에 얹힌다.
	if (const UVBTraversalComponent* Traversal = Character->FindComponentByClass<UVBTraversalComponent>();
		Traversal && (Traversal->OwnsCapsuleFacing() || Traversal->IsDoingTraversal()))
	{
		return false;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	return Movement && Movement->IsMovingOnGround();
}

bool UVBGA_UseItem::ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const
{
	if (!Super::ShouldAbilityRespondToEvent(ActorInfo, Payload))
	{
		return false;
	}
	const UVBItemDefinition* Item = Payload ? Cast<UVBItemDefinition>(Payload->OptionalObject.Get()) : nullptr;
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || !Usable->UseMontage)
	{
		// 요청한 쪽(UVBInventoryComponent::BeginUse)이 몽타주 있는 아이템만 보낸다. 여기 오면 배선 사고다 - 말한다.
		VB_LOG(Warning, "%s: 사용 요청의 아이템(%s)에 사용 몽타주가 없다 - 켜지 않는다", *GetName(), *GetNameSafe(Item));
		return false;
	}
	// 몸을 움직이기 전에 거절한다 - 움직인 뒤 거절하면 «했는데 안 됐다» 로 보인다.
	const UVBInventoryComponent* Inventory = ActorInfo ? UVBInventoryComponent::FindFor(ActorInfo->AvatarActor.Get()) : nullptr;
	return Inventory && Inventory->CanUseItem(Item);
}

void UVBGA_UseItem::ExecuteAbility(const FGameplayAbilitySpecHandle     Handle,
                                   const FGameplayAbilityActorInfo*     ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo,
                                   const FGameplayEventData*            TriggerEventData)
{
	bMarkerReached = false;
	ActiveItem = TriggerEventData ? Cast<UVBItemDefinition>(TriggerEventData->OptionalObject.Get()) : nullptr;
	const FVBItemFragment_Usable* Usable = ActiveItem ? ActiveItem->FindFragment<FVBItemFragment_Usable>() : nullptr;
	ActiveMontage = Usable ? Usable->UseMontage.Get() : nullptr;
	if (!ActiveMontage)
	{
		// 서버는 ShouldAbilityRespondToEvent 가 이미 걸렀다. 여기 오는 것은 페이로드의 아이템을 못 푼 주인 클라뿐이다 -
		//   몽타주를 안 틀고 기다린다(끝은 서버가 정해 복제로 내려온다).
		VB_LOG(Warning, "%s: 사용 요청의 아이템(%s)을 풀지 못했거나 몽타주가 없다 - 이 쪽에서는 틀지 않는다", *GetName(), *GetNameSafe(ActiveItem));
		EndOnServer(true, TEXT("몽타주 없음"));
		return;
	}

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		// 듣는 것을 몽타주보다 먼저 건다 - 몽타주 첫 프레임의 표식도 놓치지 않게.
		//   표식은 한 번만 듣지 않는다(OnlyTriggerOnce=false). 버린 표식(내 몽타주가 안 도는 때)이 듣기를 끝내면 진짜 표식을 못 받는다.
		UAbilityTask_WaitGameplayEvent* MarkerTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, VBGameplayTags::Event_Item_UseMarker, nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
		MarkerTask->EventReceived.AddDynamic(this, &UVBGA_UseItem::HandleMarker);
		MarkerTask->ReadyForActivation();

		// 실제 피해 때 허브가 피격자 ASC 로 보내는 것을 직접 듣는다. 피격 반응 GA 의 끊기(CancelAbilitiesWithTag)에 맡기지 않는 이유 =
		//   슈퍼아머·처형 등으로 그 GA 가 안 켜지는 피격에도 끊겨야 한다(설계 6절).
		UAbilityTask_WaitGameplayEvent* HitTask = UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(
			this, VBGameplayTags::Event_Combat_Hit, nullptr, /*OnlyTriggerOnce=*/false, /*OnlyMatchExact=*/true);
		HitTask->EventReceived.AddDynamic(this, &UVBGA_UseItem::HandleHit);
		HitTask->ReadyForActivation();

		// 회피·가드가 켜지는 것을 이 GA 가 듣는다. 회피·가드 쪽에 «사용을 끊어라» 를 적지 않는 이유 = 사용이 무엇에 끊기는지가
		//   피격과 함께 이 파일 한 곳에 모인다. Ability.Defense 의 자식(가드·회피)도 맞는다(FGameplayTagContainer::HasTag 는 부모까지 본다).
		//   켜짐 알림은 그 GA 본문보다 먼저 온다(GameplayAbility.cpp::UGameplayAbility::PreActivate). 그래서 가드가 무기 관문에서 곧바로 끝나는
		//   경우(무기를 넣은 채 가드 버튼)에도 사용은 끊긴다 - «방어 버튼 = 사용 취소» 로 읽고 둔다. 표식 전이면 소모는 없다.
		UAbilityTask_WaitAbilityActivate* DefenseTask = UAbilityTask_WaitAbilityActivate::WaitForAbilityActivate(
			this, VBGameplayTags::Ability_Defense, FGameplayTag(), /*IncludeTriggeredAbilities=*/false, /*TriggerOnce=*/true);
		DefenseTask->OnActivate.AddDynamic(this, &UVBGA_UseItem::HandleDefenseActivated);
		DefenseTask->ReadyForActivation();
	}

	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		SheatheIfWanted(*Usable);
	}

	// 몽타주는 서버·주인 클라 둘 다 튼다(ServerInitiated - UVBGameplayAbility 생성자 주석).
	UAbilityTask_PlayMontageAndWait* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(
		this, NAME_None, ActiveMontage, 1.0f, NAME_None, /*bStopWhenAbilityEnds=*/true);
	MontageTask->OnCompleted.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->OnBlendOut.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->OnInterrupted.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->OnCancelled.AddDynamic(this, &UVBGA_UseItem::HandleMontageEnded);
	MontageTask->ReadyForActivation();
}

void UVBGA_UseItem::HandleMarker(FGameplayEventData Payload)
{
	if (!IsActive())
	{
		return;
	}
	if (bMarkerReached)
	{
		// 몽타주마다 하나라는 약속을 IsDataValid 가 지키지만, 검증을 안 거친 자산도 돈다. 두 번 확정하지 않고 말한다.
		VB_LOG(Warning, "%s: %s 에 사용 표식이 두 번 왔다 - 두 번째는 버린다", *GetName(), *GetNameSafe(ActiveMontage));
		return;
	}
	// 표식 이벤트는 몽타주를 싣지 않는다(UVBAN_ItemUseMarker::Notify 주석). 내 몽타주의 인스턴스가 있는지로 내 표식인지 가른다.
	//   «활성» 인스턴스(Montage_IsPlaying)로 보지 않는 이유: 블렌드아웃이 시작되는 틱에는 엔진이 인스턴스를 먼저 멈추고(활성 목록에서 뺌)
	//   그 틱의 노티를 블렌드아웃 사건보다 먼저 보낸다(AnimMontage.cpp::FAnimMontageInstance::Advance, AnimInstance.cpp::UAnimInstance::DispatchQueuedAnimEvents).
	//   그 틱에 걸린 표식을 버리지 않으려고 블렌드아웃 중인 인스턴스까지 본다(GetInstanceForMontage). 블렌드아웃 «안» 의 표식은 IsDataValid 가 막는다.
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	const UAnimInstance* Anim = ActorInfo ? ActorInfo->GetAnimInstance() : nullptr;
	if (!Anim || !Anim->GetInstanceForMontage(ActiveMontage))
	{
		return;
	}
	bMarkerReached = true;
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetAvatarActorFromActorInfo());
	if (!Inventory || !Inventory->ConfirmUse(ActiveItem))
	{
		// 동작은 이미 표식 시각이다. 몽타주는 끝까지 두고(끊으면 손이 튄다) 소모·효과만 안 낸다 - 상호작용 GA 의 확정 거절과 같다.
		VB_LOG(Log, "%s: %s 확정 거절(그 사이 수·조건이 바뀜) - 동작만 끝까지", *GetName(), *GetNameSafe(ActiveItem));
	}
}

void UVBGA_UseItem::HandleHit(FGameplayEventData Payload)
{
	// 확정 뒤의 피격으로는 끊지 않는다 - 효과는 이미 났다. 몸짓은 피격 반응 몽타주가 덮으면 그 끊김(HandleMontageEnded)으로 끝난다.
	if (!bMarkerReached)
	{
		EndOnServer(true, TEXT("표식 전 피격"));
	}
}

void UVBGA_UseItem::HandleDefenseActivated(UGameplayAbility* ActivatedAbility)
{
	EndOnServer(true, bMarkerReached ? TEXT("확정 뒤 회피·가드") : TEXT("표식 전 회피·가드"));
}

void UVBGA_UseItem::HandleMontageEnded()
{
	if (!bMarkerReached && IsActive() && GetCurrentActorInfo() && GetCurrentActorInfo()->IsNetAuthority())
	{
		// 끊김(피격 반응·회피 몽타주가 덮음)이면 흔한 경로다. 끝까지 돌았는데 표식이 없었으면 자산이 약속을 어긴 것이다 -
		//   이 콜백은 둘을 가르지 못하므로 로그는 Log 로 두고 무엇을 의심할지 적는다.
		VB_LOG(Log, "%s: %s 가 표식 없이 끝났다 - 끊겼거나 표식이 없다(IsDataValid 확인)", *GetName(), *GetNameSafe(ActiveMontage));
	}
	EndOnServer(!bMarkerReached, TEXT("몽타주 끝"));
}

void UVBGA_UseItem::SheatheIfWanted(const FVBItemFragment_Usable& Usable)
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	UVBWeaponStateComponent* Weapon = Avatar ? Avatar->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	// 이미 수납돼 있으면(맨손·트래버설·상호작용 수납) 손대지 않는다 - 돌려놓는 것도 우리 몫이 아니게 된다.
	if (!Usable.bSheatheWeaponDuringUse || !Weapon || !Weapon->IsWeaponSummoned() || Weapon->IsTempSheathed())
	{
		return;
	}
	Weapon->TempSheatheForParkour();
	bSheathedWeapon = true;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->AddLooseGameplayTag(VBGameplayTags::State_Weapon_TempSheathed);
	}
}

void UVBGA_UseItem::EndOnServer(bool bWasCancelled, const TCHAR* Why)
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (!IsActive() || !ActorInfo || !ActorInfo->IsNetAuthority())
	{
		// 원격 클라가 EndAbility 를 부르면 서버로 올라가 서버의 사용을 끊는다 - 클라 몽타주가 먼저 블렌드아웃하면
		//   서버가 표식에 닿기 전에 취소될 수 있다. 끝은 서버가 정하고 복제로 내려온다(UVBGA_Interact 의 같은 판단).
		return;
	}
	VB_LOG(Log, "%s: %s 사용 끝 - %s (확정 %s)", *GetName(), *GetNameSafe(ActiveItem), Why, bMarkerReached ? TEXT("함") : TEXT("안 함"));
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, bWasCancelled);
}

void UVBGA_UseItem::EndAbility(const FGameplayAbilitySpecHandle     Handle,
                               const FGameplayAbilityActorInfo*     ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo,
                               bool                                 bReplicateEndAbility,
                               bool                                 bWasCancelled)
{
	// 끝 경로가 여럿이다(완료·블렌드아웃·피격·회피·가드·사망 취소·시작 거절). 정리는 여기 한 곳에서 한다.
	if (bSheathedWeapon)
	{
		bSheathedWeapon = false;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
		{
			ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Weapon_TempSheathed);
		}
		// 죽어서 끝났으면 돌려놓지 않는다 - 시체 손에 칼이 돌아온다(상호작용 GA 와 같은 판단).
		//   회피·가드로 끊긴 경우는 그 GA 본문보다 먼저 여기 오므로(UGameplayAbility::PreActivate 알림) 가드가 돌아온 무기로 선다.
		AActor* Avatar = GetAvatarActorFromActorInfo();
		if (!UVBAbilitySystemStatics::IsActorDead(Avatar))
		{
			if (UVBWeaponStateComponent* Weapon = Avatar ? Avatar->FindComponentByClass<UVBWeaponStateComponent>() : nullptr)
			{
				Weapon->RestoreFromTempSheathe();
			}
		}
	}
	ActiveItem = nullptr;
	ActiveMontage = nullptr;
	bMarkerReached = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
