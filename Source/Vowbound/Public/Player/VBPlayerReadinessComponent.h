#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "VBPlayerReadinessComponent.generated.h"

class APawn;
class AController;
class UAbilitySystemComponent;
class UVBPlayerRestoreComponent;
class UVBUIManagerSubsystem;
class UVBSaveParticipantSubsystem;
class UVBSaveGameSubsystem;
enum class EVBGameStartIntent : uint8;

enum class EVBPlayerPreparationState : uint8 { Ready, Blocked, Unavailable };

enum class EVBPlayerPreparationReason : uint32
{
	Initialization = 1 << 0, SaveApplication = 1 << 1, Restore = 1 << 2,
	Interaction = 1 << 3, UI = 1 << 4, ExternalHold = 1 << 5,
	Pawn = 1 << 6, Controller = 1 << 7, Authority = 1 << 8,
	AbilitySystem = 1 << 9, SaveSource = 1 << 10, RestoreSource = 1 << 11,
	UISource = 1 << 12, World = 1 << 13, Dead = 1 << 14, HoldOwner = 1 << 15
};

struct FVBPlayerPreparationSnapshot
{
	EVBPlayerPreparationState State = EVBPlayerPreparationState::Unavailable;
	FGuid InitializationId;
	uint32 Revision = 0;
	uint32 BlockedReasons = 0;
	uint32 UnavailableReasons = 0;
};

struct FVBPlayerPreparationHoldToken
{
	FGuid Id;
	FGuid InitializationId;
};

UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBPlayerReadinessComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBPlayerReadinessComponent();
	static FVBPlayerPreparationSnapshot QueryForPawn(const APawn* Pawn);
	FVBPlayerPreparationSnapshot Query() const;
	static EVBPlayerPreparationState EvaluateReasons(uint32 BlockedReasons, uint32 UnavailableReasons);
	// Requery current facts after subscribing; this notification is not a Ready command.
	FSimpleMulticastDelegate OnPreparationChanged;

	void BeginInitialization(FGuid Id, EVBGameStartIntent Intent);
	void CompleteInitialization(FGuid Id, bool bContinueDispatched);
	FVBPlayerPreparationHoldToken AcquirePreparationHold(UObject* Owner, FName Reason);
	bool CanFinishPreparationHold(const FVBPlayerPreparationHoldToken& Token) const;
	void ReleasePreparationHold(const FVBPlayerPreparationHoldToken& Token);

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBPlayerReadinessTestAccess;
#endif
	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
	void RefreshBindings();
	void DisconnectProviders();
	void NotifyChanged();
	void HandleTagChanged(FGameplayTag Tag, int32 Count);
	bool NotifyAfterTagChange(float DeltaSeconds);
	FTSTicker::FDelegateHandle TagNotificationHandle;
	uint32 ProviderEpoch = 0;
	bool bTagNotificationPending = false;
	struct FPreparationHold { TWeakObjectPtr<UObject> Owner; FName Reason; };
	TMap<FGuid, FPreparationHold> Holds;
	FGuid InitializationId;
	EVBGameStartIntent StartIntent{};
	bool bInitializationEnded = false;
	bool bContinueDispatched = false;
	bool bEnding = false;
	uint32 Revision = 0;
	TWeakObjectPtr<APawn> BoundPawn;
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystem;
	TWeakObjectPtr<UVBPlayerRestoreComponent> Restore;
	TWeakObjectPtr<UVBUIManagerSubsystem> UI;
	TWeakObjectPtr<UVBSaveParticipantSubsystem> Registry;
	TWeakObjectPtr<UVBSaveGameSubsystem> Save;
	FDelegateHandle InteractionHandle;
	FDelegateHandle DeadHandle;
	FDelegateHandle RestoreHandle;
	FDelegateHandle UIHandle;
	FDelegateHandle SaveAppliedHandle;
	FDelegateHandle StartContextHandle;
};
