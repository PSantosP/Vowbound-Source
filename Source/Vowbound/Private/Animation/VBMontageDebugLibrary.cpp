// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBMontageDebugLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"            // UAnimSequence::bEnableRootMotion / bForceRootLock
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimNotifies/AnimNotify.h"

FName UVBMontageDebugLibrary::GetMontageSlotName(UAnimMontage* Montage)
{
	// SlotAnimTracks 는 UAnimMontage public 멤버 — C++ 에선 직접 접근 가능(python 만 미노출).
	if (Montage && Montage->SlotAnimTracks.Num() > 0)
	{
		return Montage->SlotAnimTracks[0].SlotName;
	}
	return NAME_None;
}

int32 UVBMontageDebugLibrary::GetMontageSlotCount(UAnimMontage* Montage)
{
	return Montage ? Montage->SlotAnimTracks.Num() : 0;
}

TArray<FString> UVBMontageDebugLibrary::GetMontageNotifySummary(UAnimMontage* Montage)
{
	TArray<FString> Out;
	if (!Montage)
	{
		return Out;
	}
	// Notifies 는 UAnimSequenceBase public getter 로 접근(직접 멤버는 protected).
	for (const FAnimNotifyEvent& Ev : Montage->Notifies)
	{
		const UClass* Cls = Ev.NotifyStateClass ? Ev.NotifyStateClass->GetClass()
			: (Ev.Notify ? Ev.Notify->GetClass() : nullptr);

		// MotionWarping notify 의 WarpTargetName 추출 (reflection — RootMotionModifier.WarpTargetName).
		// cpp WT_FrontLedge("FrontLedge") 와 일치해야 워프 작동. 불일치면 RootMotion 폭주.
		FString WarpName;
		if (Ev.NotifyStateClass)
		{
			UObject* Mod = nullptr;
			if (const FObjectProperty* MP = CastField<FObjectProperty>(
				Ev.NotifyStateClass->GetClass()->FindPropertyByName(TEXT("RootMotionModifier"))))
			{
				Mod = MP->GetObjectPropertyValue_InContainer(Ev.NotifyStateClass);
			}
			if (Mod)
			{
				if (const FNameProperty* NP = CastField<FNameProperty>(
					Mod->GetClass()->FindPropertyByName(TEXT("WarpTargetName"))))
				{
					WarpName = NP->GetPropertyValue_InContainer(Mod).ToString();
				}
			}
		}

		Out.Add(FString::Printf(TEXT("%s @%.3f dur=%.3f cls=%s warpTarget=%s"),
			*Ev.NotifyName.ToString(),
			Ev.GetTriggerTime(),
			Ev.GetDuration(),
			Cls ? *Cls->GetName() : TEXT("None"),
			WarpName.IsEmpty() ? TEXT("-") : *WarpName));
	}
	return Out;
}

FVector UVBMontageDebugLibrary::GetMontageRootMotionDelta(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return FVector::ZeroVector;
	}
	// 0 ~ 전체 길이 구간의 RootMotion 누적. UE5.6+ 신 API(FAnimExtractContext) — 구 오버로드는 deprecated(C4996).
	// 기본 Context = 구 래퍼와 동일 동작 (구 deprecated 인라인 래퍼가 하던 그대로 - 그 래퍼는 UE 5.8 에서 삭제됐다).
	const FAnimExtractContext ExtractContext;
	const FTransform RM = Montage->ExtractRootMotionFromTrackRange(0.f, Montage->GetPlayLength(), ExtractContext);
	return RM.GetTranslation();
}

FString UVBMontageDebugLibrary::GetMontageRootMotionInfo(UAnimMontage* Montage)
{
	if (!Montage || Montage->SlotAnimTracks.Num() == 0)
	{
		return TEXT("no-slot");
	}
	const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	if (Track.AnimSegments.Num() == 0)
	{
		return TEXT("no-segment");
	}
	UAnimSequenceBase* Seq = Track.AnimSegments[0].GetAnimReference();
	UAnimSequence* AS = Cast<UAnimSequence>(Seq);
	if (!AS)
	{
		return FString::Printf(TEXT("src=%s (not UAnimSequence)"), *GetNameSafe(Seq));
	}
	return FString::Printf(TEXT("src=%s enableRM=%d forceRootLock=%d"),
		*AS->GetName(), (int)AS->bEnableRootMotion, (int)AS->bForceRootLock);
}

FString UVBMontageDebugLibrary::GetMontageWarpModifierInfo(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return TEXT("null");
	}
	for (const FAnimNotifyEvent& Ev : Montage->Notifies)
	{
		if (!Ev.NotifyStateClass)
		{
			continue;
		}
		const FObjectProperty* MP = CastField<FObjectProperty>(
			Ev.NotifyStateClass->GetClass()->FindPropertyByName(TEXT("RootMotionModifier")));
		if (!MP)
		{
			continue;
		}
		UObject* Mod = MP->GetObjectPropertyValue_InContainer(Ev.NotifyStateClass);
		if (!Mod)
		{
			continue;
		}
		// SkewWarp modifier 의 모든 bool/byte(enum) 프로퍼티 덤프 (bIgnoreZAxis / WarpPointAnimProvider 등).
		FString Out = Mod->GetClass()->GetName() + TEXT(": ");
		for (TFieldIterator<FProperty> It(Mod->GetClass()); It; ++It)
		{
			FProperty* P = *It;
			if (const FBoolProperty* BP = CastField<FBoolProperty>(P))
			{
				Out += FString::Printf(TEXT("%s=%d "), *P->GetName(), (int)BP->GetPropertyValue_InContainer(Mod));
			}
			else if (const FByteProperty* YP = CastField<FByteProperty>(P))
			{
				Out += FString::Printf(TEXT("%s=%d "), *P->GetName(), (int)YP->GetPropertyValue_InContainer(Mod));
			}
			else if (const FNameProperty* NP = CastField<FNameProperty>(P))
			{
				Out += FString::Printf(TEXT("%s=%s "), *P->GetName(), *NP->GetPropertyValue_InContainer(Mod).ToString());
			}
			else if (const FFloatProperty* FP = CastField<FFloatProperty>(P))   // MaxSpeedClampRatio 등 (2026-09-06)
			{
				Out += FString::Printf(TEXT("%s=%.2f "), *P->GetName(), FP->GetPropertyValue_InContainer(Mod));
			}
		}
		return Out;
	}
	return TEXT("no-warp-notify");
}

FString UVBMontageDebugLibrary::GetMontageSegmentAnimPath(UAnimMontage* Montage)
{
	if (!Montage || Montage->SlotAnimTracks.Num() == 0)
	{
		return TEXT("no-slot");
	}
	const FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	if (Track.AnimSegments.Num() == 0)
	{
		return TEXT("no-segment");
	}
	UAnimSequenceBase* Anim = Track.AnimSegments[0].GetAnimReference();
	return Anim ? Anim->GetPathName() : TEXT("NULL");
}

FString UVBMontageDebugLibrary::GetMontageBranchInDatabasePath(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return TEXT("null-montage");
	}
	// PoseSearchBranchIn notify 의 Database(TObjectPtr<UPoseSearchDatabase>) 를 reflection 으로 읽는다
	// (PoseSearch 헤더 의존 없이). MotionMatch 가 몽타주→검색DB 를 찾는 실제 링크.
	for (const FAnimNotifyEvent& Ev : Montage->Notifies)
	{
		if (!Ev.NotifyStateClass)
		{
			continue;
		}
		const UClass* Cls = Ev.NotifyStateClass->GetClass();
		if (!Cls->GetName().Contains(TEXT("PoseSearchBranchIn")))
		{
			continue;
		}
		if (const FObjectProperty* DBProp = CastField<FObjectProperty>(Cls->FindPropertyByName(TEXT("Database"))))
		{
			UObject* DB = DBProp->GetObjectPropertyValue_InContainer(Ev.NotifyStateClass);
			return DB ? DB->GetPathName() : TEXT("NULL-Database");
		}
		return TEXT("no-Database-prop");
	}
	return TEXT("no-BranchIn-notify");
}

FName UVBMontageDebugLibrary::GetMontageSectionNext(UAnimMontage* Montage, FName SectionName)
{
	if (!Montage) return NAME_None;
	const int32 Index = Montage->GetSectionIndex(SectionName);
	return Montage->CompositeSections.IsValidIndex(Index) ? Montage->CompositeSections[Index].NextSectionName : NAME_None;
}
