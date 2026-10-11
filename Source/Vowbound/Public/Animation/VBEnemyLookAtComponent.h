#pragma once

#include "CoreMinimal.h"
#include "Animation/VBLookAtPOIComponent.h"
#include "VBEnemyLookAtComponent.generated.h"

/** Replicates the AI's visual focus without changing its combat or camera decisions. */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBEnemyLookAtComponent : public UVBLookAtPOIComponent
{
	GENERATED_BODY()
public:
	UVBEnemyLookAtComponent();
	virtual bool ResolveTarget(FVector& OutWorldLocation) const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
private:
	void UpdateFocusActor();
	UFUNCTION() void OnRep_FocusActor();
	UPROPERTY(ReplicatedUsing=OnRep_FocusActor) TObjectPtr<AActor> FocusActor;
};
