#include "Character/VBSlideMovementComponent.h"

#include "Character/VBCharacter.h"
#include "AbilitySystem/Abilities/VBGA_Crouch.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

class FSavedMove_VBSlide final : public FSavedMove_Character
{
public:
	bool bSavedWantsToSlide = false;
	bool bSavedSlideActive = false;
	bool bSavedCrouchAfterSlide = false;
	float SavedSlideElapsed = 0.f;

	virtual void Clear() override
	{
		FSavedMove_Character::Clear();
		bSavedWantsToSlide = bSavedSlideActive = false;
		bSavedCrouchAfterSlide = false;
		SavedSlideElapsed = 0.f;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		return FSavedMove_Character::GetCompressedFlags() | (bSavedWantsToSlide ? FLAG_Custom_0 : 0)
			| (bSavedCrouchAfterSlide ? FLAG_Custom_1 : 0);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, const FVector& NewAcceleration,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, InDeltaTime, NewAcceleration, ClientData);
		if (const auto* Movement = Cast<UVBSlideMovementComponent>(Character->GetCharacterMovement()))
		{
			bSavedWantsToSlide = Movement->bWantsToSlide;
			bSavedSlideActive = Movement->bSlideActive;
			bSavedCrouchAfterSlide = Movement->bCrouchAfterSlide;
			SavedSlideElapsed = Movement->SlideElapsed;
		}
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		FSavedMove_Character::PrepMoveFor(Character);
		if (auto* Movement = Cast<UVBSlideMovementComponent>(Character->GetCharacterMovement()))
		{
			Movement->bWantsToSlide = bSavedWantsToSlide;
			Movement->bSlideActive = bSavedSlideActive;
			Movement->bCrouchAfterSlide = bSavedCrouchAfterSlide;
			Movement->SlideElapsed = SavedSlideElapsed;
			if (AVBCharacter* Player = Cast<AVBCharacter>(Character))
				Player->OnSlideMovementStateChanged(bSavedSlideActive);
		}
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		const auto* Other = static_cast<const FSavedMove_VBSlide*>(NewMove.Get());
		return !bSavedSlideActive && !Other->bSavedSlideActive &&
			bSavedWantsToSlide == Other->bSavedWantsToSlide &&
			bSavedCrouchAfterSlide == Other->bSavedCrouchAfterSlide &&
			FSavedMove_Character::CanCombineWith(NewMove, Character, MaxDelta);
	}
};

class FNetworkPredictionData_Client_VBSlide final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FNetworkPredictionData_Client_VBSlide(const UCharacterMovementComponent& Movement)
		: FNetworkPredictionData_Client_Character(Movement) {}
	virtual FSavedMovePtr AllocateNewMove() override { return MakeShared<FSavedMove_VBSlide>(); }
};

void UVBSlideMovementComponent::RequestSlide()
{
	if (CanRequestSlide())
	{
		bWantsToSlide = true;
		bCrouchAfterSlide = false;
	}
}

bool UVBSlideMovementComponent::CanRequestSlide() const
{
	const AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner);
	const UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	const UAnimInstance* Anim = Player && Player->GetMesh() ? Player->GetMesh()->GetAnimInstance() : nullptr;
	return !bSlideActive && SlideEndSpeed < SlideStartSpeed && IsMovingOnGround() && Velocity.Size2D() > SlideStartSpeed && Player
		&& (!ASC || !ASC->HasAnyMatchingGameplayTags(UVBGA_Crouch::GetPostureBlockingTags()))
		&& (!Anim || !Anim->IsAnyMontagePlaying());
}

void UVBSlideMovementComponent::SetSlideActive(bool bNewActive)
{
	if (bSlideActive == bNewActive) return;
	bSlideActive = bNewActive;
	if (bNewActive) SlideElapsed = 0.f;
	if (AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner))
		Player->OnSlideMovementStateChanged(bNewActive);
	if (!bNewActive && CharacterOwner)
	{
		if (bCrouchAfterSlide) CharacterOwner->Crouch();
		else CharacterOwner->UnCrouch();
	}
}

void UVBSlideMovementComponent::CancelSlide(bool bRemainCrouched)
{
	bWantsToSlide = false;
	bCrouchAfterSlide = bRemainCrouched;
	SetSlideActive(false);
}

void UVBSlideMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSlide = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bCrouchAfterSlide = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UVBSlideMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UVBSlideMovementComponent* MutableThis = const_cast<UVBSlideMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_VBSlide(*this);
	}
	return ClientPredictionData;
}

float UVBSlideMovementComponent::GetMaxSpeed() const
{
	return bSlideActive && IsMovingOnGround()
		? FMath::Min(SlideMaxSpeed, SlideSettings.GetTargetSpeed(GetSlideSlopeAngle(), SlideElapsed))
		: Super::GetMaxSpeed();
}

float UVBSlideMovementComponent::GetSlideSlopeAngle() const
{
	const FVector Direction = Acceleration.GetSafeNormal2D();
	if (!CurrentFloor.IsWalkableFloor() || Direction.IsNearlyZero()) return 0.f;
	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
		FVector::DotProduct(CurrentFloor.HitResult.Normal, Direction), -1.f, 1.f))) - 90.f;
}

void UVBSlideMovementComponent::PhysicsRotation(float DeltaTime)
{
	// Owner Tick은 CMC 뒤다. 예측/서버 submove의 현재 가속으로 회전율을 정한 뒤 엔진 회전을 실행한다.
	if (AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner))
		Player->UpdateRotationRateForMovementMode();
	Super::PhysicsRotation(DeltaTime);
}

void UVBSlideMovementComponent::UpdateCharacterStateBeforeMovement(float DeltaSeconds)
{
	Super::UpdateCharacterStateBeforeMovement(DeltaSeconds); // Process predicted crouch first.
	const AVBCharacter* Player = Cast<AVBCharacter>(CharacterOwner);
	const UAbilitySystemComponent* ASC = Player ? Player->GetAbilitySystemComponent() : nullptr;
	if (bSlideActive && (!IsMovingOnGround() || !IsCrouching() || !bWantsToSlide
		|| (ASC && ASC->HasAnyMatchingGameplayTags(UVBGA_Crouch::GetPostureBlockingTags()))))
		CancelSlide(IsMovingOnGround() && !bWantsToSlide && bCrouchAfterSlide);
	if (bWantsToSlide && !bSlideActive)
	{
		if (CanRequestSlide() && IsCrouching())
		{
			SetSlideActive(true);
		}
		else if (!CanRequestSlide())
		{
			bWantsToSlide = false;
		}
	}
}

void UVBSlideMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (!bSlideActive || !IsMovingOnGround())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}
	const FVector SavedAcceleration = Acceleration;
	const float SlopeAngle = GetSlideSlopeAngle();
	const float SlideAcceleration = SlideElapsed < SlideSettings.InitialBoostTime
		? SlideSettings.InitialBoostAcceleration : SlideSettings.AfterBoostAcceleration;
	Acceleration = SavedAcceleration.GetSafeNormal2D() * SlideAcceleration;
	Super::CalcVelocity(DeltaTime, 0.f, bFluid, SlideSettings.GetDeceleration(SlopeAngle));
	Acceleration = SavedAcceleration;
	Velocity.Z = 0.f; // PhysWalking projects horizontal velocity onto the floor ramp itself.
}

void UVBSlideMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
	if (!bSlideActive) return;
	SlideElapsed += DeltaSeconds;
	if (!IsMovingOnGround() || (SlideMaxDuration > 0.f && SlideElapsed >= SlideMaxDuration) ||
		Velocity.Size2D() <= SlideEndSpeed)
		CancelSlide(bCrouchAfterSlide);
}
