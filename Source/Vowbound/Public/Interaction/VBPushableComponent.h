#pragma once

#include "Interaction/VBInteractableComponent.h"
#include "VBPushableComponent.generated.h"

class ACharacter;
class UVBPushableConfig;
class UAnimInstance;

UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBPushableComponent : public UVBInteractableComponent
{
	GENERATED_BODY()
public:
	UVBPushableComponent();
	const UVBPushableConfig* GetPushConfig() const;
	FTransform GetStandTransform() const;
	bool Reserve(ACharacter* Character);
	bool StartPushing(ACharacter* Character);
	void StopPushing(ACharacter* Character);
	void SetPushInput(ACharacter* Character, float Value);
	void RequestRelease(ACharacter* Character);
	bool GetHandTargets(UAnimInstance* Anim, FTransform& Left, FTransform& Right, float& Weight) const;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
private:
	TWeakObjectPtr<ACharacter> Pusher;
	FVector InitialLocation = FVector::ZeroVector;
	float Input = 0.0f;
	double LastInputTime = 0.0;
	uint8 PreviousMovementMode = 0;
	bool bMoving = false;
	bool bReleasing = false;
};
