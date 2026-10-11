#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "Character/VBCharacter.h"
#include "VBGASPNPC.generated.h"
class UStateTree;
class UStateTreeAIComponent;
class UVBLookAtInterestComponent;

/** Project-owned NPC running the ported GASP SmartObject/patrol StateTree. */
UCLASS()
class VOWBOUND_API AVBGASPNPCController : public AAIController
{
	GENERATED_BODY()
public:
	AVBGASPNPCController();
	UPROPERTY(EditDefaultsOnly, Category="GASP") TObjectPtr<UStateTree> BehaviorStateTree;
	UPROPERTY(EditDefaultsOnly, Category="GASP") float InitialDelay = 2.f;
	UPROPERTY(EditDefaultsOnly, Category="GASP") float DedicatedServerInitialDelay = 8.f;
protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStateTreeAIComponent> StateTree;
	FTimerHandle StartTimer;
	void StartBehavior();
};

UCLASS()
class VOWBOUND_API AVBGASPNPC : public AVBCharacter
{
	GENERATED_BODY()
public:
	AVBGASPNPC(const FObjectInitializer& ObjectInitializer);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NPC|Look At") TObjectPtr<UVBLookAtInterestComponent> LookAtInterest;
	/** Disable only for a stationary preview or an NPC with externally owned behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Behavior") bool bEnableAmbientBehavior = true;
	/** Opt-in visual attention; does not lock the camera or rotate the AI controller. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Look At") bool bLookAtNearbyPlayers = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Look At", meta=(ClampMin="0", Units="cm")) float LookAtAcquireDistance = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="NPC|Look At", meta=(ClampMin="0", Units="cm")) float LookAtReleaseDistance = 500.f;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	void UpdatePlayerLookAt();
	UFUNCTION() void OnRep_LookAtPlayer();
	UPROPERTY(ReplicatedUsing=OnRep_LookAtPlayer) TObjectPtr<APawn> LookAtPlayer;
	FTimerHandle LookAtTimer;
};
