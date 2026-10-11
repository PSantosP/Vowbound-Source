#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Types/VBSlideMovementSettings.h"
#include "VBSlideMovementComponent.generated.h"

class FSavedMove_VBSlide;

/** CMC walking-floor slide: collision and crouch remain on the engine's predicted walking path. */
UCLASS()
class VOWBOUND_API UVBSlideMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	void RequestSlide();
	void CancelSlide(bool bRemainCrouched = false);
	bool IsSliding() const { return bSlideActive; }
	bool CanRequestSlide() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Slide", meta=(ClampMin="0"))
	float SlideStartSpeed = 380.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Slide", meta=(ClampMin="0"))
	float SlideEndSpeed = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Slide", meta=(ClampMin="0", ToolTip="0 disables the optional duration limit"))
	float SlideMaxDuration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Slide")
	FVBSlideMovementSettings SlideSettings;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Slide", meta=(ClampMin="0"))
	float SlideMaxSpeed = 800.f;

	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual float GetMaxSpeed() const override;
	virtual void UpdateCharacterStateBeforeMovement(float DeltaSeconds) override;
	virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void PhysicsRotation(float DeltaTime) override;

private:
	friend class FSavedMove_VBSlide;
	bool bWantsToSlide = false;
	bool bSlideActive = false;
	bool bCrouchAfterSlide = false;
	float SlideElapsed = 0.f;
	float GetSlideSlopeAngle() const;
	void SetSlideActive(bool bNewActive);
};
