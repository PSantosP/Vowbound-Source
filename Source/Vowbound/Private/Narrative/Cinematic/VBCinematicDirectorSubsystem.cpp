#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ScopeExit.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCinematicDefinition.h"
#include "VBCinematicPlaybackSubsystem.h"

bool UVBCinematicDirectorSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UVBCinematicDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bStopping = false;
	Playback = Collection.InitializeDependency<UVBCinematicPlaybackSubsystem>();
	if (Playback.IsValid())
	{
		EndHandle = Playback->OnSessionEndedNative.AddUObject(this, &ThisClass::HandleSessionEnded);
	}
}

void UVBCinematicDirectorSubsystem::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

void UVBCinematicDirectorSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	Stop();
	Super::OnWorldEndPlay(InWorld);
}

void UVBCinematicDirectorSubsystem::Stop()
{
	if (bStopping) return;
	bStopping = true;
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	if (Playback.IsValid()) Playback->OnSessionEndedNative.Remove(EndHandle);
	if (RequestId.IsValid())
	{
		if (Playback.IsValid() && SessionId.IsValid()) Playback->Cancel(SessionId);
		if (!EndResult.IsSet()) EndResult = FVBCinematicEndResult{
			SessionId, EVBCinematicEndType::Aborted, EVBCinematicEndReason::WorldEnded};
		CloseRequest(false);
	}
	Playback.Reset();
	AcquireSafety.Unbind();
	ProviderOwner.Reset();
	Receipts.Reset();
}

bool UVBCinematicDirectorSubsystem::InstallSafetyProvider(UObject* Owner, FVBCinematicAcquireSafety Provider)
{
	if (bStopping || bProcessing || RequestId.IsValid() || !IsValid(Owner) || !Provider.IsBound()) return false;
	if (ProviderOwner.IsValid() && ProviderOwner.Get() != Owner) return false;
	ProviderOwner = Owner;
	AcquireSafety = MoveTemp(Provider);
	return true;
}

void UVBCinematicDirectorSubsystem::RemoveSafetyProvider(UObject* Owner)
{
	if (!Owner || ProviderOwner.Get() != Owner) return;
	ProviderOwner.Reset();
	AcquireSafety.Unbind();
	CancelActive();
}

FVBGameCinematicStartResult UVBCinematicDirectorSubsystem::RequestPlay(UVBCinematicDefinition* Definition,
	APlayerState* Instigator, FName PlayerBindingTag, const TArray<FInstancedStruct>& Effects, AActor* Counterpart)
{
	FVBGameCinematicStartResult Result;
	if (bProcessing || RequestId.IsValid()) { Result.Status = EVBGameCinematicStartStatus::Busy; return Result; }
	UWorld* World = GetWorld();
	APlayerController* PC = IsValid(Instigator) ? Instigator->GetPlayerController() : nullptr;
	APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	UVBSaveGameSubsystem* Save = IsValid(Pawn) ? UVBSaveGameSubsystem::Get(Pawn) : nullptr;
	if (bStopping || !IsValid(World) || World->bIsTearingDown || World->GetNetMode() == NM_Client
		|| !IsValid(Definition) || !IsValid(PC) || !PC->IsLocalController() || !IsValid(Pawn)
		|| Pawn->GetWorld() != World || Pawn->GetPlayerState() != Instigator || !Save
		|| (Counterpart && (!IsValid(Counterpart) || Counterpart->GetWorld() != World))
		|| !Save->GetGameStartContext().AttemptId.IsValid() || !Playback.IsValid()) return Result;
	for (const FInstancedStruct& Effect : Effects) if (!Effect.GetPtr<FVBEffect>()) return Result;
	if (!ProviderOwner.IsValid() || !AcquireSafety.IsBound())
	{
		Result.Status = EVBGameCinematicStartStatus::SafetyUnavailable;
		return Result;
	}

	TStrongObjectPtr<ThisClass> KeepAlive(this);
	ON_SCOPE_EXIT { if (!RequestId.IsValid()) ReleaseRequestData(); };
	TGuardValue<bool> Processing(bProcessing, true);
	RequestId = FGuid::NewGuid();
	const FGuid ThisRequest = RequestId;
	AttemptId = Save->GetGameStartContext().AttemptId;
	ActivePawn = Pawn;
	ActiveController = PC;
	ActiveInstigator = Instigator;
	ActiveCounterpart = Counterpart;
	bHadCounterpart = Counterpart != nullptr;
	ActiveDefinition = Definition;
	CompletionEffects = Effects;
	if (!IsContextCurrent()) { CloseRequest(false); return Result; }
	const FVBCinematicAcquireSafety Factory = AcquireSafety;
	TSharedPtr<FVBCinematicGameLease> AcquiredLease = Factory.Execute(PC, AttemptId);
	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		if (AcquiredLease) AcquiredLease->Finish(false);
		CloseRequest(false);
		return Result;
	}
	Lease = MoveTemp(AcquiredLease);
	const TSharedPtr<FVBCinematicGameLease> Guard = Lease;
	const bool bHeld = Guard && Guard->IsSaveHeld();
	if (!bHeld || RequestId != ThisRequest || !IsContextCurrent())
	{
		CloseRequest(false);
		Result.Status = EVBGameCinematicStartStatus::SafetyUnavailable;
		return Result;
	}
	TArray<FVBCinematicBinding> Bindings;
	if (!PlayerBindingTag.IsNone())
	{
		FVBCinematicBinding Binding;
		Binding.Tag = PlayerBindingTag;
		Binding.Actors.Add(Pawn);
		Bindings.Add(MoveTemp(Binding));
	}
	Result.Core = Playback->TryPlay(Definition, Bindings);
	if (Result.Core.Status != EVBCinematicStartStatus::Accepted)
	{
		CloseRequest(false);
		Result.Status = EVBGameCinematicStartStatus::CoreRejected;
		return Result;
	}
	SessionId = Result.Core.SessionId;
	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		if (Playback.IsValid()) Playback->Cancel(Result.Core.SessionId);
		CloseRequest(false);
		return Result;
	}
	Result.Status = EVBGameCinematicStartStatus::Accepted;
	bAdmitted = true;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickHandoff));
	return Result;
}

bool UVBCinematicDirectorSubsystem::IsContextCurrent() const
{
	const APawn* Pawn = ActivePawn.Get();
	const APlayerController* PC = ActiveController.Get();
	const UWorld* World = GetWorld();
	if (bStopping || !RequestId.IsValid() || !IsValid(World) || World->bIsTearingDown
		|| !ProviderOwner.IsValid() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()
		|| !IsValid(PC) || PC->IsActorBeingDestroyed() || Pawn->GetWorld() != World
		|| PC->GetWorld() != World
		|| PC->GetPawn() != Pawn || Pawn->GetController() != PC
		|| !ActiveInstigator.IsValid() || Pawn->GetPlayerState() != ActiveInstigator.Get()
		|| (bHadCounterpart && (!ActiveCounterpart.IsValid() || ActiveCounterpart->IsActorBeingDestroyed()))) return false;
	const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(Pawn);
	const FVBPlayerPreparationSnapshot Preparation = UVBPlayerReadinessComponent::QueryForPawn(Pawn);
	return Save && Save->GetGameStartContext().AttemptId == AttemptId
		&& Preparation.InitializationId == AttemptId && Preparation.State != EVBPlayerPreparationState::Unavailable;
}

void UVBCinematicDirectorSubsystem::HandleSessionEnded(const FVBCinematicEndResult& Result)
{
	if (!bStopping && RequestId.IsValid() && Result.SessionId == SessionId && !EndResult.IsSet()) EndResult = Result;
}

bool UVBCinematicDirectorSubsystem::TickHandoff(float DeltaTime)
{
	if (bStopping || !RequestId.IsValid()) { TickerHandle.Reset(); return false; }
	if (bProcessing) return true;
	TStrongObjectPtr<ThisClass> KeepAlive(this);
	ON_SCOPE_EXIT { if (!RequestId.IsValid()) ReleaseRequestData(); };
	TGuardValue<bool> Processing(bProcessing, true);
	const FGuid ThisRequest = RequestId;
	const TSharedPtr<FVBCinematicGameLease> Guard = Lease;
	const bool bHeld = Guard && Guard->IsSaveHeld();
	if (!bHeld || RequestId != ThisRequest || !IsContextCurrent())
	{
		if (Playback.IsValid() && SessionId.IsValid()) Playback->Cancel(SessionId);
		CloseRequest(false);
		TickerHandle.Reset();
		return false;
	}
	if (!EndResult.IsSet()) return true;
	if (EndResult->Type != EVBCinematicEndType::Completed && EndResult->Type != EVBCinematicEndType::Skipped)
	{
		CloseRequest(false); TickerHandle.Reset(); return false;
	}
	if (!bRestoreRequested)
	{
		bRestoreRequested = true;
		Guard->RestoreGameplay();
	}
	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		CloseRequest(false); TickerHandle.Reset(); return false;
	}
	const bool bRestored = Guard->IsGameplayRestored();
	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		CloseRequest(false); TickerHandle.Reset(); return false;
	}
	if (!bRestored || UVBPlayerReadinessComponent::QueryForPawn(ActivePawn.Get()).State != EVBPlayerPreparationState::Ready)
	{
		return true;
	}
	bRestorationConfirmed = true;
	// Retain reflected data until the external effect stack unwinds, even on synchronous Stop.
	for (int32 Index = 0; Index < CompletionEffects.Num(); ++Index)
	{
		const bool bStillHeld = Guard->IsSaveHeld();
		if (!bStillHeld || RequestId != ThisRequest || !IsContextCurrent())
		{
			CloseRequest(false); TickerHandle.Reset(); return false;
		}
		FVBRuleContext Context = FVBRuleContext::Make(this, ActiveInstigator.Get());
		Context.Counterpart = ActiveCounterpart.Get();
		const FVBEffect* Effect = CompletionEffects[Index].GetPtr<FVBEffect>();
		if (!Context.CanApplyEffects() || !Effect)
		{
			CloseRequest(false); TickerHandle.Reset(); return false;
		}
		Effect->Apply(Context);
	}
	const bool bStillHeld = Guard->IsSaveHeld();
	CloseRequest(bStillHeld && RequestId == ThisRequest && IsContextCurrent());
	TickerHandle.Reset();
	return false;
}

void UVBCinematicDirectorSubsystem::CloseRequest(bool bSucceeded)
{
	if (!RequestId.IsValid()) return;
	bSucceeded = bSucceeded && bRestorationConfirmed;
	const FVBCinematicEndResult Result = EndResult.Get(FVBCinematicEndResult{
		SessionId, EVBCinematicEndType::Aborted, EVBCinematicEndReason::Cancelled});
	const TWeakObjectPtr<APawn> Pawn = ActivePawn;
	const TWeakObjectPtr<APlayerController> Controller = ActiveController;
	const TWeakObjectPtr<APlayerState> Instigator = ActiveInstigator;
	const TWeakObjectPtr<UVBCinematicDefinition> Definition = ActiveDefinition;
	const FGuid CompletedAttempt = AttemptId;
	const bool bWasAdmitted = bAdmitted;
	TSharedPtr<FVBCinematicGameLease> FinishedLease = MoveTemp(Lease);
	RequestId.Invalidate();
	SessionId.Invalidate();
	EndResult.Reset();
	bRestoreRequested = false;
	bAdmitted = false;
	bRestorationConfirmed = false;
	if (FinishedLease) FinishedLease->Finish(bSucceeded);
	bSucceeded = bSucceeded && !bStopping && Definition.IsValid() && Pawn.IsValid()
		&& !Pawn->IsActorBeingDestroyed() && Controller.IsValid() && !Controller->IsActorBeingDestroyed()
		&& Pawn->GetWorld() == GetWorld() && Pawn->GetController() == Controller.Get()
		&& Controller->GetPawn() == Pawn.Get() && Instigator.IsValid() && Pawn->GetPlayerState() == Instigator.Get();
	if (bSucceeded)
	{
		const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(Pawn.Get());
		// 완료 효과가 새 UI/상호작용 보류를 만들 수 있다. 복귀는 효과 전에 확인했다.
		bSucceeded = Save && Save->GetGameStartContext().AttemptId == CompletedAttempt;
	}
	if (bSucceeded && Definition.IsValid()) Receipts.Add(Definition, {Pawn, CompletedAttempt});
	if (bWasAdmitted && Result.SessionId.IsValid()) OnHandoffEnded.Broadcast(Result, bSucceeded);
	if (!bProcessing) ReleaseRequestData();
}

void UVBCinematicDirectorSubsystem::ReleaseRequestData()
{
	CompletionEffects.Reset();
	ActiveDefinition = nullptr;
	ActivePawn.Reset();
	ActiveController.Reset();
	ActiveInstigator.Reset();
	ActiveCounterpart.Reset();
	AttemptId.Invalidate();
	bHadCounterpart = false;
}

bool UVBCinematicDirectorSubsystem::HasCompletedFor(const APawn* Pawn, const UVBCinematicDefinition* Definition) const
{
	if (bStopping || !IsValid(Pawn) || !IsValid(Definition) || Pawn->GetWorld() != GetWorld()) return false;
	const FCompletionReceipt* Receipt = Receipts.Find(TWeakObjectPtr<UVBCinematicDefinition>(const_cast<UVBCinematicDefinition*>(Definition)));
	const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(Pawn);
	return Receipt && Receipt->Pawn.Get() == Pawn && Save && Save->GetGameStartContext().AttemptId == Receipt->AttemptId;
}

bool UVBCinematicDirectorSubsystem::SkipActive()
{
	return !bStopping && !bProcessing && Playback.IsValid() && SessionId.IsValid() && !EndResult.IsSet()
		&& Playback->Skip(SessionId);
}

bool UVBCinematicDirectorSubsystem::CancelActive()
{
	if (bStopping || bProcessing || !RequestId.IsValid()) return false;
	if (!EndResult.IsSet() && Playback.IsValid() && SessionId.IsValid()) return Playback->Cancel(SessionId);
	TStrongObjectPtr<ThisClass> KeepAlive(this);
	ON_SCOPE_EXIT { if (!RequestId.IsValid()) ReleaseRequestData(); };
	TGuardValue<bool> Processing(bProcessing, true);
	if (!EndResult.IsSet()) EndResult = FVBCinematicEndResult{
		SessionId, EVBCinematicEndType::Aborted, EVBCinematicEndReason::Cancelled};
	CloseRequest(false);
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	return true;
}
