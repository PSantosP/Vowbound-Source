#include "Animation/VBGASPBridgeLibrary.h"

#include "Animation/VBAnimInstance.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "VBGASPStateView.h"
#include "Chooser.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/VBCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PoseSearch/MotionMatchingAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include "Vowbound/Vowbound.h" // VB_LOG
#include "UObject/Stack.h"

namespace
{
	FProperty* Field(UScriptStruct* Struct, const TCHAR* Name)
	{
		const FString Prefix = FString(Name) + TEXT("_");
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
			if (It->GetName().StartsWith(Prefix)) return *It;
		return nullptr;
	}

	template<typename T>
	bool SetStruct(UScriptStruct* Struct, void* Output, const TCHAR* Name, const T& Value)
	{
		FStructProperty* Property = CastField<FStructProperty>(Field(Struct, Name));
		if (!Property || Property->Struct != TBaseStructure<T>::Get())
		{
			VB_LOG(Error, "GASP struct field %s: actual=%s expected=%s", Name,
				Property ? *Property->Struct->GetPathName() : TEXT("missing"), *TBaseStructure<T>::Get()->GetPathName());
			return false;
		}
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Output), &Value);
		return true;
	}

	bool SetBool(UScriptStruct* Struct, void* Output, const TCHAR* Name, bool Value)
	{
		FBoolProperty* Property = CastField<FBoolProperty>(Field(Struct, Name));
		if (!Property) { VB_LOG(Error, "GASP missing bool %s", Name); return false; }
		Property->SetPropertyValue_InContainer(Output, Value);
		return true;
	}

	bool SetNumber(UScriptStruct* Struct, void* Output, const TCHAR* Name, double Value)
	{
		FNumericProperty* Property = CastField<FNumericProperty>(Field(Struct, Name));
		if (!Property || !Property->IsFloatingPoint()) { VB_LOG(Error, "GASP missing number %s", Name); return false; }
		Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(Output), Value);
		return true;
	}

	// User-defined enum display order and NewEnumerator IDs differ. Resolve authored names,
	// which UUserDefinedEnum retains in cooked builds, rather than casting native ordinals.
	bool SetEnum(UScriptStruct* Struct, void* Output, const TCHAR* Name, const TCHAR* Value)
	{
		FByteProperty* Property = CastField<FByteProperty>(Field(Struct, Name));
		if (!Property || !Property->Enum) { VB_LOG(Error, "GASP missing enum %s", Name); return false; }
		for (int32 Index = 0; Index < Property->Enum->NumEnums(); ++Index)
		{
			if (Property->Enum->GetAuthoredNameStringByIndex(Index) == Value)
			{
				Property->SetPropertyValue_InContainer(Output, Property->Enum->GetValueByIndex(Index));
				return true;
			}
		}
		VB_LOG(Error, "GASP missing enum value %s.%s", Name, Value);
		return false;
	}
}

void UVBGASPBridgeLibrary::ReadCharacterProperties(ACharacter*, int32&)
{
	checkNoEntry(); // Wildcard calls must execute through the VM thunk.
}

DEFINE_FUNCTION(UVBGASPBridgeLibrary::execReadCharacterProperties)
{
	P_GET_OBJECT(ACharacter, Character);
	Stack.MostRecentProperty = nullptr;
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FStructProperty* Property = CastField<FStructProperty>(Stack.MostRecentProperty);
	void* Output = Stack.MostRecentPropertyAddress;
	P_FINISH;
	P_NATIVE_BEGIN;
	if (!Property || !Output || !FillCharacterProperties(Character, Property->Struct, Output))
		FFrame::KismetExecutionMessage(TEXT("Vowbound GASP bridge: incompatible character/property contract"), ELogVerbosity::Error);
	P_NATIVE_END;
}

bool UVBGASPBridgeLibrary::FillCharacterProperties(ACharacter* Character, UScriptStruct* Struct, void* Output)
{
	if (!Character || !Struct || !Output) return false;
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Movement) return false;
	const AVBCharacter* VB = Cast<AVBCharacter>(Character);
	const UVBAnimInstance* Animation = Character->GetMesh() ? Cast<UVBAnimInstance>(Character->GetMesh()->GetAnimInstance()) : nullptr;
	const bool bSprint = VB && VB->GetIsSprinting();
	const bool bWalk = VB && VB->GetGait() == EVBGait::Walk;
	const bool bStrafe = !Movement->bOrientRotationToMovement;
	const TCHAR* Gait = bSprint ? TEXT("Sprint") : bWalk ? TEXT("Walk") : TEXT("Run");
	const TCHAR* Mode = Movement->IsFalling() || Movement->IsSwimming() ? TEXT("InAir") : TEXT("OnGround");
	// Match the original CMC contract: traversal poses are supplied by the main instance.
	bool bValid = SetEnum(Struct, Output, TEXT("MovementMode"), Mode);
	bValid &= SetEnum(Struct, Output, TEXT("Stance"), Character->bIsCrouched ? TEXT("Crouch") : TEXT("Stand"));
	bValid &= SetEnum(Struct, Output, TEXT("Gait"), Gait);
	if (Struct->GetFName() == TEXT("S_CharacterPropertiesForCamera"))
	{
		bValid &= SetEnum(Struct, Output, TEXT("CameraStyle"), VB ? *VB->GetGASPCameraStyle().ToString() : TEXT("Medium"));
		bValid &= SetEnum(Struct, Output, TEXT("CameraMode"), VB && VB->WantsToAim() ? TEXT("Aim") : bStrafe ? TEXT("Strafe") : TEXT("FreeCam"));
		return bValid;
	}
	if (Struct->GetFName() != TEXT("S_CharacterPropertiesForAnimation")) return false;
	bValid &= SetEnum(Struct, Output, TEXT("RotationMode"), bStrafe ? TEXT("Strafe") : TEXT("OrientToMovement"));
	bValid &= SetEnum(Struct, Output, TEXT("MovementDirection"), TEXT("F")); // CMC source leaves direction to the ABP.
	bValid &= SetStruct(Struct, Output, TEXT("ActorTransform"), Character->GetActorTransform());
	bValid &= SetStruct(Struct, Output, TEXT("Velocity"), Animation && Animation->IsSuppressingAttackLocomotion()
		? FVector::ZeroVector : Movement->Velocity);
	bValid &= SetStruct(Struct, Output, TEXT("InputAcceleration"), Movement->GetCurrentAcceleration());
	bValid &= SetNumber(Struct, Output, TEXT("CurrentMaxAcceleration"), Movement->GetMaxAcceleration());
	bValid &= SetNumber(Struct, Output, TEXT("CurrentMaxDeceleration"), Movement->BrakingDecelerationWalking);
	bValid &= SetStruct(Struct, Output, TEXT("OrientationIntent"), Character->GetActorRotation());
	bValid &= SetStruct(Struct, Output, TEXT("AimingRotation"), Character->IsLocallyControlled() ? Character->GetControlRotation() : Character->GetBaseAimRotation());
	bValid &= SetBool(Struct, Output, TEXT("JustLanded"), Animation && Animation->JustLanded);
	bValid &= SetStruct(Struct, Output, TEXT("LandVelocity"), Animation ? Animation->LandVelocity : FVector::ZeroVector);
	bValid &= SetNumber(Struct, Output, TEXT("SteeringTime"), 0.0);
	bValid &= SetStruct(Struct, Output, TEXT("GroundNormal"), FVector(Movement->CurrentFloor.HitResult.ImpactNormal));
	bValid &= SetStruct(Struct, Output, TEXT("GroundLocation"), FVector(Movement->CurrentFloor.HitResult.ImpactPoint));
	bValid &= SetStruct(Struct, Output, TEXT("BasedMovementDelta"), FTransform::Identity); // Original CMC leaves this at identity; Mover supplies a delta.
	FStructProperty* Input = CastField<FStructProperty>(Field(Struct, TEXT("InputState")));
	if (!Input) return false;
	void* InputValue = Input->ContainerPtrToValuePtr<void>(Output);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToSprint"), bSprint);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToWalk"), bWalk);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToStrafe"), bStrafe);
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToAim"), VB && VB->WantsToAim());
	bValid &= SetBool(Input->Struct, InputValue, TEXT("WantsToCrouch"), Movement->bWantsToCrouch);
	return bValid;
}

void UVBGASPBridgeLibrary::ApplyWeaponDatabases(UAnimInstance* LinkedInstance, const FAnimNodeReference& Node)
{
	if (!LinkedInstance || !LinkedInstance->GetSkelMeshComponent()) return;
	const UVBAnimInstance* Main = Cast<UVBAnimInstance>(LinkedInstance->GetSkelMeshComponent()->GetAnimInstance());
	if (!Main || !Main->UsesWeaponLocomotion()) return;
	EAnimNodeReferenceConversionResult Result;
	const FMotionMatchingAnimNodeReference Matching = UMotionMatchingAnimNodeLibrary::ConvertToMotionMatchingNode(Node, Result);
	if (Result == EAnimNodeReferenceConversionResult::Succeeded)
		UMotionMatchingAnimNodeLibrary::SetDatabasesToSearch(Matching, Main->GetAllActivePoseSearchDBs(), Main->GetMMInterruptMode());
}

EOffsetRootBoneMode UVBGASPBridgeLibrary::ConstrainRootOffsetMode(UAnimInstance* LinkedInstance, EOffsetRootBoneMode Mode)
{
	const UVBAnimInstance* Main = LinkedInstance && LinkedInstance->GetSkelMeshComponent()
		? Cast<UVBAnimInstance>(LinkedInstance->GetSkelMeshComponent()->GetAnimInstance()) : nullptr;
	return Main && Main->ShouldReleaseGASPRootOffset() ? EOffsetRootBoneMode::Release : Mode;
}

void UVBGASPBridgeLibrary::CaptureCMCSelection(UAnimInstance* LinkedInstance)
{
	if (!LinkedInstance || !ensure(IsInGameThread())) return;
	const VBGASP::FStateView View(LinkedInstance);
	const auto* StateProperty = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
	// Original sample instances do not have the project contract.
	if (!StateProperty || StateProperty->Struct != FVBGASPCMCState::StaticStruct()) return;
	FVBGASPCMCState& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(LinkedInstance);
	const ACharacter* Character = Cast<ACharacter>(LinkedInstance->GetOwningActor());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	State.bHasWalkableFloor = Movement && Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor();
	State.GroundNormal = State.bHasWalkableFloor ? Movement->CurrentFloor.HitResult.ImpactNormal : FVector::UpVector;
	USkeletalMeshComponent* Mesh = LinkedInstance->GetSkelMeshComponent();
	UVBAnimInstance* Main = Mesh ? Cast<UVBAnimInstance>(Mesh->GetAnimInstance()) : nullptr;
	const bool bWeapon = Main && Main->UsesWeaponLocomotion();
	const bool bChangedRoute = State.bUsesWeaponLocomotion != bWeapon;
	State.bUsesWeaponLocomotion = bWeapon;
	State.CurrentInterruptMode = Main ? Main->GetMMInterruptMode() : EPoseSearchInterruptMode::DoNotInterrupt;
	State.SelectionContext = bWeapon ? static_cast<UAnimInstance*>(Main) : LinkedInstance;
	const auto* ChooserProperty = CastField<FObjectPropertyBase>(View.Find(bWeapon ? TEXT("CMCWeaponChooser") : TEXT("CMCUnarmedChooser")));
	UChooserTable* Chooser = ChooserProperty ? Cast<UChooserTable>(ChooserProperty->GetObjectPropertyValue_InContainer(LinkedInstance)) : nullptr;
	if (bWeapon)
		if (UChooserTable* GuardChooser = Main->GetRequestedGuardLocomotionChooser()) Chooser = GuardChooser;
	if (State.ActiveChooser != Chooser)
	{
		// 선택기 교체 전의 성공을 새 자세의 준비 완료로 오인하지 않는다.
		// 실패한 가드 선택기를 빠져나갈 때도 원래 이동 선택기를 새로 평가한다.
		State.bHasSelection = false;
		State.bWaitingForPhase = false;
		State.bSelectionContractFailed = false;
		State.bTransitionToLoopRequested = false;
	}
	State.bNeedsReselect |= bChangedRoute || State.ActiveChooser != Chooser;
	State.ActiveChooser = Chooser;
	if (!bWeapon)
	{
		State.AllowedDatabases.Reset();
		State.WeaponSelectionKey = 0;
		return;
	}

	const VBGASP::FStateView MainView(Main);
	FVBGASPCMCWeaponQuery Query;
	// All reflected access and main-instance helpers happen on the game thread.
	// Worker selection receives plain values and recomputes only linked CMC state.
	auto CopyEnum = [&]<typename T>(FName Name, T& Value)
	{
		const auto* Property = CastField<FEnumProperty>(MainView.Find(Name));
		if (!Property || Property->GetEnum() != StaticEnum<T>()) return false;
		Value = static_cast<T>(Property->GetUnderlyingProperty()->GetSignedIntPropertyValue(
			Property->ContainerPtrToValuePtr<void>(Main)));
		return true;
	};
	const bool bSnapshotValid = CopyEnum(TEXT("MMWeaponType"), Query.MMWeaponType) &&
		CopyEnum(TEXT("LocomotionState"), Query.LocomotionState) && CopyEnum(TEXT("Gait"), Query.Gait);
	if (!bSnapshotValid)
	{
		State.bSelectionContractFailed = true;
		ensureMsgf(false, TEXT("Weapon CMC snapshot enum contract changed"));
		return;
	}
	Query.MMDatabaseLOD = static_cast<float>(MainView.Number(TEXT("MMDatabaseLOD")));
	Query.MMSprintGateSpeed = static_cast<float>(MainView.Number(TEXT("MMSprintGateSpeed")));
	// Linked GT update precedes Main NativeUpdate: cached Main mode can still be
	// InAir on the first grounded frame. Match the current character snapshot.
	Query.TimeToLand = Movement && !Movement->IsFalling() ? 0.0f : Main->TimeToLand();
	Query.TrajectoryTimeToLand = static_cast<float>(MainView.Child(TEXT("TrajectoryCollision")).Number(TEXT("TimeToLand")));
	Query.JustTraversed = Main->JustTraversed();
	Query.JustLandedLight = Main->JustLandedLight();
	Query.JustLandedHeavy = Main->JustLandedHeavy();
	Query.bTurnProfileOwnsTIP = Main->DoesTurnProfileOwnTIP();
	const int32 Pool = static_cast<int32>(Query.MMWeaponType);
	const bool bPoolChanged = Pool != State.CapturedWeaponPool;
	const bool bInterrupt = State.CurrentInterruptMode != EPoseSearchInterruptMode::DoNotInterrupt;
	// Preserve the main weapon-switch convergence delay; CMC state transitions,
	// not MM's different movement predicate, own ordinary start/stop interrupts.
	if (bPoolChanged && State.bHasSelection && !bChangedRoute && !bInterrupt)
		Query.MMWeaponType = State.WeaponQuery.MMWeaponType;
	else
	{
		State.bNeedsReselect |= bPoolChanged || Query.LocomotionState != State.WeaponQuery.LocomotionState;
		State.CapturedWeaponPool = Pool;
	}
	State.CurrentInterruptMode = bPoolChanged && bInterrupt
		? EPoseSearchInterruptMode::InterruptOnDatabaseChange : EPoseSearchInterruptMode::DoNotInterrupt;
	State.WeaponQuery = Query;
}
