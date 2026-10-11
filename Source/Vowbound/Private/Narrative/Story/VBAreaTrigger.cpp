// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Story/VBAreaTrigger.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "TimerManager.h"
#include "Vowbound/Vowbound.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBAreaTrigger"

bool VBAreaTrigger::ShouldFire(bool bWasSatisfied, bool bNowSatisfied, bool bOnce, bool bOnceAlreadyFired)
{
	if (!bNowSatisfied || bWasSatisfied)
	{
		return false;
	}
	return !(bOnce && bOnceAlreadyFired);
}

AVBAreaTrigger::AVBAreaTrigger()
{
	PrimaryActorTick.bCanEverTick = false;

	Area = CreateDefaultSubobject<UBoxComponent>(TEXT("Area"));
	SetRootComponent(Area);
	Area->SetBoxExtent(FVector(200.0f, 200.0f, 150.0f));
	// 겹침만 낸다. 막지 않는다 - 영역은 보이지 않는 판정이다.
	Area->SetCollisionProfileName(UCollisionProfile::CustomCollisionProfileName);
	Area->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Area->SetCollisionObjectType(ECC_WorldDynamic);
	Area->SetCollisionResponseToAllChannels(ECR_Ignore);
	Area->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Area->SetGenerateOverlapEvents(true);
	Area->SetHiddenInGame(true);
}

void AVBAreaTrigger::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
	{
		return;
	}

	Area->OnComponentBeginOverlap.AddDynamic(this, &AVBAreaTrigger::HandleBeginOverlap);
	Area->OnComponentEndOverlap.AddDynamic(this, &AVBAreaTrigger::HandleEndOverlap);

	if (UVBStoryStateComponent* Story = UVBStoryStateComponent::Get(this))
	{
		BoundStory = Story;
		StoryChangedHandle = Story->OnStoryStateChanged.AddUObject(this, &AVBAreaTrigger::RequestEvaluate);
	}

	// 이미 안에서 시작한 플레이어도 «들어옴» 으로 친다(설계 8.3) - 겹침 신호만 기다리면 그 플레이어를 영영 못 본다.
	RefreshPlayerBindings();
	RequestEvaluate();
}

void AVBAreaTrigger::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	DisconnectPlayerBindings();
	Area->OnComponentBeginOverlap.RemoveDynamic(this, &AVBAreaTrigger::HandleBeginOverlap);
	Area->OnComponentEndOverlap.RemoveDynamic(this, &AVBAreaTrigger::HandleEndOverlap);
	if (UVBStoryStateComponent* Story = BoundStory.Get())
	{
		Story->OnStoryStateChanged.Remove(StoryChangedHandle);
	}
	BoundStory.Reset();
	StoryChangedHandle.Reset();
	GetWorldTimerManager().ClearAllTimersForObject(this);
	Super::EndPlay(EndPlayReason);
}

void AVBAreaTrigger::HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
                                        int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bEnding) return;
	RequestEvaluate();
	OnOccupancyChanged.Broadcast();
}

void AVBAreaTrigger::HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (bEnding) return;
	RequestEvaluate();
	OnOccupancyChanged.Broadcast();
}

void AVBAreaTrigger::RefreshPlayerBindings()
{
 if (!bRequirePlayerPreparation || bEnding || !GetWorld()) return;
 PlayerBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &ThisClass::RequestEvaluate));
}

void AVBAreaTrigger::DisconnectPlayerBindings()
{
 PlayerBinding.Disconnect();
}

void AVBAreaTrigger::RequestEvaluate()
{
	if (bEnding || bEvaluatePending || !HasAuthority() || !GetWorld())
	{
		return;
	}
	bEvaluatePending = true;
	GetWorldTimerManager().SetTimerForNextTick(this, &AVBAreaTrigger::Evaluate);
}

void AVBAreaTrigger::Evaluate()
{
	bEvaluatePending = false;
	UWorld* World = GetWorld();
	if (bEnding || !World || !HasAuthority())
	{
		return;
	}

	RefreshPlayerBindings();
	const bool bOnce = OnceFact.IsValid();
	if (bRequirePlayerPreparation && !bOnce)
	{
		if (!bWarnedMissingOnce)
		{
			bWarnedMissingOnce = true;
			VB_LOG(Warning, "%s: 준비 검사는 OnceFact가 있는 일회 영역에만 쓴다 - 발화하지 않는다", *GetName());
		}
		return;
	}
	const UVBStoryStateComponent* Story = UVBStoryStateComponent::Get(this);
	if (bOnce && !Story)
	{
		// 한 번만을 기록할 곳이 없다 - 발화하면 들어올 때마다 반복된다. 못 지키는 약속을 통과로 떨어뜨리지 않고 멈춘다.
		if (!bWarnedNoStory)
		{
			bWarnedNoStory = true;
			VB_LOG(Warning, "%s: 한 번만 영역인데 세계 사실(UVBStoryStateComponent)이 없다 - 발화하지 않는다", *GetName());
		}
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!IsValid(Pawn) || (bRequirePlayerPreparation && PlayerController != PlayerBinding.GetController()))
		{
			continue;
		}
		const bool bNow = IsSatisfiedFor(*Pawn);
		const bool bWas = LastSatisfied.FindOrAdd(Pawn, false);
		const bool bFired = bOnce && Story->HasFact(OnceFact);
		const bool bFire = VBAreaTrigger::ShouldFire(bWas, bNow, bOnce, bFired);
		// 마지막 admission 실패는 edge도 소비하지 않는다. 효과 중 재진입에 map 원소 참조를 보관하지 않는다.
		if (!bRequirePlayerPreparation)
		{
			LastSatisfied.FindOrAdd(Pawn) = bNow;
			if (bFire) TryFire(*Pawn);
			if (bEnding) return;
			continue;
		}
		const bool bAdmitted = bFire && TryFire(*Pawn);
		if (bEnding) return;
		LastSatisfied.FindOrAdd(Pawn) = bFire ? bAdmitted : bNow;
	}

	// 사라진 폰(죽음·다시 낳음)의 기록을 지운다. 다시 낳은 폰은 새 열쇠라 «거짓» 에서 다시 시작한다.
	for (auto It = LastSatisfied.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || (bRequirePlayerPreparation && It.Key().Get() != PlayerBinding.GetPawn()))
		{
			It.RemoveCurrent();
		}
	}
}

bool AVBAreaTrigger::IsPawnInside(const APawn* Pawn) const
{
	const UWorld* World = GetWorld();
	return !bEnding && !IsActorBeingDestroyed() && World
		&& IsValid(Pawn)
		&& IsValid(Area.Get())
		&& Pawn->GetWorld() == World
		&& Area->IsOverlappingActor(Pawn);
}

bool AVBAreaTrigger::IsSatisfiedFor(APawn& Pawn)
{
	if (!IsPawnInside(&Pawn))
	{
		return false;
	}
	FVBRuleContext Context = FVBRuleContext::Make(this, Pawn.GetPlayerState());
	Context.Counterpart = this;
	if (!VBRules::EvaluateAll(Conditions, Context)) return false;
	if (!bRequirePlayerPreparation) return true;
	const APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	return OnceFact.IsValid() && IsValid(Controller) && Controller->GetPawn() == &Pawn
		&& Controller->PlayerState == Pawn.GetPlayerState()
		&& UVBPlayerReadinessComponent::QueryForPawn(&Pawn).State == EVBPlayerPreparationState::Ready;
}

bool AVBAreaTrigger::TryFire(APawn& Pawn)
{
	// 사실/사건/효과 바로 앞에서 현재 점유·조건·준비·Once를 다시 확인한다.
	if (bEnding || !HasAuthority() || (bRequirePlayerPreparation && !IsSatisfiedFor(Pawn))) return false;
	FVBRuleContext Context = FVBRuleContext::Make(this, Pawn.GetPlayerState());
	Context.Counterpart = this;
	if (OnceFact.IsValid() && (!Context.Story || Context.Story->HasFact(OnceFact))) return false;
	VB_LOG(Log, "영역 진입: %s <- %s", *GetName(), *GetNameSafe(&Pawn));

	// 사실 먼저 - 효과가 이 사실을 읽거나, 효과가 바꾼 사실로 다시 판정할 때 «이미 발화함» 이어야 한다.
	if (OnceFact.IsValid())
	{
		FVBEffect_SetFact MarkFired;
		MarkFired.Fact = OnceFact;
		MarkFired.bOn = true;
		MarkFired.Apply(Context);
	}

	if (!EventTags.IsEmpty())
	{
		if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
		{
			FVBGameEvent Event;
			Event.EventTag = VBGameplayTags::Event_Area_Entered;
			Event.TargetTags = EventTags;
			Event.Instigator = Pawn.GetPlayerState();
			Events->Broadcast(Event);
		}
	}

	if (!Effects.IsEmpty())
	{
		VBRules::ApplyAll(Effects, Context);
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult AVBAreaTrigger::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (bRequirePlayerPreparation && !OnceFact.IsValid())
	{
		Context.AddError(LOCTEXT("PreparationNeedsOnce", "플레이어 준비 검사는 OnceFact가 있는 일회 영역에만 사용할 수 있습니다."));
		Result = EDataValidationResult::Invalid;
	}
	if (!VBRules::ValidateStructArray(Conditions, FVBCondition::StaticStruct(), LOCTEXT("ConditionsWhere", "조건"), Context)
		|| !VBRules::ValidateStructArray(Effects, FVBEffect::StaticStruct(), LOCTEXT("EffectsWhere", "효과"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	if (Effects.IsEmpty() && EventTags.IsEmpty() && !OnceFact.IsValid())
	{
		Context.AddWarning(LOCTEXT("DoesNothing", "효과·사건 태그·한 번만 사실이 모두 비었다 - 들어와도 아무 일이 없다."));
	}
	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
