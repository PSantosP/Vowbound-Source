#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBEnvironmentCameraComponent.generated.h"

class UBlueprintCameraDirectorEvaluator;
class UCameraRigAsset;
class UGameplayCameraComponentBase;
class UVBEnvironmentCameraConfig;
class UCameraAsset;

/** Selects the local collision owner at Director activation; never writes from Actor Tick. */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBEnvironmentCameraComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBEnvironmentCameraComponent();
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	TObjectPtr<UVBEnvironmentCameraConfig> Config;

	/** Replaces only the Director's collision activation. Failure retains the original rig. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Camera")
	static void ActivateEnvironmentCollision(UObject* EvaluationContextOwner,
		UBlueprintCameraDirectorEvaluator* Director, UCameraRigAsset* FallbackRig);

	void ReleaseCollisionOwnership();
	bool OwnsCollision() const;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	bool Prepare(UGameplayCameraComponentBase* Camera);
	TWeakObjectPtr<UGameplayCameraComponentBase> ActiveCamera;
	TWeakObjectPtr<UCameraAsset> ActiveCameraAsset;
};
