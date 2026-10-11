#pragma once

#include "Interaction/VBInteractableConfig.h"
#include "VBPushableConfig.generated.h"

// 평탄한 바닥의 레일형 밀기. 오프셋은 액터 회전 기준 cm이며 메시 스케일을 중복 적용하지 않는다.
UCLASS(BlueprintType)
class VOWBOUND_API UVBPushableConfig : public UVBInteractableConfig
{
	GENERATED_BODY()
public:
	UVBPushableConfig() { Mode = EVBInteractMode::Push; }
	UPROPERTY(EditDefaultsOnly, Category="Push")
	FVector StandOffset = FVector(-110, 0, -40);
	UPROPERTY(EditDefaultsOnly, Category="Push")
	FVector MoveAxis = FVector::XAxisVector;
	UPROPERTY(EditDefaultsOnly, Category="Push", meta=(ClampMin="1"))
	float MoveSpeed = 70.0f;
	// 보행 클립 1배속에 해당하는 이동 속도(cm/s).
	UPROPERTY(EditDefaultsOnly, Category="Push", meta=(ClampMin="1"))
	float AnimationReferenceSpeed = 70.0f;
	UPROPERTY(EditDefaultsOnly, Category="Push", meta=(ClampMin="1"))
	float PullAnimationReferenceSpeed = 70.0f;
	UPROPERTY(EditDefaultsOnly, Category="Push", meta=(ClampMin="0.01"))
	float SectionBlendTime = 0.25f;
	UPROPERTY(EditDefaultsOnly, Category="Push")
	float MinTravel = 0.0f;
	UPROPERTY(EditDefaultsOnly, Category="Push")
	float MaxTravel = 345.0f;
	UPROPERTY(EditDefaultsOnly, Category="Push")
	FTransform LeftHand;
	UPROPERTY(EditDefaultsOnly, Category="Push")
	FTransform RightHand;
	UPROPERTY(EditDefaultsOnly, Category="Push", meta=(ClampMin="0"))
	float GrabContactStartTime = 0.35f;
	UPROPERTY(EditDefaultsOnly, Category="Push", meta=(ClampMin="0.01"))
	float GrabBlendTime = 0.3f;
	UPROPERTY(EditDefaultsOnly, Category="Push", meta=(ClampMin="0.01"))
	float ReleaseBlendTime = 0.25f;

	float ClampTravel(float Travel) const { return FMath::Clamp(Travel, MinTravel, FMath::Max(MinTravel, MaxTravel)); }
	static float SharedMoveFraction(float PropFraction, float PawnFraction)
	{
		return FMath::Clamp(FMath::Min(PropFraction, PawnFraction), 0.0f, 1.0f);
	}
};
