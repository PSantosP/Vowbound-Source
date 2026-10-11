#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBLookAtPOIComponent.generated.h"

/** Game-thread target selection for the original GASP CR_AdjustToPOI rig. */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBLookAtPOIComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBLookAtPOIComponent();
	UFUNCTION(BlueprintCallable, Category="Vowbound|Look At") void SetLookAtActor(AActor* Target);
	UFUNCTION(BlueprintCallable, Category="Vowbound|Look At") void SetLookAtLocation(FVector WorldLocation);
	UFUNCTION(BlueprintCallable, Category="Vowbound|Look At") void ClearLookAtTarget();
	/** Explicit > conversation > lock-on > player attention > AI focus. Game thread only. */
	virtual bool ResolveTarget(FVector& OutWorldLocation) const;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bUseLockedTarget = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bUseAIFocus = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") FName TargetSocket = TEXT("head");
private:
	UPROPERTY(Transient) TWeakObjectPtr<AActor> TargetActor;
	FVector TargetLocation = FVector::ZeroVector;
	bool bHasLocation = false;
};
