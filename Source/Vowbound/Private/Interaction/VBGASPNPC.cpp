#include "Interaction/VBGASPNPC.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Components/StateTreeAIComponent.h"
#include "TimerManager.h"
#include "Animation/VBLookAtPOIComponent.h"
#include "Animation/VBLookAtInterestComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

AVBGASPNPCController::AVBGASPNPCController()
{
	StateTree = CreateDefaultSubobject<UStateTreeAIComponent>(TEXT("StateTree"));
	StateTree->SetStartLogicAutomatically(false);
	BrainComponent = StateTree;
	bStartAILogicOnPossess = false;
}
void AVBGASPNPCController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (!HasAuthority() || !BehaviorStateTree) return;
	if (const AVBGASPNPC* NPC = Cast<AVBGASPNPC>(InPawn); NPC && !NPC->bEnableAmbientBehavior) return;
	StateTree->SetStateTree(BehaviorStateTree);
	const float Delay = GetNetMode() == NM_DedicatedServer ? DedicatedServerInitialDelay : InitialDelay;
	GetWorld()->GetTimerManager().SetTimer(StartTimer, this, &ThisClass::StartBehavior, FMath::Max(0.001f, Delay), false);
}
void AVBGASPNPCController::StartBehavior()
{
	if (GetPawn() && BehaviorStateTree) StateTree->StartLogic();
}
void AVBGASPNPCController::OnUnPossess()
{
	GetWorld()->GetTimerManager().ClearTimer(StartTimer);
	StateTree->StopLogic(TEXT("NPC unpossessed"));
	if (APawn* ControlledPawn = GetPawn())
		if (UVBGASPSmartObjectComponent* Component = ControlledPawn->FindComponentByClass<UVBGASPSmartObjectComponent>()) Component->AbortInteraction();
	Super::OnUnPossess();
}
AVBGASPNPC::AVBGASPNPC(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	AIControllerClass = AVBGASPNPCController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	LookAtInterest = CreateDefaultSubobject<UVBLookAtInterestComponent>(TEXT("LookAtInterest"));
	LookAtInterest->SetupAttachment(GetRootComponent());
	LookAtInterest->TargetSocket = TEXT("head");
}

void AVBGASPNPC::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && bLookAtNearbyPlayers)
	{
		UpdatePlayerLookAt();
		GetWorldTimerManager().SetTimer(LookAtTimer, this, &ThisClass::UpdatePlayerLookAt, 0.2f, true);
	}
}

void AVBGASPNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(LookAtTimer);
	Super::EndPlay(EndPlayReason);
}

void AVBGASPNPC::UpdatePlayerLookAt()
{
	if (!HasAuthority()) return;
	const float AcquireDistance = FMath::Max(0.f, LookAtAcquireDistance);
	const float ReleaseDistance = FMath::Max(AcquireDistance, LookAtReleaseDistance);
	APawn* Next = nullptr;
	if (bLookAtNearbyPlayers && AcquireDistance > 0.f && !UVBAbilitySystemStatics::IsActorDead(this))
	{
		// Keep the current player until they leave: two nearby players must not cause gaze flicker.
		if (IsValid(LookAtPlayer) && LookAtPlayer->IsPlayerControlled()
			&& !UVBAbilitySystemStatics::IsActorDead(LookAtPlayer)
			&& FVector::DistSquared(GetActorLocation(), LookAtPlayer->GetActorLocation()) <= FMath::Square(ReleaseDistance))
		{
			Next = LookAtPlayer;
		}
		else
		{
			float BestDistanceSq = FMath::Square(AcquireDistance);
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				APawn* Candidate = PC ? PC->GetPawn() : nullptr;
				if (!IsValid(Candidate) || Candidate == this || UVBAbilitySystemStatics::IsActorDead(Candidate)) continue;
				const float DistanceSq = FVector::DistSquared(GetActorLocation(), Candidate->GetActorLocation());
				if (DistanceSq < BestDistanceSq)
				{
					BestDistanceSq = DistanceSq;
					Next = Candidate;
				}
			}
		}
	}
	if (LookAtPlayer != Next)
	{
		LookAtPlayer = Next;
		OnRep_LookAtPlayer();
	}
}

void AVBGASPNPC::OnRep_LookAtPlayer()
{
	if (!LookAtPOI) return;
	if (IsValid(LookAtPlayer)) LookAtPOI->SetLookAtActor(LookAtPlayer);
	else LookAtPOI->ClearLookAtTarget();
}

void AVBGASPNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVBGASPNPC, LookAtPlayer);
}
