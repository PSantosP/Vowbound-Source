// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBEncounterGateComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffectExtension.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Narrative/Story/VBAreaTrigger.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "TimerManager.h"
#include "Vowbound/Vowbound.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBEncounterGate"

// 쓰러진 조우: 새 보류는 걸 이유가 없고(멈출 것이 없다), 쓰러지기 전에 걸린 보류를 풀 때는 태그만 걷고 알리지 않는다 -
//   알리면 AI 컨트롤러가 행동 트리를 잇는다. 장면 생략·종료가 죽은 적을 다시 움직이면 안 된다(.sw/cinematic-playback 10절 4).
EVBEncounterHoldAction VBEncounterHold::Decide(bool bHeldNow, bool bWantHeld, bool bDefeated)
{
	if (bHeldNow == bWantHeld)
	{
		return EVBEncounterHoldAction::Ignore;
	}
	if (bDefeated)
	{
		return bWantHeld ? EVBEncounterHoldAction::Ignore : EVBEncounterHoldAction::ApplySilently;
	}
	return EVBEncounterHoldAction::ApplyAndNotify;
}

UVBEncounterGateComponent::UVBEncounterGateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

APlayerState* UVBEncounterGateComponent::GetFirstPlayerState() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	return Controller ? Controller->PlayerState : nullptr;
}

void UVBEncounterGateComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const FVBRuleContext Context = FVBRuleContext::Make(this, GetFirstPlayerState());
	if (!VBRules::EvaluateAll(ExistConditions, Context))
	{
		VB_LOG(Log, "조우 문: %s 는 나오지 않는다(존재 조건 거짓)", *Owner->GetName());
		Owner->Destroy();
		return;
	}
	bExisted = true;

	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner))
	{
		HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute())
			.AddUObject(this, &UVBEncounterGateComponent::HandleHealthChanged);
	}
	else
	{
		// 체력을 못 들으면 «맞으면 깬다» 와 «쓰러지면 효과» 가 둘 다 죽는다. 조용히 넘기지 않는다.
		VB_LOG(Warning, "조우 문: %s 에 ASC 가 없다 - 맞아도 깨지 않고 쓰러져도 효과가 없다", *Owner->GetName());
	}

	if (UVBStoryStateComponent* Story = UVBStoryStateComponent::Get(this))
	{
		BoundStory = Story;
		StoryChangedHandle = Story->OnStoryStateChanged.AddUObject(this, &UVBEncounterGateComponent::EvaluateWake);
	}
	RefreshPlayerBindings();
	EvaluateWake();
}

void UVBEncounterGateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	// 체력 0 없이 낙하(KillZ)로 사라진 적도 «해소» 로 친다 - 안 그러면 쓰러짐 사실이 영영 안 켜져 진행이 잠긴다(03 검토 10-03).
	//   그 밖의 사라짐은 해소가 아니다(03 2차 검토): 레벨 전환·스트리밍·PIE 종료(이유가 Destroyed 가 아님), 존재 조건으로 스스로 나감(bExisted 거짓),
	//   쓰러진 뒤 시체 정리(bDefeated 참), 관리상 Destroy(KillZ 위). AActor::FellOutOfWorld 를 컴포넌트가 덮을 수 없어 «KillZ 아래에서 파괴됨» 으로 가른다.
	if (EndPlayReason == EEndPlayReason::Destroyed && bExisted && !bDefeated && GetOwner() && GetOwner()->HasAuthority())
	{
		const UWorld* World = GetWorld();
		const AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
		if (Settings && Settings->bEnableWorldBoundsChecks && GetOwner()->GetActorLocation().Z < Settings->KillZ)
		{
			ApplyDefeated(nullptr, TEXT("KillZ 낙하"));
		}
	}
	if (UVBStoryStateComponent* Story = BoundStory.Get())
	{
		Story->OnStoryStateChanged.Remove(StoryChangedHandle);
	}
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner()))
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	}
	DisconnectPlayerBindings();
	if (bRequirePlayerPreparation)
	{
		if (UWorld* World = GetWorld()) World->GetTimerManager().ClearAllTimersForObject(this);
	}
	BoundStory.Reset();
	StoryChangedHandle.Reset();
	HealthChangedHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void UVBEncounterGateComponent::RefreshPlayerBindings()
{
 if (!bRequirePlayerPreparation || bEnding || !GetWorld()) return;
 PlayerBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &ThisClass::RequestEvaluateWake));

 AVBAreaTrigger* CurrentArea = HasValidWakeArea() ? WakeArea.Get() : nullptr;
 if (BoundWakeArea.Get() != CurrentArea)
 {
  if (AVBAreaTrigger* Old = BoundWakeArea.Get()) Old->OnOccupancyChanged.Remove(AreaOccupancyHandle);
  AreaOccupancyHandle.Reset(); BoundWakeArea = CurrentArea;
  if (CurrentArea) AreaOccupancyHandle = CurrentArea->OnOccupancyChanged.AddUObject(this, &ThisClass::RequestEvaluateWake);
 }
}

void UVBEncounterGateComponent::DisconnectPlayerBindings()
{
 PlayerBinding.Disconnect();

 if (AVBAreaTrigger* Old = BoundWakeArea.Get()) Old->OnOccupancyChanged.Remove(AreaOccupancyHandle);
 AreaOccupancyHandle.Reset(); BoundWakeArea.Reset();
}

void UVBEncounterGateComponent::RequestEvaluateWake()
{
	AActor* Owner = GetOwner();
	if (bEnding || bEvaluatePending || bAwake || bDefeated || !bExisted || !Owner || !Owner->HasAuthority() || !GetWorld()) return;
	bEvaluatePending = true;
	GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::EvaluateWake);
}

void UVBEncounterGateComponent::EvaluateWake()
{
	bEvaluatePending = false;
	if (bEnding || bAwake || bDefeated)
	{
		return;
	}
	Wake(WakeConditions.IsEmpty() ? TEXT("조건 없음") : TEXT("조건 참"));
}

void UVBEncounterGateComponent::Wake(const TCHAR* Why, bool bFromDamage)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (bEnding || bAwake || bDefeated || !bExisted || !IsValid(Owner) || !Owner->HasAuthority() || !World)
	{
		return;
	}
	RefreshPlayerBindings();
	APlayerController* Controller = World->GetFirstPlayerController();
	APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	FVBRuleContext Context = FVBRuleContext::Make(this, Controller ? Controller->PlayerState.Get() : nullptr);
	if (!bFromDamage && !VBRules::EvaluateAll(WakeConditions, Context)) return;
	if (bRequirePlayerPreparation)
	{
		if (!HasValidWakeArea() && !bWarnedInvalidWakeArea)
		{
			bWarnedInvalidWakeArea = true;
			VB_LOG(Warning, "조우 준비: %s WakeArea가 없거나 다른 월드입니다", *GetNameSafe(GetOwner()));
		}
		// 조건/피격 양쪽의 마지막 admission. 미준비 피격은 예약하지 않고, 후속 신호는 현재 조건만 다시 본다.
		if (!IsValid(Controller) || !IsValid(Pawn) || Pawn->GetWorld() != World
			|| Controller != World->GetFirstPlayerController() || Controller->GetPawn() != Pawn
			|| !IsValid(Controller->PlayerState) || Pawn->GetPlayerState() != Controller->PlayerState
			|| UVBPlayerReadinessComponent::QueryForPawn(Pawn).State != EVBPlayerPreparationState::Ready) return;
		if (!bFromDamage && (!HasValidWakeArea() || !WakeArea->IsPawnInside(Pawn))) return;
	}
	bAwake = true;
	VB_LOG(Log, "조우 문: %s 깨어남(%s)", *GetNameSafe(GetOwner()), Why);
	if (!WokenEffects.IsEmpty())
	{
		Context.Counterpart = Owner;
		VBRules::ApplyAll(WokenEffects, Context);
	}
	OnWoken.Broadcast();
}

void UVBEncounterGateComponent::SetHeld(bool bInHeld)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	const EVBEncounterHoldAction Action = VBEncounterHold::Decide(bHeld, bInHeld, bDefeated);
	if (Action == EVBEncounterHoldAction::Ignore)
	{
		VB_CLOG(bDefeated && bInHeld && !bHeld, Log, "조우 문: %s 는 이미 쓰러졌다 - 보류를 걸지 않는다", *Owner->GetName());
		return;
	}
	bHeld = bInHeld;
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner))
	{
		if (bHeld)
		{
			ASC->AddLooseGameplayTag(VBGameplayTags::State_Encounter_Held);
			// 태그는 «새» 공격만 막는다. 이미 시작된 공격은 여기서 끝낸다 - 공격 GA 가 끝나면 몽타주가 멈추고
			//   (PlayMontageAndWait 의 bStopWhenAbilityEnds) 열린 판정 창이 닫힌다(UVBGA_MeleeAttackBase::EndAbility 의 EndAllTraceWindows).
			//   부모 태그로 고른다 - CancelAbilities 는 GA 의 자산 태그에 HasAny 를 쓰므로 Ability.Attack.* 가 다 걸린다. 넉다운·기상 같은 반응은 안 건드린다.
			//   C++ 에는 적 발사체·지연 피해 경로가 없다(10-04 Enemy/·AI/ 소스 확인, 블루프린트 그래프는 안 쟀다). 생기면 그 주인이 여기서 함께 끄는 길을 더한다.
			const FGameplayTagContainer AttackTags(VBGameplayTags::Ability_Attack);
			ASC->CancelAbilities(&AttackTags);
		}
		else
		{
			ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Encounter_Held);
		}
	}
	else
	{
		// 태그를 못 붙이면 공격 GA 막기가 빠진다 - 행동 트리 멈춤만 남는다.
		VB_LOG(Warning, "조우 문: %s 에 ASC 가 없다 - 보류 태그를 못 붙인다(행동 트리 멈춤만 걸린다)", *Owner->GetName());
	}
	const bool bNotify = Action == EVBEncounterHoldAction::ApplyAndNotify;
	VB_LOG(Log, "조우 문: %s 보류 %s%s", *Owner->GetName(), bHeld ? TEXT("켬") : TEXT("끔"), bNotify ? TEXT("") : TEXT("(쓰러짐 - 행동 트리는 안 잇는다)"));
	if (bNotify)
	{
		OnHeldChanged.Broadcast(bHeld);
	}
}

void UVBEncounterGateComponent::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue >= Data.OldValue || bDefeated)
	{
		return;
	}
	if (!bAwake && bWakeWhenDamaged)
	{
		Wake(TEXT("맞음"), true);
	}
	if (Data.NewValue <= 0.0f)
	{
		// 쓰러뜨린 사람 = 피해를 준 GE 의 주인. 못 찾으면 첫 플레이어(싱글 전제, 클래스 주석).
		APlayerState* Killer = nullptr;
		if (Data.GEModData)
		{
			const APawn* InstigatorPawn = Cast<APawn>(Data.GEModData->EffectSpec.GetContext().GetOriginalInstigator());
			Killer = InstigatorPawn ? InstigatorPawn->GetPlayerState() : nullptr;
		}
		ApplyDefeated(Killer, TEXT("체력 0"));
	}
}

void UVBEncounterGateComponent::ApplyDefeated(APlayerState* Killer, const TCHAR* Why)
{
	if (bDefeated)
	{
		return;
	}
	bDefeated = true;
	if (!DefeatedEffects.IsEmpty())
	{
		FVBRuleContext Context = FVBRuleContext::Make(this, Killer ? Killer : GetFirstPlayerState());
		Context.Counterpart = GetOwner();
		VBRules::ApplyAll(DefeatedEffects, Context);
	}
	VB_LOG(Log, "조우 문: %s 해소(%s) - 효과 %d", *GetNameSafe(GetOwner()), Why, DefeatedEffects.Num());
}

#if WITH_EDITOR
EDataValidationResult UVBEncounterGateComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (bRequirePlayerPreparation && !IsValid(WakeArea))
	{
		Context.AddError(LOCTEXT("PreparationNeedsArea", "플레이어 준비를 확인하는 조우에는 현재 점유를 읽을 WakeArea가 필요합니다."));
		Result = EDataValidationResult::Invalid;
	}
	if (!VBRules::ValidateStructArray(ExistConditions, FVBCondition::StaticStruct(), LOCTEXT("ExistWhere", "존재 조건"), Context)
		|| !VBRules::ValidateStructArray(WakeConditions, FVBCondition::StaticStruct(), LOCTEXT("WakeWhere", "깨어날 조건"), Context)
		|| !VBRules::ValidateStructArray(WokenEffects, FVBEffect::StaticStruct(), LOCTEXT("WokenWhere", "깨어날 때 효과"), Context)
		|| !VBRules::ValidateStructArray(DefeatedEffects, FVBEffect::StaticStruct(), LOCTEXT("DefeatedWhere", "쓰러질 때 효과"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE


bool UVBEncounterGateComponent::HasValidWakeArea() const
{
 return IsValid(WakeArea) && WakeArea->GetWorld() == GetWorld() && !WakeArea->IsActorBeingDestroyed();
}
