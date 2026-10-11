#include "Narrative/Cinematic/VBCinematicStartupPolicy.h"
#include "Narrative/Cinematic/VBCinematicStartupLease.h"

#include "EnhancedInputComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCinematicDefinition.h"
#include "Vowbound/Vowbound.h"

bool UVBCinematicStartupPolicy::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UVBCinematicStartupPolicy::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bStopping = false;
	Director = Collection.InitializeDependency<UVBCinematicDirectorSubsystem>();
}

bool UVBCinematicStartupPolicy::BeginNewGameCinematic(APlayerController* PC, UVBCinematicDefinition* Definition, FGuid Attempt)
{
	TArray<FText> Errors;
	if (!VBCinematicStartup::ValidateDefinition(Definition, Errors))
	{
		VB_LOG(Error, "새 게임 시네마틱 정의 거절: %s", *Errors[0].ToString());
		return false;
	}
	if (bStopping || !Director.IsValid() || !IsValid(PC) || !PC->IsLocalController()
		|| !IsValid(PC->GetPawn()) || !PC->GetPawn()->HasAuthority() || !Attempt.IsValid()) return false;
	UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(PC);
	if (!Save || Save->GetGameStartContext().AttemptId != Attempt
		|| Save->GetGameStartContext().Intent != EVBGameStartIntent::NewGame) return false;
	if (StartedAttempts.Contains(Attempt)) return StartupLease && StartupLease->Matches(PC, Attempt);
	if (StartupLease)
	{
		VB_LOG(Warning, "같은 월드의 두 번째 시네마틱 부팅 거절: 기존 시도/보류 유지");
		return false;
	}
	TStrongObjectPtr<ThisClass> KeepAlive(this);
	StartedAttempts.Add(Attempt);
	PendingDefinition = Definition;
	StartupLease = MakeShared<FVBStartupLease>(this, PC, Attempt);
	const TSharedPtr<FVBStartupLease> Guard = StartupLease;
	if (!Guard->Initialize(Definition) || bStopping || !Director.IsValid()
		|| !Director->InstallSafetyProvider(this, FVBCinematicAcquireSafety::CreateUObject(this, &ThisClass::AcquireForController)))
	{
		Guard->Finish(false);
		VB_LOG(Warning, "새 게임 시네마틱 준비 거절: 초기화 보류 유지");
		return true;
	}
	bRequested = false;
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickStartup));
	return true;
}

TSharedPtr<FVBCinematicGameLease> UVBCinematicStartupPolicy::AcquireForController(APlayerController* PC, FGuid Attempt)
{
	return !bStopping && StartupLease && StartupLease->Matches(PC, Attempt) && StartupLease->IsCurrent()
		&& StartupLease->IsSaveHeld() && !StartupLease->IsFinished() ? StartupLease : nullptr;
}

bool UVBCinematicStartupPolicy::TickStartup(float DeltaTime)
{
	const TSharedPtr<FVBStartupLease> Guard = StartupLease;
	if (bStopping || !Guard || !Guard->IsCurrent())
	{
		if (Guard) Guard->Finish(false);
		TickerHandle.Reset();
		return false;
	}
	if (Guard->IsFinished()) { TickerHandle.Reset(); return false; }
	if (!bRequested && Guard->PrepareToPlay())
	{
		bRequested = true;
		Guard->CompleteInitialization();
		if (bStopping || !Guard->IsCurrent() || !Director.IsValid()) { Guard->Finish(false); return false; }
		const FVBGameCinematicStartResult Start = Director->RequestPlay(PendingDefinition, Guard->GetInstigator(), TEXT("Player"), {});
		if (Start.Status != EVBGameCinematicStartStatus::Accepted)
		{
			Guard->Finish(false);
			VB_LOG(Warning, "새 게임 시네마틱 재생 거절: 저장/준비 보류 유지");
		}
	}
	Guard->TickRestore();
	const bool bKeepTicking = !bStopping && !Guard->IsFinished();
	if (!bKeepTicking) TickerHandle.Reset();
	return bKeepTicking;
}

void UVBCinematicStartupPolicy::Stop()
{
	if (bStopping) return;
	bStopping = true;
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	if (Director.IsValid()) Director->RemoveSafetyProvider(this);
	if (StartupLease) StartupLease->Finish(false);
	StartupLease.Reset();
	PendingDefinition = nullptr;
}

void UVBCinematicStartupPolicy::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

void UVBCinematicStartupPolicy::OnWorldEndPlay(UWorld& World)
{
	Stop();
	Super::OnWorldEndPlay(World);
}
