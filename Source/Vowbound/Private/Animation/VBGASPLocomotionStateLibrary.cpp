#include "Animation/VBGASPLocomotionStateLibrary.h"

#include "Animation/VBGASPLocomotionLibrary.h"
#include "VBGASPStateView.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNodeReference.h"
#include "Animation/AnimSubsystem_Tag.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendProfile.h"
#include "BlendStack/BlendStackAnimNodeLibrary.h"
#include "BlendStack/AnimNode_BlendStack.h"
#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "ChooserPropertyAccess.h"
#include "ChooserTypes.h"
#include "IObjectChooser.h"
#include "PoseSearch/PoseSearchHistoryCollectorAnimNodeLibrary.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "StructUtils/InstancedStruct.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBGASPState, Log, All);

using VBGASP::FStateView;

namespace
{
	template<typename T>
	T* NativeProperty(const FStateView& View, FName Name)
	{
		const FStructProperty* Property = CastField<FStructProperty>(View.Find(Name));
		return Property && Property->Struct == T::StaticStruct()
			? Property->ContainerPtrToValuePtr<T>(View.Data) : nullptr;
	}

	bool WriteNames(const FStateView& View, FName Name, const TArray<FName>& Names)
	{
		const FArrayProperty* Property = CastField<FArrayProperty>(View.Find(Name));
		if (!Property || !CastField<FNameProperty>(Property->Inner)) return false;
		*Property->ContainerPtrToValuePtr<TArray<FName>>(View.Data) = Names;
		return true;
	}

	bool WriteBool(const FStateView& State, FName Name, bool Value)
	{
		if (const FBoolProperty* Property = CastField<FBoolProperty>(State.Find(Name)))
		{
			Property->SetPropertyValue_InContainer(State.Data, Value);
			return true;
		}
		return false;
	}

	bool WriteNumber(const FStateView& State, FName Name, double Value)
	{
		if (const FNumericProperty* Property = CastField<FNumericProperty>(State.Find(Name)))
		{
			void* Data = Property->ContainerPtrToValuePtr<void>(State.Data);
			if (Property->IsFloatingPoint()) Property->SetFloatingPointPropertyValue(Data, Value);
			else Property->SetIntPropertyValue(Data, static_cast<int64>(Value));
			return true;
		}
		return false;
	}

	bool WriteObject(const FStateView& State, FName Name, UObject* Value)
	{
		if (const FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(State.Find(Name));
			Property && (!Value || Value->IsA(Property->PropertyClass)))
		{
			Property->SetObjectPropertyValue_InContainer(State.Data, Value);
			return true;
		}
		return false;
	}

	bool CopyField(const FStateView& Destination, FName DestinationName, const FStateView& Source, FName SourceName)
	{
		const FProperty* To = Destination.Find(DestinationName);
		const FProperty* From = Source.Find(SourceName);
		if (!To || !From || !To->SameType(From)) return false;
		To->CopyCompleteValue(To->ContainerPtrToValuePtr<void>(Destination.Data), From->ContainerPtrToValuePtr<void>(Source.Data));
		return true;
	}

	FName ReadName(const FStateView& State, FName Name)
	{
		const FNameProperty* Property = CastField<FNameProperty>(State.Find(Name));
		return Property ? Property->GetPropertyValue_InContainer(State.Data) : NAME_None;
	}

	bool StoreValidAnimations(const FStateView& State, const TArray<UAnimationAsset*>& Animations)
	{
		const FArrayProperty* Property = CastField<FArrayProperty>(State.Find(TEXT("ValidAnims")));
		const FObjectPropertyBase* Inner = Property ? CastField<FObjectPropertyBase>(Property->Inner) : nullptr;
		if (!Property || !Inner) return false;
		FScriptArrayHelper Array(Property, Property->ContainerPtrToValuePtr<void>(State.Data));
		Array.Resize(Animations.Num());
		for (int32 Index = 0; Index < Animations.Num(); ++Index) Inner->SetObjectPropertyValue(Array.GetRawPtr(Index), Animations[Index]);
		return true;
	}

	FAnimNodeReference FindTaggedNode(UAnimInstance* Instance, FName Tag)
	{
		const IAnimClassInterface* Class = Instance ? IAnimClassInterface::GetFromClass(Instance->GetClass()) : nullptr;
		const FAnimSubsystem_Tag* Tags = Class ? Class->FindSubsystem<FAnimSubsystem_Tag>() : nullptr;
		// FindNodeIndexByTag already returns the reversed property index used by FAnimNodeReference.
		return Tags ? FAnimNodeReference(Instance, Tags->FindNodeIndexByTag(Tag)) : FAnimNodeReference();
	}

	bool FindBlendStack(UAnimInstance* Instance, FBlendStackAnimNodeReference& Reference)
	{
		EAnimNodeReferenceConversionResult Result;
		Reference = UBlendStackAnimNodeLibrary::ConvertToBlendStackNode(FindTaggedNode(Instance, TEXT("State Machine Blend Stack")), Result);
		return Result == EAnimNodeReferenceConversionResult::Succeeded;
	}

	FName CMCStateName(const FStateView& View)
	{
		const auto* Property = CastField<FByteProperty>(View.Find(TEXT("StateMachineState")));
		return Property && Property->Enum ? FName(Property->Enum->GetAuthoredNameStringByValue(
			Property->GetPropertyValue_InContainer(View.Data))) : NAME_None;
	}

	bool HasLegacyProjection(const FStateView& State)
	{
		const FStateView Blend = State.Child(TEXT("BlendStackInputs"));
		const FProperty* Current = State.Find(TEXT("BlendStackInputs"));
		const FProperty* Previous = State.Find(TEXT("Previous_BlendStackInputs"));
		const FArrayProperty* Tags = CastField<FArrayProperty>(Blend.Find(TEXT("Tags")));
		return Current && Previous && Current->SameType(Previous)
			&& CastField<FObjectPropertyBase>(Blend.Find(TEXT("Anim")))
			&& CastField<FBoolProperty>(Blend.Find(TEXT("Loop")))
			&& CastField<FNumericProperty>(Blend.Find(TEXT("StartTime")))
			&& CastField<FNumericProperty>(Blend.Find(TEXT("BlendTime")))
			&& CastField<FObjectPropertyBase>(Blend.Find(TEXT("BlendProfile")))
			&& Tags && CastField<FNameProperty>(Tags->Inner)
			&& CastField<FBoolProperty>(State.Find(TEXT("NoValidAnim")));
	}

	bool ApplyCMCSelection(UAnimInstance* Instance, uint8 MachineState, bool bForceBlend,
		UAnimationAsset* Animation, const FVBGASPCMCChooserOutput& Output)
	{
		const FStateView View(Instance);
		FVBGASPCMCState* State = NativeProperty<FVBGASPCMCState>(View, TEXT("CMCState"));
		FVBGASPCMCBlendInputs* Inputs = NativeProperty<FVBGASPCMCBlendInputs>(View, TEXT("CMCBlendInputs"));
		if (!State || !Inputs || !HasLegacyProjection(View)
			|| !UVBGASPLocomotionStateLibrary::IsValidCMCSelection(Animation, Output)) return false;

		FVBGASPCMCBlendInputs Next;
		Next.Anim = Animation;
		UPoseSearchLibrary::IsAnimationAssetLooping(Animation, Next.bLoop);
		Next.StartTime = Output.StartTime;
		Next.bMirror = Output.bMirror;
		Next.BlendTime = Output.BlendTime;
		Next.BlendProfile = const_cast<UBlendProfile*>(Instance->GetBlendProfileByName(Output.BlendProfileName));
		Next.BlendCurve = Output.BlendCurve;
		Next.Tags = Output.Tags;
		if (!Output.BlendProfileName.IsNone() && !Next.BlendProfile) return false;
		// The same slope asset keeps its continuously updated parameters across a state reselection.
		if (Next.Tags.Contains(TEXT("BS_Slope"))) Next.BlendParameters = Inputs->BlendParameters;

		// Validate the complete result before publishing either the native command or legacy view.
		const bool bSelectionChanged = Inputs->Anim != Next.Anim || Inputs->bMirror != Next.bMirror ||
			!State->bHasSelection || State->SelectedState != MachineState || State->SelectedWeaponPool != State->CapturedWeaponPool;
		CopyField(View, TEXT("Previous_BlendStackInputs"), View, TEXT("BlendStackInputs"));
		const FStateView Blend = View.Child(TEXT("BlendStackInputs"));
		WriteObject(Blend, TEXT("Anim"), Next.Anim);
		WriteBool(Blend, TEXT("Loop"), Next.bLoop);
		WriteNumber(Blend, TEXT("StartTime"), Next.StartTime);
		WriteNumber(Blend, TEXT("BlendTime"), Next.BlendTime);
		WriteObject(Blend, TEXT("BlendProfile"), Next.BlendProfile);
		WriteNames(Blend, TEXT("Tags"), Next.Tags);
		WriteBool(View, TEXT("NoValidAnim"), false);
		// Periodic armed searches must not consume transition notifies before the state graph sees them.
		if (bSelectionChanged || bForceBlend)
		{
			WriteBool(View, TEXT("NotifyTransition_Re-Transition"), false);
			WriteBool(View, TEXT("NotifyTransition_ToLoop"), false);
		}
		WriteNumber(View, TEXT("Search Cost"), Output.SearchCost);
		WriteObject(View, TEXT("CurrentSelectedAnim"), Animation);
		WriteObject(View, TEXT("CurrentSelectedDatabase"), Output.SourceDatabase);
		TArray<FName> DatabaseTags;
		if (Output.SourceDatabase) UPoseSearchLibrary::GetDatabaseTags(Output.SourceDatabase, DatabaseTags);
		if (State->bUsesWeaponLocomotion)
			DatabaseTags = UVBGASPLocomotionStateLibrary::ProjectCMCPlaybackTags(Output.Tags, DatabaseTags);
		WriteNames(View, TEXT("CurrentDatabaseTags"), DatabaseTags);
		*Inputs = MoveTemp(Next);
		State->LastSelection = Output;
		State->SelectedState = MachineState;
		State->SelectedWeaponKey = State->WeaponSelectionKey;
		State->SelectedWeaponPool = State->CapturedWeaponPool;
		State->bSelectedWeaponLocomotion = State->bUsesWeaponLocomotion;
		State->bWaitingForPhase = false;
		State->bHasSelection = true;
		State->bSelectionContractFailed = false;
		++State->SelectionSerial;
		if (bForceBlend || Output.bForceBlend) State->PendingForceSerial = State->SelectionSerial;
		// An authored transition can fall back to a loop when the active pool has
		// no start/stop. Such a loop cannot ever fire a non-loop completion notify.
		if (State->bUsesWeaponLocomotion && Inputs->bLoop &&
			UVBGASPLocomotionStateLibrary::GetCMCPhase(CMCStateName(View)) == EVBGASPCMCPhase::Transition)
		{
			State->bTransitionToLoopRequested = true;
			WriteBool(View, TEXT("NotifyTransition_ToLoop"), true);
			// 피벗 중이었다면 이 풀·자세에는 피벗 클립이 없다 - 곧장 Loop 로 돌아와도 피벗 원판정이 참이라 다시 들어가 매 프레임 왕복한다(대검 앉기, 10-04 Codex 재현).
			//   래치를 켜서 원판정이 한 번 거짓이 될 때까지 피벗 진입을 막는다(.sw/crouch-mm-completion/pivot-guard-design.md).
			const bool bWasLatched = State->bPivotFallbackLatched;
			State->bPivotFallbackLatched = UVBGASPLocomotionStateLibrary::StepPivotFallbackLatch(
				bWasLatched, true, UVBGASPLocomotionLibrary::IsPivotingRaw(Instance), false);
			if (State->bPivotFallbackLatched && !bWasLatched)
				State->bPivotFallbackCrouched = View.Is(TEXT("Stance"), TEXT("Crouch"));
		}
		if (State->bUsesWeaponLocomotion && bSelectionChanged)
			UE_LOG(LogVBGASPState, Log, TEXT("CMC armed selection: state=%s phase=%d pool=%d asset=%s loop=%d source=%s candidates=%d"),
				*CMCStateName(View).ToString(), static_cast<int32>(Output.Phase), State->CapturedWeaponPool,
				*GetNameSafe(Animation), Inputs->bLoop, *GetNameSafe(Output.SourceDatabase), State->AllowedDatabases.Num());
		return true;
	}

	void SelectNativeCMC(UAnimInstance* Instance, uint8 MachineState, bool bForceBlend, UChooserTable* Chooser)
	{
		const FStateView View(Instance);
		FVBGASPCMCState* State = NativeProperty<FVBGASPCMCState>(View, TEXT("CMCState"));
		if (!ensureMsgf(State, TEXT("Native CMC chooser requires CMCState on %s"), *GetNameSafe(Instance))
			|| State->bSelectionContractFailed) return;
		State->bNeedsReselect = false;
		State->SecondsSinceSearch = 0.0f;
		if (!State->bHasSelection || State->SelectedState != MachineState || bForceBlend ||
			State->bSelectedWeaponLocomotion != State->bUsesWeaponLocomotion || State->SelectedWeaponPool != State->CapturedWeaponPool)
			State->bTransitionToLoopRequested = false;
		// 피벗 대체 래치는 «이 풀·이 경로에 피벗 콘텐츠가 없다» 는 사실이라, 경로(무장/맨손)나 무기 풀이 바뀌면 더는 참이 아니다.
		if (UVBGASPLocomotionStateLibrary::HasPivotFallbackOwnerChanged(*State))
			State->bPivotFallbackLatched = false;
		// State is an input filter. Playback inputs are not mutated until the result is accepted.
		if (!WriteNumber(View, TEXT("StateMachineState"), MachineState))
		{
			State->bSelectionContractFailed = true;
			UE_LOG(LogVBGASPState, Error, TEXT("Missing CMC StateMachineState on %s"), *GetNameSafe(Instance));
			return;
		}
		UAnimationAsset* Animation = nullptr;
		FVBGASPCMCChooserOutput Output;
		UAnimInstance* Context = State->SelectionContext ? State->SelectionContext.Get() : Instance;
		const bool bEvaluated = UVBGASPLocomotionStateLibrary::EvaluateCMCAnimation(Instance, Context, Chooser, Animation, Output);
		State->bHistoryWasReady = UVBGASPLocomotionStateLibrary::IsCMCPoseHistoryReady(Instance);
		const bool bSamePlaybackOwner = State->bHasSelection &&
			State->bSelectedWeaponLocomotion == State->bUsesWeaponLocomotion &&
			(!State->bUsesWeaponLocomotion || State->SelectedWeaponPool == State->CapturedWeaponPool);
		// A stop-only pool can remain active briefly after the CMC graph enters
		// Idle Loop. No looping row is valid yet; hold the same weapon's last pose
		// and retry when its pool changes, rather than permanently disabling CMC.
		if (bEvaluated && !Animation &&
			State->bUsesWeaponLocomotion && bSamePlaybackOwner)
		{
			if (!State->bWaitingForPhase)
				UE_LOG(LogVBGASPState, Log, TEXT("CMC armed awaiting phase: state=%s phase=%d pool=%d candidates=%d phaseCandidates=%d held=%s"),
					*CMCStateName(View).ToString(), static_cast<int32>(Output.Phase), State->CapturedWeaponPool,
					State->AllowedDatabases.Num(), Output.bHasPhaseCandidates, *GetNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Instance).Anim));
			State->bWaitingForPhase = true;
			WriteBool(View, TEXT("NoValidAnim"), true);
			return;
		}
		if (!bEvaluated || !ApplyCMCSelection(Instance, MachineState, bForceBlend, Animation, Output))
		{
			WriteBool(View, TEXT("NoValidAnim"), true);
			StoreValidAnimations(View, {});
			// Null results with an existing weapon pose were handled as retriable
			// above. Invalid contexts/commands, or a stale other-weapon pose, cannot
			// satisfy the playback contract and must fail closed.
			if (State->bUsesWeaponLocomotion || !bEvaluated || !bSamePlaybackOwner)
			{
				State->bSelectionContractFailed = true;
				UE_LOG(LogVBGASPState, Error, TEXT("CMC selection contract failed (%s, chooser=%s, state=%s, pool=%d, evaluated=%d, history=%d, candidateChooser=%d, phaseCandidates=%d, phaseValid=%d, allowed=%d, asset=%s, source=%s, poseMatched=%d, fallback=%d, cost=%g, start=%g, blend=%g, profile=%s); restore MM until reinitialization"),
					*GetNameSafe(Instance), *GetNameSafe(Chooser), *CMCStateName(View).ToString(), State->CapturedWeaponPool,
					bEvaluated, Output.bHistoryReady, Output.bCandidateChooserEvaluated, Output.bHasPhaseCandidates,
					Output.bPhaseDataValid, Output.AllowedDatabases.Num(), *GetNameSafe(Animation), *GetNameSafe(Output.SourceDatabase),
					Output.bPoseMatched, Output.bFallback, Output.SearchCost, Output.StartTime, Output.BlendTime, *Output.BlendProfileName.ToString());
			}
			return;
		}
		StoreValidAnimations(View, { Animation });
		UVBGASPLocomotionStateLibrary::ConsumeCMCForceBlend(Instance);
	}

	void EnterState(UAnimInstance* Instance, UChooserTable* Chooser, const TCHAR* StateName, bool bForceBlend, bool bCaptureTargetRotation = false)
	{
		const FStateView State(Instance);
		if (bCaptureTargetRotation) CopyField(State, TEXT("TargetRotationOnTransitionStart"), State, TEXT("TargetRotation"));
		const FByteProperty* Property = CastField<FByteProperty>(State.Find(TEXT("StateMachineState")));
		if (Property && Property->Enum)
		{
			for (int32 Index = 0; Index < Property->Enum->NumEnums(); ++Index)
			{
				if (Property->Enum->GetAuthoredNameStringByIndex(Index) == StateName)
				{
					UVBGASPLocomotionStateLibrary::SetBlendStackAnimFromChooser(Instance,
						static_cast<uint8>(Property->Enum->GetValueByIndex(Index)), bForceBlend, Chooser);
					return;
				}
			}
		}
		WriteBool(State, TEXT("NoValidAnim"), true);
		UE_LOG(LogVBGASPState, Error, TEXT("Missing GASP StateMachineState value '%s' on %s"), StateName, *GetNameSafe(Instance));
	}

	bool EvaluateAnimations(UAnimInstance* Instance, UChooserTable* Chooser, TArray<UAnimationAsset*>& Results, FInstancedStruct& Output)
	{
		if (!Chooser) return false;
		const TConstArrayView<FInstancedStruct> Signature = Chooser->GetContextData();
		if (Signature.Num() != 2) return false;
		const FContextObjectTypeClass* ObjectContext = Signature[0].GetPtr<FContextObjectTypeClass>();
		const FContextObjectTypeStruct* StructContext = Signature[1].GetPtr<FContextObjectTypeStruct>();
		if (!ObjectContext || !ObjectContext->Class || !Instance->IsA(ObjectContext->Class) || !StructContext ||
			!StructContext->Struct || StructContext->Struct->GetFName() != TEXT("S_ChooserOutputs")) return false;

		// The output retains the original UDS identity required by the authored Chooser bindings.
		Output.InitializeAs(StructContext->Struct);
		FChooserEvaluationContext Context(Instance);
		Context.AddStructViewParam(FStructView(Output.GetScriptStruct(), Output.GetMutableMemory()));
		// Match EvaluateChooser2's native expansion, including AnimInstance proxy synchronization.
		const FInstancedStruct Selection = UChooserFunctionLibrary::MakeEvaluateChooser(Chooser);
		for (UObject* Result : UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(Context, Selection, UAnimationAsset::StaticClass()))
		{
			Results.Add(Cast<UAnimationAsset>(Result));
		}
		return true;
	}
}

bool UVBGASPLocomotionStateLibrary::IsCMCPoseHistoryReady(UAnimInstance* Instance)
{
	const FPoseHistoryReference History = Get_PoseHistoryReference(Instance);
	// A valid collector reference can still have no samples on its first update.
	return History.PoseHistory.IsValid() && History.PoseHistory->GetNumEntries() >= 2;
}

bool UVBGASPLocomotionStateLibrary::IsValidCMCSelection(const UAnimationAsset* Animation,
	const FVBGASPCMCChooserOutput& Output)
{
	if (!Animation || !FMath::IsFinite(Output.StartTime) || Output.StartTime < 0.0f
		|| !FMath::IsFinite(Output.BlendTime) || Output.BlendTime < 0.0f
		|| !FMath::IsFinite(Output.MaxAcceptedCost)) return false;
	if (!Output.bHistoryReady && !Output.bFallback) return false;
	if (!Output.bPoseMatched) return true;
	return Output.bHistoryReady && !Output.bFallback
		&& FMath::IsFinite(Output.SearchCost) && Output.SearchCost < MAX_flt
		&& (Output.MaxAcceptedCost <= 0.0f || Output.SearchCost <= Output.MaxAcceptedCost);
}

bool UVBGASPLocomotionStateLibrary::EvaluateCMCAnimation(UAnimInstance* LinkedInstance,
	UAnimInstance* ContextInstance, UChooserTable* Chooser, UAnimationAsset*& Animation,
	FVBGASPCMCChooserOutput& Output)
{
	Animation = nullptr;
	Output = FVBGASPCMCChooserOutput();
	if (!LinkedInstance || !ContextInstance || !Chooser) return false;
	const TConstArrayView<FInstancedStruct> Signature = Chooser->GetContextData();
	if (Signature.Num() != 3) return false;
	const FContextObjectTypeClass* ObjectType = Signature[0].GetPtr<FContextObjectTypeClass>();
	const FContextObjectTypeStruct* OutputType = Signature[1].GetPtr<FContextObjectTypeStruct>();
	const FContextObjectTypeStruct* HistoryType = Signature[2].GetPtr<FContextObjectTypeStruct>();
	if (!ObjectType || !ObjectType->Class || !ContextInstance->IsA(ObjectType->Class)
		|| !OutputType || OutputType->Struct != FVBGASPCMCChooserOutput::StaticStruct()
		|| OutputType->Direction != EContextObjectDirection::ReadWrite
		|| !HistoryType || HistoryType->Struct != FPoseHistoryReference::StaticStruct()) return false;

	FPoseHistoryReference History = Get_PoseHistoryReference(LinkedInstance);
	const bool bHistoryReady = History.PoseHistory.IsValid() && History.PoseHistory->GetNumEntries() >= 2;
	Output.bHistoryReady = bHistoryReady;
	FVBGASPCMCState* State = NativeProperty<FVBGASPCMCState>(FStateView(LinkedInstance), TEXT("CMCState"));
	FVBGASPCMCWeaponQuery WeaponQuery;
	if (State)
	{
		Output.AllowedDatabases = State->AllowedDatabases;
		Output.InterruptMode = State->CurrentInterruptMode;
		if (State->bUsesWeaponLocomotion)
		{
			Output.Phase = State->bTransitionToLoopRequested ? EVBGASPCMCPhase::Loop :
				GetCMCSelectionPhase(CMCStateName(FStateView(LinkedInstance)), FStateView(LinkedInstance).Struct<FVector>(TEXT("Velocity")).Z);
			Output.bRequireCandidateChooser = true;
			WeaponQuery = State->WeaponQuery;
			const FStateView LinkedView(LinkedInstance);
			WeaponQuery.MovementMode = LinkedView.Is(TEXT("MovementMode"), TEXT("InAir")) ? EVBMovementMode::InAir : EVBMovementMode::Grounded;
			WeaponQuery.Stance = LinkedView.Is(TEXT("Stance"), TEXT("Crouch")) ? EVBStance::Crouch : EVBStance::Stand;
			WeaponQuery.MMState = UVBGASPLocomotionLibrary::IsMoving(LinkedInstance) ? EVBMMState::Moving : EVBMMState::Idle;
			WeaponQuery.Speed2D = static_cast<float>(LinkedView.Number(TEXT("Speed2D")));
			WeaponQuery.IsStarting = UVBGASPLocomotionLibrary::IsStarting(LinkedInstance);
			WeaponQuery.IsPivoting = UVBGASPLocomotionLibrary::IsPivoting(LinkedInstance);
			WeaponQuery.ShouldSpinTransition = UVBGASPLocomotionLibrary::ShouldSpinTransition(LinkedInstance);
			WeaponQuery.ShouldTurnInPlace = !WeaponQuery.bTurnProfileOwnsTIP && UVBGASPLocomotionLibrary::ShouldTurnInPlace(LinkedInstance);
			ApplyCMCWeaponPhase(WeaponQuery, CMCStateName(LinkedView), Output.Phase);
		}
	}
	FChooserEvaluationContext Context(ContextInstance);
	Context.AddStructParam(Output);
	Context.AddStructParam(History);
	if (State && State->bUsesWeaponLocomotion) Context.AddStructParam(WeaponQuery);
	// Optional runtime context consumed by Pose Match (not part of authored property bindings).
	// Supplying the actual running sample lets periodic armed searches retain continuing-pose cost.
	FChooserPlayerSettings PlayerSettings;
	FBlendStackAnimNodeReference StackReference;
	if (FindBlendStack(LinkedInstance, StackReference))
	{
		const FAnimNode_BlendStack* Stack = StackReference.GetAnimNodePtr<FAnimNode_BlendStack>();
		if (Stack)
		{
			PlayerSettings.PlayingAsset = UBlendStackAnimNodeLibrary::GetCurrentAsset(StackReference);
			PlayerSettings.PlayingAssetAccumulatedTime = UBlendStackAnimNodeLibrary::GetCurrentAssetTime(StackReference);
			PlayerSettings.bIsPlayingAssetMirrored = Stack->GetMirror();
			PlayerSettings.PlayingAssetBlendParameters = Stack->GetBlendParameters();
		}
	}
	Context.AddStructParam(PlayerSettings);
	const FInstancedStruct Selection = UChooserFunctionLibrary::MakeEvaluateChooser(Chooser);
	Animation = Cast<UAnimationAsset>(UChooserFunctionLibrary::EvaluateObjectChooserBase(
		Context, Selection, UAnimationAsset::StaticClass()));
	// These are inputs, not author-controlled metadata. Output columns cannot forge readiness.
	Output.bHistoryReady = bHistoryReady;
	if (State)
	{
		Output.InterruptMode = State->CurrentInterruptMode;
		if (State->bUsesWeaponLocomotion)
		{
			State->AllowedDatabases = Output.AllowedDatabases;
			uint32 Key = 0;
			for (const auto& Database : Output.AllowedDatabases) Key = HashCombineFast(Key, GetTypeHash(Database.Get()));
			State->WeaponSelectionKey = static_cast<int32>(Key);
			if (!Output.bCandidateChooserEvaluated) Output.bPhaseDataValid = false;
			if (Animation && (!Output.SourceDatabase || !Output.AllowedDatabases.Contains(Output.SourceDatabase)))
				Output.bPhaseDataValid = false;
		}
	}
	return Output.bPhaseDataValid;
}

EVBGASPCMCPhase UVBGASPLocomotionStateLibrary::GetCMCPhase(FName AuthoredState)
{
	if (AuthoredState == TEXT("Idle Loop") || AuthoredState == TEXT("Locomotion Loop"))
		return EVBGASPCMCPhase::Loop;
	if (AuthoredState == TEXT("Transition to Idle Loop") || AuthoredState == TEXT("Transition to Locomotion Loop"))
		return EVBGASPCMCPhase::Transition;
	return EVBGASPCMCPhase::Any;
}

EVBGASPCMCPhase UVBGASPLocomotionStateLibrary::GetCMCSelectionPhase(FName AuthoredState, float VerticalSpeed)
{
	const bool bIsInAirSelection = AuthoredState == TEXT("In Air Loop") || AuthoredState == TEXT("Transition to In Air Loop");
	if (bIsInAirSelection && FMath::IsFinite(VerticalSpeed) && VerticalSpeed < 10.0f)
		return EVBGASPCMCPhase::Loop;
	return GetCMCPhase(AuthoredState);
}

void UVBGASPLocomotionStateLibrary::ApplyCMCWeaponPhase(FVBGASPCMCWeaponQuery& Query,
	FName AuthoredState, EVBGASPCMCPhase Phase)
{
	if (AuthoredState == TEXT("Idle Loop") || AuthoredState == TEXT("Transition to Idle Loop")) Query.MMState = EVBMMState::Idle;
	if (AuthoredState == TEXT("Locomotion Loop") || AuthoredState == TEXT("Transition to Locomotion Loop")) Query.MMState = EVBMMState::Moving;
	if (Phase == EVBGASPCMCPhase::Loop)
	{
		// Repeating playback is deliberately independent of MM's one-shot gates.
		// Otherwise a pivot-only source pool can make a valid CMC Loop empty forever.
		Query.IsStarting = Query.IsPivoting = Query.ShouldSpinTransition = Query.ShouldTurnInPlace = false;
		Query.JustTraversed = Query.JustLandedLight = Query.JustLandedHeavy = false;
	}
}

TArray<FName> UVBGASPLocomotionStateLibrary::ProjectCMCPlaybackTags(const TArray<FName>& PlaybackTags,
	const TArray<FName>& DatabaseTags)
{
	TArray<FName> Result = DatabaseTags;
	for (FName Tag : PlaybackTags)
	{
		if (Tag == TEXT("Start")) Result.AddUnique(TEXT("Starts"));
		else if (Tag == TEXT("Pivot")) Result.AddUnique(TEXT("Pivots"));
		else if (Tag == TEXT("Stop")) Result.AddUnique(TEXT("Stops"));
		else Result.AddUnique(Tag);
	}
	return Result;
}

bool UVBGASPLocomotionStateLibrary::StepPivotFallbackLatch(bool bLatched, bool bFellBackToLoop, bool bRawPivoting, bool bStanceChanged)
{
	if (!bRawPivoting || bStanceChanged) return false;
	return bLatched || bFellBackToLoop;
}

void UVBGASPLocomotionStateLibrary::RefreshPivotFallbackLatch(UAnimInstance* Instance, FVBGASPCMCState& State)
{
	if (!State.bPivotFallbackLatched || !Instance) return;
	if (HasPivotFallbackOwnerChanged(State))
	{
		State.bPivotFallbackLatched = false;
		return;
	}
	const bool bCrouchedNow = FStateView(Instance).Is(TEXT("Stance"), TEXT("Crouch"));
	State.bPivotFallbackLatched = StepPivotFallbackLatch(true, false, UVBGASPLocomotionLibrary::IsPivotingRaw(Instance),
		bCrouchedNow != State.bPivotFallbackCrouched);
}

bool UVBGASPLocomotionStateLibrary::HasPivotFallbackOwnerChanged(const FVBGASPCMCState& State)
{
	return State.bSelectedWeaponLocomotion != State.bUsesWeaponLocomotion || State.SelectedWeaponPool != State.CapturedWeaponPool;
}

bool UVBGASPLocomotionStateLibrary::ShouldReleaseCMCTransition(EVBGASPCMCPhase Phase,
	bool bLooping, bool bToLoopNotify, float TimeRemaining, float BlendTime)
{
	if (Phase != EVBGASPCMCPhase::Transition) return false;
	return bLooping || bToLoopNotify || (FMath::IsFinite(TimeRemaining) &&
		TimeRemaining <= FMath::Max(0.0f, BlendTime));
}

void UVBGASPLocomotionStateLibrary::UpdateCMCSelection(UAnimInstance* Instance)
{
	const FStateView View(Instance);
	FVBGASPCMCState* State = NativeProperty<FVBGASPCMCState>(View, TEXT("CMCState"));
	if (!State || !State->bSetupWasActive || State->bSelectionContractFailed || !State->ActiveChooser) return;
	// 피벗 대체 래치 풀기 - 막힌 판정이 아니라 원판정과 자세로 본다(StepPivotFallbackLatch 주석).
	RefreshPivotFallbackLatch(Instance, *State);
	const bool bHistoryReady = IsCMCPoseHistoryReady(Instance);
	bool bHistoryBecameReady = bHistoryReady && !State->bHistoryWasReady;
	State->bHistoryWasReady = bHistoryReady;
	bool bRestartFinishedAsset = false;
	if (State->bUsesWeaponLocomotion && State->bHasSelection)
	{
		const EVBGASPCMCPhase Phase = GetCMCPhase(CMCStateName(View));
		const EVBGASPCMCPhase SelectionPhase = GetCMCSelectionPhase(CMCStateName(View), View.Struct<FVector>(TEXT("Velocity")).Z);
		const FVBGASPCMCBlendInputs Inputs = GetCMCBlendInputs(Instance);
		FBlendStackAnimNodeReference Stack;
		const bool bCurrentSample = FindBlendStack(Instance, Stack) &&
			UBlendStackAnimNodeLibrary::GetCurrentAsset(Stack) == Inputs.Anim;
		const float Remaining = bCurrentSample ? UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining(Stack) : MAX_flt;
		const bool bRelease = ShouldReleaseCMCTransition(Phase, Inputs.bLoop,
			View.Bool(TEXT("NotifyTransition_ToLoop")), Remaining, Inputs.BlendTime);
		if (Phase == EVBGASPCMCPhase::Transition && !Inputs.bLoop && !bRelease && !State->bWaitingForPhase)
			bHistoryBecameReady = false;
		if (bRelease)
		{
			WriteBool(View, TEXT("NotifyTransition_ToLoop"), true);
			if (!State->bTransitionToLoopRequested) State->bNeedsReselect = true;
			State->bTransitionToLoopRequested = true;
		}
		// Recover a non-loop accidentally carried into a repeating state without
		// interrupting healthy starts/stops every 0.1 seconds.
		State->bNeedsReselect |= SelectionPhase == EVBGASPCMCPhase::Loop && !Inputs.bLoop && !State->bWaitingForPhase;
		State->bNeedsReselect |= State->bWaitingForPhase && State->SecondsSinceSearch >= 0.1f;
		bRestartFinishedAsset = bCurrentSample && !Inputs.bLoop && Remaining <= UE_SMALL_NUMBER;
	}
	if (State->bNeedsReselect || bHistoryBecameReady || !State->bHasSelection)
	{
		const uint64 PreviousSerial = State->SelectionSerial;
		SelectNativeCMC(Instance, static_cast<uint8>(View.Number(TEXT("StateMachineState"))),
			false, State->ActiveChooser);
		// Selecting the same exhausted asset needs one explicit restart. Restrict
		// this to an accepted selection; an empty phase must never restart the hold.
		if (bRestartFinishedAsset && State->SelectionSerial != PreviousSerial &&
			!State->bWaitingForPhase && !State->bSelectionContractFailed)
			State->PendingForceSerial = State->SelectionSerial;
	}
}

bool UVBGASPLocomotionStateLibrary::ConsumeCMCForceRequest(FVBGASPCMCState& State)
{
	if (State.PendingForceSerial == 0 || State.PendingForceSerial == State.ConsumedForceSerial) return false;
	State.ConsumedForceSerial = State.PendingForceSerial;
	return true;
}

void UVBGASPLocomotionStateLibrary::ConsumeCMCForceBlend(UAnimInstance* Instance)
{
	FVBGASPCMCState* State = NativeProperty<FVBGASPCMCState>(FStateView(Instance), TEXT("CMCState"));
	if (!State || State->bSelectionContractFailed || State->PendingForceSerial == State->ConsumedForceSerial) return;
	FBlendStackAnimNodeReference Node;
	if (FindBlendStack(Instance, Node) && ConsumeCMCForceRequest(*State))
		UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(Node);
}

FVBGASPCMCBlendInputs UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(UAnimInstance* Instance)
{
	const FVBGASPCMCBlendInputs* Inputs = NativeProperty<FVBGASPCMCBlendInputs>(FStateView(Instance), TEXT("CMCBlendInputs"));
	return Inputs ? *Inputs : FVBGASPCMCBlendInputs();
}

void UVBGASPLocomotionStateLibrary::SetBlendStackAnimFromChooser(UAnimInstance* Instance,
	uint8 StateMachineState, bool ForceBlend, UChooserTable* Chooser)
{
	if (!Instance) return;
	const FStateView State(Instance);
	if (const FVBGASPCMCState* Native = NativeProperty<FVBGASPCMCState>(State, TEXT("CMCState"));
		Native && Native->ActiveChooser)
	{
		SelectNativeCMC(Instance, StateMachineState, ForceBlend, Native->ActiveChooser);
		return;
	}
	if (Chooser && Chooser->GetContextData().Num() == 3)
	{
		SelectNativeCMC(Instance, StateMachineState, ForceBlend, Chooser);
		return;
	}
	const FStateView Blend = State.Child(TEXT("BlendStackInputs"));
	bool bContractValid = WriteNumber(State, TEXT("StateMachineState"), StateMachineState);
	bContractValid &= CopyField(State, TEXT("Previous_BlendStackInputs"), State, TEXT("BlendStackInputs"));
	bContractValid &= WriteBool(State, TEXT("NoValidAnim"), false);
	bContractValid &= WriteBool(State, TEXT("NotifyTransition_Re-Transition"), false);
	bContractValid &= WriteBool(State, TEXT("NotifyTransition_ToLoop"), false);
	if (!bContractValid)
	{
		WriteBool(State, TEXT("NoValidAnim"), true);
		UE_LOG(LogVBGASPState, Error, TEXT("Invalid native GASP blend-stack property contract on %s"), *GetNameSafe(Instance));
		return;
	}

	TArray<UAnimationAsset*> Candidates;
	FInstancedStruct Outputs;
	if (!EvaluateAnimations(Instance, Chooser, Candidates, Outputs))
	{
		StoreValidAnimations(State, Candidates);
		WriteBool(State, TEXT("NoValidAnim"), true);
		UE_LOG(LogVBGASPState, Error, TEXT("Invalid GASP state Chooser context: %s / %s"), *GetNameSafe(Instance), *GetNameSafe(Chooser));
		return;
	}
	if (!StoreValidAnimations(State, Candidates) || Candidates.IsEmpty())
	{
		WriteBool(State, TEXT("NoValidAnim"), true);
		return;
	}

	const FStateView Output(Outputs.GetScriptStruct(), Outputs.GetMutableMemory());
	UAnimationAsset* First = Candidates[0];
	bool bLoop = false;
	UPoseSearchLibrary::IsAnimationAssetLooping(First, bLoop);
	bContractValid = WriteObject(Blend, TEXT("Anim"), First);
	bContractValid &= WriteBool(Blend, TEXT("Loop"), bLoop);
	bContractValid &= WriteNumber(Blend, TEXT("StartTime"), Output.Number(TEXT("StartTime")));
	bContractValid &= WriteNumber(Blend, TEXT("BlendTime"), Output.Number(TEXT("BlendTime")));
	bContractValid &= WriteObject(Blend, TEXT("BlendProfile"), const_cast<UBlendProfile*>(Instance->GetBlendProfileByName(ReadName(Output, TEXT("BlendProfile")))));
	bContractValid &= CopyField(Blend, TEXT("Tags"), Output, TEXT("Tags"));
	if (!bContractValid)
	{
		WriteBool(State, TEXT("NoValidAnim"), true);
		UE_LOG(LogVBGASPState, Error, TEXT("Invalid GASP BlendStackInputs fields on %s"), *GetNameSafe(Instance));
		return;
	}

	if (Output.Bool(TEXT("UseMM")))
	{
		TArray<UObject*> SearchAssets;
		SearchAssets.Reserve(Candidates.Num());
		for (UAnimationAsset* Candidate : Candidates) SearchAssets.Add(Candidate);
		FPoseSearchBlueprintResult Result;
		UPoseSearchLibrary::MotionMatch(Instance, SearchAssets, TEXT("PoseHistory"), {}, {}, Result);
		WriteNumber(State, TEXT("Search Cost"), Result.SearchCost);
		UAnimationAsset* Selected = Cast<UAnimationAsset>(Result.SelectedAnim);
		const double CostLimit = Output.Number(TEXT("MMCostLimit"));
		const bool bAcceptCost = CostLimit > 0.0 ? Result.SearchCost <= CostLimit : true;
		if (!Selected || !bAcceptCost)
		{
			WriteBool(State, TEXT("NoValidAnim"), true);
			return;
		}
		UPoseSearchLibrary::IsAnimationAssetLooping(Selected, bLoop);
		WriteObject(Blend, TEXT("Anim"), Selected);
		WriteBool(Blend, TEXT("Loop"), bLoop);
		WriteNumber(Blend, TEXT("StartTime"), Result.SelectedTime);
	}

	if (ForceBlend)
	{
		FBlendStackAnimNodeReference Node;
		if (FindBlendStack(Instance, Node)) UBlendStackAnimNodeLibrary::ForceBlendNextUpdate(Node);
	}
}

void UVBGASPLocomotionStateLibrary::OnStateEntry_IdleLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Idle Loop"), false);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToIdle(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to Idle Loop"), true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_LocomotionLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Locomotion Loop"), false, true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToLocomotion(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to Locomotion Loop"), true, true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_InAirLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("In Air Loop"), false);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToInAir(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to In Air Loop"), true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_IdleBreak(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Idle Break"), true);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_SlideLoop(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Slide Loop"), false);
}
void UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToSlide(UAnimInstance* Instance, UChooserTable* Chooser)
{
	EnterState(Instance, Chooser, TEXT("Transition to Slide"), true);
}

void UVBGASPLocomotionStateLibrary::OnUpdate_TransitionToLocomotion(UAnimInstance* Instance)
{
	if (!Instance) return;
	const FStateView State(Instance);
	const FRotator Rotation = FMath::RInterpTo(State.Struct<FRotator>(TEXT("TargetRotationOnTransitionStart")),
		State.Struct<FRotator>(TEXT("TargetRotation")), Instance->GetDeltaSeconds(), 5.f);
	const FStructProperty* Property = CastField<FStructProperty>(State.Find(TEXT("TargetRotationOnTransitionStart")));
	if (Property && Property->Struct == TBaseStructure<FRotator>::Get())
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(State.Data), &Rotation);
}

bool UVBGASPLocomotionStateLibrary::IsAnimationAlmostComplete(UAnimInstance* Instance)
{
	FBlendStackAnimNodeReference Node;
	return FindBlendStack(Instance, Node) && !UBlendStackAnimNodeLibrary::IsCurrentAssetLooping(Node)
		&& UBlendStackAnimNodeLibrary::GetCurrentAssetTimeRemaining(Node) <= 0.75f;
}

bool UVBGASPLocomotionStateLibrary::PlayLand(UAnimInstance* Instance)
{
	const FStateView State(Instance);
	return State.Is(TEXT("MovementMode"), TEXT("OnGround")) && State.Is(TEXT("MovementMode_LastFrame"), TEXT("InAir"));
}

bool UVBGASPLocomotionStateLibrary::PlayMovingLand(UAnimInstance* Instance)
{
	return PlayLand(Instance) && FMath::Abs(UVBGASPLocomotionLibrary::Get_TrajectoryTurnAngle(Instance)) <= 120.0;
}

FPoseHistoryReference UVBGASPLocomotionStateLibrary::Get_PoseHistoryReference(UAnimInstance* Instance)
{
	EAnimNodeReferenceConversionResult Result;
	const FPoseSearchHistoryCollectorAnimNodeReference Node = UPoseSearchHistoryCollectorAnimNodeLibrary::ConvertToPoseHistoryNode(
		FindTaggedNode(Instance, TEXT("PoseHistory")), Result);
	return Result == EAnimNodeReferenceConversionResult::Succeeded ?
		UPoseSearchHistoryCollectorAnimNodeLibrary::GetPoseHistoryReference(Node) : FPoseHistoryReference();
}
