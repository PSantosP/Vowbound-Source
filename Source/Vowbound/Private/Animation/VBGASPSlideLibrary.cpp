#include "Animation/VBGASPSlideLibrary.h"

#include "Animation/AnimInstance.h"
#include "Animation/VBGASPLocomotionUpdateLibrary.h"
#include "Animation/VBAnimInstance.h"
#include "Character/VBCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "KismetAnimationLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "VBGASPMutableStateView.h"

namespace
{
	bool SetAuthoredEnum(const VBGASP::FMutableStateView& State, FName Name, const TCHAR* Value)
	{
		const FByteProperty* Property = CastField<FByteProperty>(State.Find(Name));
		if (!Property || !Property->Enum) return false;
		for (int32 Index = 0; Index < Property->Enum->NumEnums(); ++Index)
			if (Property->Enum->GetAuthoredNameStringByIndex(Index) == Value)
			{
				Property->SetPropertyValue_InContainer(State.Data, static_cast<uint8>(Property->Enum->GetValueByIndex(Index)));
				return true;
			}
		return false;
	}
}

void UVBGASPSlideLibrary::InitializeMoverPredictor(UAnimInstance* Instance)
{
	if (!Instance || !IsInGameThread()) return;
	const VBGASP::FMutableStateView State(Instance);
	const AActor* Owner = Instance->GetOwningActor();
	State.SetBool(TEXT("HasOwningActor"), Owner != nullptr);
	const ACharacter* Character = Cast<ACharacter>(Owner);
	State.SetBool(TEXT("HasMover"), Character && Character->GetCharacterMovement());
	State.SetNumber(TEXT("NetworkRole"), Owner ? Owner->GetLocalRole() : ROLE_None);
}

void UVBGASPSlideLibrary::Update_PropertiesFromCharacter(UAnimInstance* Instance)
{
	UVBGASPLocomotionUpdateLibrary::Update_PropertiesFromCharacter(Instance);
	if (!Instance || !IsInGameThread()) return;
	const ACharacter* Character = Cast<ACharacter>(Instance->GetOwningActor());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement) return;
	const VBGASP::FMutableStateView Properties(VBGASP::FStateView(Instance).Child(TEXT("CharacterProperties")));
	const AVBCharacter* Player = Cast<AVBCharacter>(Character);
	if (Player && Player->GetIsSliding() && Movement->IsMovingOnGround())
		ensure(SetAuthoredEnum(Properties, TEXT("MovementMode"), TEXT("Sliding")));
	const float Angle = UKismetAnimationLibrary::CalculateDirection(Movement->Velocity, Character->GetActorRotation());
	const TCHAR* Direction = TEXT("F");
	if (!Movement->bOrientRotationToMovement)
	{
		if (Angle < -120.f || Angle > 120.f) Direction = TEXT("B");
		else if (Angle < -60.f) Direction = TEXT("LL");
		else if (Angle > 60.f) Direction = TEXT("RL");
	}
	ensure(SetAuthoredEnum(Properties, TEXT("MovementDirection"), Direction));
}

void UVBGASPSlideLibrary::GenerateSlideTrajectory(UAnimInstance* Instance, float InDeltaTime,
	const FTransformTrajectory& InOutTrajectory, float InOutDesiredControllerYawLastUpdate,
	FTransformTrajectory& OutTrajectory, float InHistorySamplingInterval,
	int32 InTrajectoryHistoryCount, float InPredictionSamplingInterval, int32 InTrajectoryPredictionCount)
{
	OutTrajectory = InOutTrajectory;
	if (!Instance || !Instance->GetSkelMeshComponent()) return;
	const UAnimInstance* Main = Instance->GetSkelMeshComponent()->GetAnimInstance();
	if (!Main) return;
	const VBGASP::FStateView State(const_cast<UAnimInstance*>(Main));
	const FPoseSearchTrajectoryData Data = State.NativeStruct<FPoseSearchTrajectoryData>(
		State.Number(TEXT("Speed2D")) > 0.0 ? TEXT("TrajectoryGenerationData_Moving") : TEXT("TrajectoryGenerationData_Idle"));
	FTransformTrajectory History = InOutTrajectory;
	UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectory(Main, Data, InDeltaTime,
		History, InOutDesiredControllerYawLastUpdate, OutTrajectory, InHistorySamplingInterval,
		InTrajectoryHistoryCount, InPredictionSamplingInterval, InTrajectoryPredictionCount);
	VBGASP::FMutableStateView(Instance).SetNumber(TEXT("PreviousDesiredControllerYaw"), InOutDesiredControllerYawLastUpdate);
}

bool UVBGASPSlideLibrary::HasEnteredSlide(const UAnimInstance* Instance)
{
	if (!Instance) return false;
	const VBGASP::FStateView State(const_cast<UAnimInstance*>(Instance));
	return State.Is(TEXT("MovementMode"), TEXT("Sliding"));
}

bool UVBGASPSlideLibrary::HasCompletedSlideExit(const UAnimInstance* Instance)
{
	if (!Instance) return false;
	const FNameProperty* Property = FindFProperty<FNameProperty>(Instance->GetClass(), TEXT("StateName"));
	const FName Name = Property ? Property->GetPropertyValue_InContainer(Instance) : NAME_None;
	return Name == TEXT("Idle Loop") || Name == TEXT("Locomotion Loop") || Name == TEXT("In Air Loop");
}
