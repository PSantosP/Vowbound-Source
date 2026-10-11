#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBCinematicPlaybackTypes.h"
#include "VBCinematicDirectorSubsystem.generated.h"

class APawn;
class APlayerController;
class APlayerState;
class UVBCinematicDefinition;
class UVBCinematicPlaybackSubsystem;

// Mandatory game policy: real save/input protection, then measured gameplay restoration.
class VOWBOUND_API FVBCinematicGameLease
{
public:
	virtual ~FVBCinematicGameLease() = default;
	virtual bool IsSaveHeld() const = 0;
	virtual void RestoreGameplay() = 0;
	virtual bool IsGameplayRestored() const = 0;
	// Idempotent. False is cleanup, not permission to release transition save holds.
	virtual void Finish(bool bSucceeded) = 0;
};

DECLARE_DELEGATE_RetVal_TwoParams(TSharedPtr<FVBCinematicGameLease>, FVBCinematicAcquireSafety,
	APlayerController*, FGuid);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBGameCinematicHandoffEnded,
	const FVBCinematicEndResult&, bool);

enum class EVBGameCinematicStartStatus : uint8
{
	Accepted, Busy, InvalidContext, SafetyUnavailable, CoreRejected
};

struct FVBGameCinematicStartResult
{
	EVBGameCinematicStartStatus Status = EVBGameCinematicStartStatus::InvalidContext;
	FVBCinematicStartResult Core;
};

UCLASS()
class VOWBOUND_API UVBCinematicDirectorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;

	bool InstallSafetyProvider(UObject* Owner, FVBCinematicAcquireSafety Provider);
	void RemoveSafetyProvider(UObject* Owner);
	FVBGameCinematicStartResult RequestPlay(UVBCinematicDefinition* Definition, APlayerState* Instigator,
		FName PlayerBindingTag, const TArray<FInstancedStruct>& OnCompletedEffects, AActor* Counterpart = nullptr);
	bool HasCompletedFor(const APawn* Pawn, const UVBCinematicDefinition* Definition) const;
	bool SkipActive();
	bool CancelActive();
	FVBGameCinematicHandoffEnded OnHandoffEnded;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	void HandleSessionEnded(const FVBCinematicEndResult& Result);
	bool TickHandoff(float DeltaTime);
	bool IsContextCurrent() const;
	void CloseRequest(bool bSucceeded);
	void ReleaseRequestData();
	void Stop();

	UPROPERTY(Transient)
	TObjectPtr<UVBCinematicDefinition> ActiveDefinition;
	UPROPERTY(Transient)
	TArray<FInstancedStruct> CompletionEffects;

	struct FCompletionReceipt { TWeakObjectPtr<APawn> Pawn; FGuid AttemptId; };
	TMap<TWeakObjectPtr<UVBCinematicDefinition>, FCompletionReceipt> Receipts;
	TWeakObjectPtr<UObject> ProviderOwner;
	FVBCinematicAcquireSafety AcquireSafety;
	TSharedPtr<FVBCinematicGameLease> Lease;
	TWeakObjectPtr<UVBCinematicPlaybackSubsystem> Playback;
	TWeakObjectPtr<APawn> ActivePawn;
	TWeakObjectPtr<APlayerController> ActiveController;
	TWeakObjectPtr<APlayerState> ActiveInstigator;
	TWeakObjectPtr<AActor> ActiveCounterpart;
	FGuid RequestId;
	FGuid AttemptId;
	FGuid SessionId;
	TOptional<FVBCinematicEndResult> EndResult;
	FDelegateHandle EndHandle;
	FTSTicker::FDelegateHandle TickerHandle;
	bool bHadCounterpart = false;
	bool bRestoreRequested = false;
	bool bAdmitted = false;
	bool bRestorationConfirmed = false;
	bool bProcessing = false;
	bool bStopping = true;
};
