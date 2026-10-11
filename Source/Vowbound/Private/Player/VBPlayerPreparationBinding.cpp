#include "Player/VBPlayerPreparationBinding.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

FVBPlayerPreparationBinding::~FVBPlayerPreparationBinding() { Disconnect(); }
APawn* FVBPlayerPreparationBinding::GetPawn() const { return Pawn.Get(); }
APlayerController* FVBPlayerPreparationBinding::GetController() const { return Controller.Get(); }

void FVBPlayerPreparationBinding::Refresh(UObject* Owner, UWorld* World, FSimpleDelegate Requery)
{
 if (!IsValid(Owner) || !World) { Disconnect(); return; }
 if (BoundOwner.Get() != Owner || BoundWorld.Get() != World) Disconnect();
 BoundOwner = Owner;
 BoundWorld = World;
 RequestRequery = MoveTemp(Requery);
 // 교체 컨트롤러가 먼저 스폰되는 순서도 지원하므로 발견 후 구독을 끊지 않는다.
 if (!SpawnHandle.IsValid())
 {
  const auto OnControllerChanged = [this](AActor* Actor)
  {
   if (Actor && Actor->IsA<APlayerController>()) RequestRequery.ExecuteIfBound();
  };
  SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateWeakLambda(Owner, OnControllerChanged));
  DestroyHandle = World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateWeakLambda(Owner, OnControllerChanged));
 }
 APlayerController* Current = World->GetFirstPlayerController();
 if (!IsValid(Current) || Current->IsActorBeingDestroyed()) Current = nullptr;
 if (Controller.Get() != Current)
 {
  if (APlayerController* Old = Controller.Get()) Old->GetOnNewPawnNotifier().Remove(PawnHandle);
  PawnHandle.Reset();
  Controller = Current;
  if (Current)
   PawnHandle = Current->GetOnNewPawnNotifier().AddWeakLambda(Owner, [this](APawn*) { RequestRequery.ExecuteIfBound(); });
 }
 // 구독한 다음 현재 값을 읽어 이미 끝난 빙의도 놓치지 않는다.
 APawn* CurrentPawn = Current ? Current->GetPawn() : nullptr;
 UVBPlayerReadinessComponent* CurrentPreparation = IsValid(CurrentPawn)
  ? CurrentPawn->FindComponentByClass<UVBPlayerReadinessComponent>() : nullptr;
 if (Pawn.Get() != CurrentPawn || Preparation.Get() != CurrentPreparation)
 {
  if (UVBPlayerReadinessComponent* Old = Preparation.Get()) Old->OnPreparationChanged.Remove(PreparationHandle);
  PreparationHandle.Reset();
  Pawn = CurrentPawn;
  Preparation = CurrentPreparation;
  if (CurrentPreparation)
   PreparationHandle = CurrentPreparation->OnPreparationChanged.AddWeakLambda(Owner, [this]() { RequestRequery.ExecuteIfBound(); });
 }
}

void FVBPlayerPreparationBinding::Disconnect()
{
 if (UVBPlayerReadinessComponent* Old = Preparation.Get()) Old->OnPreparationChanged.Remove(PreparationHandle);
 if (APlayerController* Old = Controller.Get()) Old->GetOnNewPawnNotifier().Remove(PawnHandle);
 if (UWorld* World = BoundWorld.Get())
 {
  if (SpawnHandle.IsValid()) World->RemoveOnActorSpawnedHandler(SpawnHandle);
  if (DestroyHandle.IsValid()) World->RemoveOnActorDestroyedHandler(DestroyHandle);
 }
 Preparation.Reset(); Pawn.Reset(); Controller.Reset(); BoundOwner.Reset(); BoundWorld.Reset();
 SpawnHandle.Reset(); DestroyHandle.Reset(); PawnHandle.Reset(); PreparationHandle.Reset();
 RequestRequery.Unbind();
}
