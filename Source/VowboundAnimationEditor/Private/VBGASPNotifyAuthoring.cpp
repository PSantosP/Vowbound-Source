#include "VBGASPNotifyAuthoring.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/VBGASPAnimNotifies.h"
#include "Camera/CameraShakeBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

// 에디터 모듈은 게임 모듈의 LogVB 를 못 쓴다(내보내지 않는다). 이름은 파일마다 달라야 unity 묶음에서 겹치지 않는다.
DEFINE_LOG_CATEGORY_STATIC(LogVBGASPNotifyAuthoring, Log, All);

namespace
{
	struct FNotifyFamily
	{
		const TCHAR* Kind;
		const TCHAR* BlueprintName;
		UClass* NativeClass;
	};

	const TArray<FNotifyFamily>& Families()
	{
		static const TArray<FNotifyFamily> Values = {
			{TEXT("EarlyTransition"), TEXT("BP_NotifyState_EarlyTransition"), UVBGASPNotifyState_EarlyTransition::StaticClass()},
			{TEXT("MontageBlendOut"), TEXT("BP_NotifyState_MontageBlendOut"), UVBGASPNotifyState_MontageBlendOut::StaticClass()},
			{TEXT("OverrideCapsuleCollision"), TEXT("BP_NotifyState_OverrideCapsuleCollision"), UVBGASPNotifyState_OverrideCapsuleCollision::StaticClass()},
			{TEXT("OverrideMovementMode"), TEXT("BP_NotifyState_OverrideMovementMode"), UVBGASPNotifyState_OverrideMovementMode::StaticClass()},
			{TEXT("SimpleCameraShake"), TEXT("BP_AnimNotify_SimpleCameraShake"), UVBGASPAnimNotify_SimpleCameraShake::StaticClass()},
			{TEXT("FoleyEvent"), TEXT("BP_AnimNotify_FoleyEvent"), UVBGASPAnimNotify_FoleyEvent::StaticClass()}
		};
		return Values;
	}

	const FNotifyFamily* FindFamily(const UObject* Notify, bool& bNative)
	{
		bNative = false;
		if (!Notify) return nullptr;
		for (const FNotifyFamily& Family : Families())
		{
			if (Notify->IsA(Family.NativeClass)) { bNative = true; return &Family; }
			const FString Source = FString::Printf(TEXT("/Game/Blueprints/AnimNotifies/%s.%s_C"), Family.BlueprintName, Family.BlueprintName);
			for (const UClass* Class = Notify->GetClass(); Class; Class = Class->GetSuperClass())
				if (Class->GetPathName() == Source) return &Family;
		}
		return nullptr;
	}

	FString Normalize(FString Text)
	{
		Text.ReplaceInline(TEXT(" "), TEXT(""));
		Text.ReplaceInline(TEXT("-"), TEXT(""));
		Text.ReplaceInline(TEXT("_"), TEXT(""));
		return Text.ToLower();
	}

	bool ReadEnum(const UObject* Object, FName Name, FString& Display)
	{
		const FProperty* Property = Object->GetClass()->FindPropertyByName(Name);
		const UEnum* Enum = nullptr;
		int64 Value = 0;
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
		{
			Enum = Byte->Enum;
			Value = Byte->GetPropertyValue_InContainer(Object);
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
			Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Object));
		}
		if (!Enum || !Enum->IsValidEnumValue(Value)) return false;
		// UUserDefinedEnum returns the authored source label, independent of editor locale.
		Display = Normalize(Enum->GetAuthoredNameStringByValue(Value));
		return true;
	}

	template <typename T> bool CopySame(const UObject* Old, UObject* New, FName OldName, FName NewName)
	{
		const T* From = FindFProperty<T>(Old->GetClass(), OldName);
		const T* To = FindFProperty<T>(New->GetClass(), NewName);
		if (!From || !To || !From->SameType(To)) return false;
		To->CopyCompleteValue(To->ContainerPtrToValuePtr<void>(New), From->ContainerPtrToValuePtr<void>(Old));
		return true;
	}

	bool CopyNumber(const UObject* Old, FName Name, float& Value)
	{
		const FNumericProperty* Number = FindFProperty<FNumericProperty>(Old->GetClass(), Name);
		if (!Number) return false;
		const void* Data = Number->ContainerPtrToValuePtr<void>(Old);
		Value = Number->IsFloatingPoint() ? static_cast<float>(Number->GetFloatingPointPropertyValue(Data)) : static_cast<float>(Number->GetSignedIntPropertyValue(Data));
		return FMath::IsFinite(Value);
	}

	bool CopyProperties(const UObject* Old, UObject* New)
	{
		// Copy editor presentation and preserve authored state behavior flags, retaining native isolation.
		bool Valid = CopySame<FStructProperty>(Old, New, TEXT("NotifyColor"), TEXT("NotifyColor")) &&
			CopySame<FBoolProperty>(Old, New, TEXT("bShouldFireInEditor"), TEXT("bShouldFireInEditor"));
		if (auto* State = Cast<UAnimNotifyState>(New))
			State->NotifyStateBehaviorFlags |= CastChecked<UAnimNotifyState>(Old)->NotifyStateBehaviorFlags;
		FString Display;
		if (auto* Early = Cast<UVBGASPNotifyState_EarlyTransition>(New))
		{
			Valid &= ReadEnum(Old, TEXT("TransitionCondition"), Display);
			if (Display == TEXT("always")) Early->TransitionCondition = EVBGASPEarlyTransitionCondition::Always;
			else if (Display == TEXT("gaitnotequal")) Early->TransitionCondition = EVBGASPEarlyTransitionCondition::GaitNotEqual;
			else Valid = false;
			Valid &= ReadEnum(Old, TEXT("TransitionDestination"), Display);
			if (Display == TEXT("retransition")) Early->TransitionDestination = EVBGASPEarlyTransitionDestination::ReTransition;
			else if (Display == TEXT("transitiontoloop")) Early->TransitionDestination = EVBGASPEarlyTransitionDestination::ToLoop;
			else Valid = false;
			Valid &= ReadEnum(Old, TEXT("GaitNotEqual"), Display);
			if (Display == TEXT("walk")) Early->GaitNotEqual = EVBGait::Walk;
			else if (Display == TEXT("run")) Early->GaitNotEqual = EVBGait::Run;
			else if (Display == TEXT("sprint")) Early->GaitNotEqual = EVBGait::Sprint;
			else Valid = false;
		}
		else if (auto* Blend = Cast<UVBGASPNotifyState_MontageBlendOut>(New))
		{
			Valid &= ReadEnum(Old, TEXT("BlendOutCondition"), Display);
			if (Display == TEXT("forceblendout")) Blend->BlendOutCondition = EVBGASPMontageBlendOutCondition::ForceBlendOut;
			else if (Display == TEXT("withmovementinput")) Blend->BlendOutCondition = EVBGASPMontageBlendOutCondition::WithMovementInput;
			else if (Display == TEXT("iffalling")) Blend->BlendOutCondition = EVBGASPMontageBlendOutCondition::IfFalling;
			else Valid = false;
			Valid &= CopyNumber(Old, TEXT("BlendOutTime"), Blend->BlendOutTime);
			// The old local BP lost this original field. Restore the verified source default if absent.
			if (Old->GetClass()->FindPropertyByName(TEXT("BlendProfile")))
				Valid &= CopySame<FNameProperty>(Old, New, TEXT("BlendProfile"), TEXT("BlendProfile"));
		}
		else if (Cast<UVBGASPNotifyState_OverrideCapsuleCollision>(New))
		{
			Valid &= CopySame<FByteProperty>(Old, New, TEXT("New Response"), TEXT("NewResponse"));
			Valid &= CopySame<FBoolProperty>(Old, New, TEXT("ResetOnEnd"), TEXT("bResetOnEnd"));
		}
		else if (auto* Movement = Cast<UVBGASPNotifyState_OverrideMovementMode>(New))
		{
			Valid &= CopySame<FNameProperty>(Old, New, TEXT("OverrideMovementMode"), TEXT("SourceMoverOverrideMode"));
			Valid &= CopySame<FNameProperty>(Old, New, TEXT("EndingMovementMode"), TEXT("SourceMoverEndingMode"));
			Movement->bApplyToCMC = false; // Source Mover casts never affected this project's CMC actors.
		}
		else if (Cast<UVBGASPAnimNotify_SimpleCameraShake>(New))
		{
			Valid &= CopySame<FClassProperty>(Old, New, TEXT("Shake"), TEXT("Shake"));
		}
		else if (auto* Foley = Cast<UVBGASPAnimNotify_FoleyEvent>(New))
		{
			Valid &= CopySame<FStructProperty>(Old, New, TEXT("Event"), TEXT("Event"));
			Valid &= ReadEnum(Old, TEXT("Side"), Display);
			if (Display == TEXT("none")) Foley->Side = EVBGASPFoleySide::None;
			else if (Display == TEXT("left")) Foley->Side = EVBGASPFoleySide::Left;
			else if (Display == TEXT("right")) Foley->Side = EVBGASPFoleySide::Right;
			else Valid = false;
			Valid &= CopyNumber(Old, TEXT("VolumeMultiplier"), Foley->VolumeMultiplier);
			Valid &= CopyNumber(Old, TEXT("PitchMultiplier"), Foley->PitchMultiplier);
			Valid &= CopySame<FStructProperty>(Old, New, TEXT("VisLogDebugColor"), TEXT("VisLogDebugColor"));
			Valid &= CopySame<FStrProperty>(Old, New, TEXT("VisLogDebugText"), TEXT("VisLogDebugText"));
		}
		return Valid;
	}
}

TArray<FVBGASPNotifyMigrationEntry> UVBGASPNotifyAuthoring::InspectNotifies(UAnimSequenceBase* Animation)
{
	TArray<FVBGASPNotifyMigrationEntry> Entries;
	if (!Animation) return Entries;
	for (int32 Index = 0; Index < Animation->Notifies.Num(); ++Index)
	{
		const FAnimNotifyEvent& Event = Animation->Notifies[Index];
		UObject* Notify = Event.Notify ? static_cast<UObject*>(Event.Notify) : static_cast<UObject*>(Event.NotifyStateClass);
		bool bNative;
		const FNotifyFamily* Family = FindFamily(Notify, bNative);
		if (!Family) continue;
		FVBGASPNotifyMigrationEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Index = Index;
		Entry.Kind = Family->Kind;
		Entry.ClassPath = Notify->GetClass()->GetPathName();
		Entry.bNative = bNative;
		FAnimNotifyEvent Snapshot(Event);
		Snapshot.Notify = nullptr;
		Snapshot.NotifyStateClass = nullptr;
		FAnimNotifyEvent::StaticStruct()->ExportText(Entry.EventData, &Snapshot, nullptr, Animation, PPF_None, nullptr);
		for (TFieldIterator<FProperty> It(Notify->GetClass()); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Edit) || It->HasAnyPropertyFlags(CPF_Transient)) continue;
			FString Value;
			It->ExportText_InContainer(0, Value, Notify, nullptr, Notify, PPF_None);
			Entry.NotifyData += It->GetName() + TEXT("=") + Value + TEXT("\n");
		}
	}
	return Entries;
}

int32 UVBGASPNotifyAuthoring::ReplaceNotifies(UAnimSequenceBase* Animation, UVBGASPFoleyBank* Bank)
{
	if (!Animation || !Bank || !Animation->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))) return -1;
	TMap<int32, UObject*> Replacements;
	for (int32 Index = 0; Index < Animation->Notifies.Num(); ++Index)
	{
		const FAnimNotifyEvent& Event = Animation->Notifies[Index];
		const UObject* Old = Event.Notify ? static_cast<UObject*>(Event.Notify) : static_cast<UObject*>(Event.NotifyStateClass);
		bool bNative;
		const FNotifyFamily* Family = FindFamily(Old, bNative);
		if (!Family || bNative) continue;
		UObject* New = NewObject<UObject>(Animation, Family->NativeClass, NAME_None, RF_Transactional);
		if (!CopyProperties(Old, New))
		{
			UE_LOG(LogVBGASPNotifyAuthoring, Error, TEXT("Notify migration rejected %s[%d] %s: property contract mismatch; no events changed"), *Animation->GetPathName(), Index, *Old->GetPathName());
			return -1;
		}
		if (auto* Foley = Cast<UVBGASPAnimNotify_FoleyEvent>(New)) Foley->Bank = Bank;
		FAnimNotifyEvent Probe(Event);
		if (auto* State = Cast<UAnimNotifyState>(New)) Probe.NotifyStateClass = State;
		else Probe.Notify = CastChecked<UAnimNotify>(New);
		if (Probe.IsBranchingPoint() != Event.IsBranchingPoint())
		{
			UE_LOG(LogVBGASPNotifyAuthoring, Error, TEXT("Notify migration rejected %s[%d]: native branching policy differs"), *Animation->GetPathName(), Index);
			return -1;
		}
		Replacements.Add(Index, New);
	}
	if (Replacements.IsEmpty()) return 0;
	Animation->Modify();
	for (const TPair<int32, UObject*>& Pair : Replacements)
	{
		FAnimNotifyEvent& Event = Animation->Notifies[Pair.Key];
		// Deliberately change only the instanced object: time, duration, GUID, track, filtering,
		// link methods, threshold and branching/queued tick type remain byte-for-byte authored data.
		if (auto* State = Cast<UAnimNotifyState>(Pair.Value)) Event.NotifyStateClass = State;
		else Event.Notify = CastChecked<UAnimNotify>(Pair.Value);
	}
	// Neither array storage nor event type/time/branching policy changed, so track pointers
	// and montage marker indices remain valid. RefreshCacheData would sort/reassign tracks
	// and propagate child montage edits outside this explicitly backed-up operation.
	Animation->MarkPackageDirty();
	return Replacements.Num();
}

bool UVBGASPNotifyAuthoring::AddFoleyStep(UAnimSequence* Animation, UVBGASPFoleyBank* Bank,
	float Time, bool bLeftFoot, bool bRun)
{
	if (!Animation || !Bank || !Animation->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
		|| !FMath::IsFinite(Time) || Time < 0.f || Time >= Animation->GetPlayLength()) return false;
	const FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(
		bRun ? FName(TEXT("Foley.Event.Run")) : FName(TEXT("Foley.Event.Walk")), false);
	if (!EventTag.IsValid() || !Bank->Assets.Contains(EventTag)) return false;
	const FName NotifyName = bLeftFoot ? FName(TEXT("VB_Foley_Left")) : FName(TEXT("VB_Foley_Right"));
	for (const FAnimNotifyEvent& Existing : Animation->Notifies)
		if (Existing.NotifyName == NotifyName && Cast<UVBGASPAnimNotify_FoleyEvent>(Existing.Notify)) return true;

	Animation->Modify();
	if (Animation->AnimNotifyTracks.IsEmpty()) Animation->InitializeNotifyTrack();
	UVBGASPAnimNotify_FoleyEvent* Notify = NewObject<UVBGASPAnimNotify_FoleyEvent>(Animation, NAME_None, RF_Transactional);
	Notify->Bank = Bank;
	Notify->Event = EventTag;
	Notify->Side = bLeftFoot ? EVBGASPFoleySide::Left : EVBGASPFoleySide::Right;
	FAnimNotifyEvent& Entry = Animation->Notifies.AddDefaulted_GetRef();
	Entry.NotifyName = NotifyName;
	Entry.Notify = Notify;
	Entry.Guid = FGuid::NewGuid();
	Entry.TrackIndex = 0;
	Entry.Link(Animation, Time);
	Entry.TriggerTimeOffset = GetTriggerTimeOffsetForType(Animation->CalculateOffsetForNotify(Time));
	Animation->RefreshCacheData();
	Animation->PostEditChange();
	Animation->MarkPackageDirty();
	return true;
}

bool UVBGASPNotifyAuthoring::ConfigureFoleyBank(UVBGASPFoleyBank* Bank, const TArray<FName>& Tags,
	const TArray<USoundBase*>& Sounds, USoundAttenuation* Attenuation)
{
	if (!Bank || !Attenuation || Tags.Num() != 16 || Sounds.Num() != Tags.Num()) return false;
	TMap<FGameplayTag, TObjectPtr<USoundBase>> Assets;
	for (int32 Index = 0; Index < Tags.Num(); ++Index)
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(Tags[Index], false);
		if (!Tag.IsValid() || !Sounds[Index] || Assets.Contains(Tag)) return false;
		Assets.Add(Tag, Sounds[Index]);
	}
	Bank->Modify();
	Bank->Assets = MoveTemp(Assets);
	Bank->Attenuation = Attenuation;
	Bank->MarkPackageDirty();
	return true;
}
