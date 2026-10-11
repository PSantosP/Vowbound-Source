#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "VBLookAtPOIAnimInstance.generated.h"

class UDataAsset;
class FArrayProperty;

/** Linked input pose -> original GASP Control Rig. No sample animation or BP decision graph. */
UCLASS(Transient, Blueprintable)
class VOWBOUND_API UVBLookAtPOIAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Look At") TObjectPtr<UDataAsset> LookAtSettings;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Look At") FVector LookAtTarget = FVector::ZeroVector;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Look At") bool bLookAtEnabled = false;
	UPROPERTY(Transient, BlueprintReadOnly, Category="Look At") bool bInteractionAttention = false;
	/** Wildcard arrays preserve the original rig's user-defined struct input contract. */
	UFUNCTION(BlueprintPure, CustomThunk, Category="Look At", meta=(ArrayParm="Values", BlueprintThreadSafe))
	void GetLookAtWeightOverrides(TArray<int32>& Values) const;
	DECLARE_FUNCTION(execGetLookAtWeightOverrides);
	UFUNCTION(BlueprintPure, CustomThunk, Category="Look At", meta=(ArrayParm="Values", BlueprintThreadSafe))
	void GetLookAtLimitOverrides(TArray<int32>& Values) const;
	DECLARE_FUNCTION(execGetLookAtLimitOverrides);
private:
	UPROPERTY(Transient) TArray<FName> BodyPartLabels;
	void BuildCurveOverrides(FArrayProperty* ArrayProperty, void* ArrayAddress, bool bLimits) const;
};
