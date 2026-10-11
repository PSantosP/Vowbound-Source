#include "VBWeaponFinisherAuthoring.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/VBAnimNotify_InteractionRagdoll.h"
#include "Animation/VBAnimNotifyState_InteractionWarp.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContextualAnimSceneAsset.h"
#include "Data/VBFinisherConfig.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "PoseSearch/PoseSearchIndex.h"
#include "PoseSearch/PoseSearchInteractionAsset.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "RootMotionModifier_SkewWarp.h"
#include "UObject/Package.h"

// 에디터 모듈은 게임 모듈의 LogVB 를 못 쓴다(내보내지 않는다). 이름은 파일마다 달라야 unity 묶음에서 겹치지 않는다
//   (같은 모듈의 VBFinisherCameraAuthoringSession.cpp 가 LogVBFinisherAuthoring 을 쓴다).
DEFINE_LOG_CATEGORY_STATIC(LogVBWeaponFinisherAuthoring, Log, All);

namespace
{
	const FString GeneratedRoot(TEXT("/Game/Vowbound/Animations/MotionMatching/Finisher/Weapons/"));
	const FName AttackerRole(TEXT("Attacker"));
	const FName VictimRole(TEXT("Victim"));

	template <typename T> T* FindOrCreateAsset(const FString& PackagePath)
	{
		if (UObject* Existing = LoadObject<UObject>(nullptr, *PackagePath)) return Cast<T>(Existing);
		UPackage* Package = CreatePackage(*PackagePath);
		T* Asset = NewObject<T>(Package, *FPackageName::GetLongPackageAssetName(PackagePath), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		return Asset;
	}

	template <typename T> T* DuplicateGenerated(T* Source, const FString& Directory, const FString& Name)
	{
		const FString Path = Directory / Name;
		if (UObject* Existing = LoadObject<UObject>(nullptr, *Path)) return Cast<T>(Existing);
		IAssetTools& Assets = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
		return Cast<T>(Assets.DuplicateAsset(Name, Directory, Source));
	}

	const FAnimSegment* GetSingleSegment(const UAnimMontage* Montage)
	{
		if (!Montage || Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1) return nullptr;
		const FAnimSegment& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		// This converter deliberately rejects time-remapped/multi-section scenes instead of silently flattening them.
		if (!FMath::IsNearlyZero(Segment.StartPos) || !FMath::IsNearlyZero(Segment.AnimStartTime) ||
			!FMath::IsNearlyEqual(Segment.AnimPlayRate, 1.0f) || Segment.LoopingCount != 1 ||
			!FMath::IsNearlyEqual(Montage->RateScale, 1.0f) || Montage->CompositeSections.Num() > 1) return nullptr;
		return &Segment;
	}

	void AddWarp(UAnimMontage* Montage, float EndTime)
	{
		UVBAnimNotifyState_InteractionWarp* Notify = NewObject<UVBAnimNotifyState_InteractionWarp>(Montage, NAME_None, RF_Transactional);
		Notify->AlignmentAnchorRole = AttackerRole;
		URootMotionModifier_SkewWarp* Modifier = NewObject<URootMotionModifier_SkewWarp>(Notify, NAME_None, RF_Transactional);
		Modifier->WarpTargetName = TEXT("VBWeaponInteraction");
		Modifier->bWarpTranslation = true;
		Modifier->bIgnoreZAxis = true;
		Modifier->bWarpRotation = true;
		Notify->RootMotionModifier = Modifier;
		FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TEXT("VBWeaponInteractionWarp");
		Event.NotifyStateClass = Notify;
		Event.Guid = FGuid::NewGuid();
		Event.Link(Montage, 0.0f);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(0.0f));
		Event.SetDuration(EndTime);
		Event.EndLink.Link(Montage, EndTime);
	}

	void AddRelease(UAnimMontage* Montage, float Time)
	{
		UVBAnimNotify_InteractionRagdoll* Notify = NewObject<UVBAnimNotify_InteractionRagdoll>(Montage, NAME_None, RF_Transactional);
		FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.NotifyName = TEXT("VBWeaponInteractionRelease");
		Event.Notify = Notify;
		Event.Guid = FGuid::NewGuid();
		Event.MontageTickType = EMontageNotifyTickType::BranchingPoint;
		Event.Link(Montage, Time);
		Event.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(Time));
	}
}

FString UVBWeaponFinisherAuthoring::BuildWeaponFinisherAssets(UVBFinisherConfig* Config, UPoseSearchSchema* Schema, const FString& DestinationPath)
{
	if (!Config || !Config->Scene || !Schema || !DestinationPath.StartsWith(GeneratedRoot) ||
		!FPackageName::IsValidLongPackageName(DestinationPath)) return TEXT("Invalid config/schema or destination outside generated weapon folder");
	const UContextualAnimSceneAsset* Scene = Config->Scene;
	const FContextualAnimSceneSection* Section = Scene->GetSection(0);
	if (Scene->GetNumSections() != 1 || !Section || Section->GetNumAnimSets() != 1 || Scene->GetNumRoles() != 2 ||
		!Scene->GetAttachmentParams().IsEmpty()) return TEXT("Expected one two-role scene without attachments");
	const FName Roles[] = {AttackerRole, VictimRole};
	const FContextualAnimTrack* Tracks[2];
	UAnimMontage* Sources[2];
	UAnimSequence* Sequences[2];
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Tracks[Index] = Scene->GetAnimTrack(0, 0, Roles[Index]);
		Sources[Index] = Tracks[Index] ? Cast<UAnimMontage>(Tracks[Index]->Animation) : nullptr;
		const FAnimSegment* Segment = GetSingleSegment(Sources[Index]);
		Sequences[Index] = Segment ? Cast<UAnimSequence>(Segment->GetAnimReference()) : nullptr;
		if (!Sequences[Index] || !FMath::IsNearlyEqual(Segment->AnimEndTime, Sequences[Index]->GetPlayLength()) ||
			!FMath::IsNearlyEqual(Sequences[Index]->RateScale, 1.0f) ||
			Schema->GetSkeleton(Roles[Index]) != Sources[Index]->GetSkeleton() ||
			Sources[Index]->GetSkeleton() != Sequences[Index]->GetSkeleton()) return FString::Printf(TEXT("Unsupported montage layout or role skeleton: %s"), *Roles[Index].ToString());
		// The live eight weapon pairs have no notifies. New content must author its own policy, not erase it here.
		if (!Sources[Index]->Notifies.IsEmpty() || !Sequences[Index]->Notifies.IsEmpty()) return TEXT("Source contains notifies; explicit conversion is required");
		if (!Tracks[Index]->MeshToScene.GetScale3D().Equals(FVector::OneVector) || Tracks[Index]->MeshToScene.ContainsNaN()) return TEXT("Invalid origin transform");
	}
	const IAnimationDataModel* VictimData = Sequences[1]->GetDataModel();
	if (!VictimData) return TEXT("Victim source animation has no editable data model");
	const float Frame = VictimData->GetFrameRate().AsInterval();
	const float ReleaseTime = Sources[1]->GetPlayLength() - Frame;
	const float WarpEnd = Config->AlignBlendTime;
	if (!FMath::IsFinite(Frame) || Frame <= 0.0f || WarpEnd <= Frame || ReleaseTime <= WarpEnd ||
		ReleaseTime >= Sources[0]->GetPlayLength()) return TEXT("Invalid alignment/release timing");

	// Use the final authored sample before playback ends. This is weapon-content adaptation, not a GASP timestamp.
	// Disable the victim's automatic blend so that this release receives the authored pose, not an idle blend.
	UAnimMontage* Montages[2];
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UAnimSequence* Sequence = DuplicateGenerated(Sequences[Index], DestinationPath, TEXT("AS_") + Roles[Index].ToString());
		Montages[Index] = DuplicateGenerated(Sources[Index], DestinationPath, TEXT("AM_") + Roles[Index].ToString());
		if (!Sequence || !Montages[Index]) return TEXT("Could not duplicate generated animation assets");
		Sequence->Modify();
		Sequence->bEnableRootMotion = true;
		Sequence->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;
		Sequence->PostEditChange();
		Sequence->MarkPackageDirty();
		UAnimMontage* Montage = Montages[Index];
		Montage->Modify();
		Montage->SlotAnimTracks = Sources[Index]->SlotAnimTracks;
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].SetAnimReference(Sequence);
		// UE 5.8 duplication may populate the composite model before slot tracks.
		// PostEditChange does not recalculate this cached length after replacing them.
		Montage->SetCompositeLength(Montage->CalculateSequenceLength());
		Montage->Notifies.Reset();
		if (Montage->AnimNotifyTracks.IsEmpty()) Montage->InitializeNotifyTrack();
		AddWarp(Montage, WarpEnd);
		if (Index == 1)
		{
			Montage->bEnableAutoBlendOut = false;
			AddRelease(Montage, ReleaseTime);
		}
		Montage->RefreshCacheData();
		Montage->PostEditChange();
		Montage->MarkPackageDirty();
	}
	UPoseSearchInteractionAsset* Interaction = FindOrCreateAsset<UPoseSearchInteractionAsset>(DestinationPath / TEXT("PSIA_Interaction"));
	UPoseSearchDatabase* Database = FindOrCreateAsset<UPoseSearchDatabase>(DestinationPath / TEXT("PSD_Interaction"));
	if (!Interaction || !Database) return TEXT("Generated package has incompatible asset class");
	Interaction->Modify();
	Interaction->Items.Reset();
	// Keep the paired clips rigidly aligned; translation stays anchored to the attacker.
	Interaction->MinimalTranslationWeight = 0.0f;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FPoseSearchInteractionAssetItem& Item = Interaction->Items.AddDefaulted_GetRef();
		Item.Role = Roles[Index];
		Item.Animation = Montages[Index];
		Item.Origin = Tracks[Index]->MeshToScene;
		// Anchor position only. GASP uses zero rotation weights so the engine derives
		// orientation from the pair's positions. A sole positive rotation weight leaves
		// FindReferenceOrientation without any weighted reference points in UE 5.8.
		Item.WarpingWeightTranslation = Index == 0 ? 1.0f : 0.0f;
		Item.WarpingWeightRotation = 0.0f;
	}
	Interaction->PostEditChange();
	Interaction->MarkPackageDirty();
	Database->Modify();
	Database->Schema = Schema;
	// Each paired clip intentionally contributes only its first pose. PCA has no
	// variance with one sample; exhaustive search is exact and trivial at this size.
	Database->PoseSearchMode = EPoseSearchMode::BruteForce;
	Database->ExcludeFromDatabaseParameters = FFloatInterval(0.0f, 0.0f);
	while (Database->GetNumAnimationAssets() > 0) Database->RemoveAnimationAssetAt(Database->GetNumAnimationAssets() - 1);
	FPoseSearchDatabaseAnimationAsset Entry;
	Entry.AnimAsset = Interaction;
	// Paired weapon clips contain no authored skip/approach range. Search only the first sample, before alignment ends.
	Entry.SamplingRange = FFloatInterval(0.0f, FMath::Min(Frame * 0.5f, WarpEnd * 0.5f));
	Entry.MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
	Database->AddAnimationAsset(Entry);
	Database->PostEditChange();
	Database->MarkPackageDirty();
	UE_LOG(LogVBWeaponFinisherAuthoring, Display, TEXT("Weapon MMI authored %s: align %.4fs release %.4fs/%0.4fs, victim origin %s"),
		*Config->GetName(), WarpEnd, ReleaseTime, Sources[1]->GetPlayLength(), *Tracks[1]->MeshToScene.ToHumanReadableString());
	return FString();
}

bool UVBWeaponFinisherAuthoring::NormalizeWeaponDatabases(const TArray<UPoseSearchDatabase*>& Databases, const FString& PackagePath)
{
	if (Databases.IsEmpty() || !PackagePath.StartsWith(GeneratedRoot) || !FPackageName::IsValidLongPackageName(PackagePath)) return false;
	for (const UPoseSearchDatabase* Database : Databases) if (!Database || !Database->GetPathName().StartsWith(GeneratedRoot)) return false;
	UPoseSearchNormalizationSet* Set = FindOrCreateAsset<UPoseSearchNormalizationSet>(PackagePath);
	if (!Set) return false;
	Set->Modify();
	Set->Databases.Reset();
	for (UPoseSearchDatabase* Database : Databases)
	{
		Set->Databases.Add(Database);
		Database->Modify();
		Database->NormalizationSet = Set;
		Database->MarkPackageDirty();
	}
	Set->PostEditChange();
	Set->MarkPackageDirty();
	return true;
}

FString UVBWeaponFinisherAuthoring::CheckWeaponDatabase(UPoseSearchDatabase* Database, bool bStartBuild)
{
	if (!Database || !Database->Schema || Database->GetNumAnimationAssets() != 1) return TEXT("ERROR: missing database/schema/pair");
	const FPoseSearchDatabaseAnimationAsset* Entry = Database->GetDatabaseAnimationAsset(0);
	const UPoseSearchInteractionAsset* Pair = Entry ? Cast<UPoseSearchInteractionAsset>(Entry->GetAnimationAsset()) : nullptr;
	if (!Pair || Pair->Items.Num() != 2 || !Database->NormalizationSet) return TEXT("ERROR: missing pair/normalization set");
	float RotationWeightSum = 0.0f;
	float MaxRotationWeight = 0.0f;
	for (const FPoseSearchInteractionAssetItem& Item : Pair->Items)
	{
		if (!FMath::IsFinite(Item.WarpingWeightRotation) || Item.WarpingWeightRotation < 0.0f || Item.WarpingWeightRotation > 1.0f)
			return TEXT("ERROR: invalid rotation weight");
		RotationWeightSum += Item.WarpingWeightRotation;
		MaxRotationWeight = FMath::Max(MaxRotationWeight, Item.WarpingWeightRotation);
	}
	// All-zero weights use the engine's homogeneous fallback and are valid.
	// Otherwise its orientation reference excludes the highest-weight role.
	if (Pair->MinimalTranslationWeight < 1.0f && RotationWeightSum > UE_KINDA_SMALL_NUMBER &&
		(RotationWeightSum - MaxRotationWeight) / RotationWeightSum <= UE_SMALL_NUMBER)
		return TEXT("ERROR: rotation reference has zero weight after excluding the anchor role");
	for (const FName Role : {AttackerRole, VictimRole})
	{
		const UAnimMontage* Montage = Cast<UAnimMontage>(Pair->GetAnimationAsset(Role));
		if (!Montage || !Montage->HasRootMotion() || Database->Schema->GetSkeleton(Role) != Montage->GetSkeleton()) return TEXT("ERROR: role skeleton/root motion mismatch");
		const FAnimNotifyEvent* Warp = Montage->Notifies.FindByPredicate([](const FAnimNotifyEvent& Event)
		{
			return Cast<UVBAnimNotifyState_InteractionWarp>(Event.NotifyStateClass) != nullptr;
		});
		if (!Warp || Warp->GetEndTriggerTime() <= Entry->SamplingRange.Max) return TEXT("ERROR: search can start after alignment ends");
		if (CastChecked<UVBAnimNotifyState_InteractionWarp>(Warp->NotifyStateClass)->AlignmentAnchorRole != AttackerRole)
			return TEXT("ERROR: weapon alignment must preserve the Attacker origin");
		if (Role == VictimRole)
		{
			const FAnimNotifyEvent* Release = Montage->Notifies.FindByPredicate([](const FAnimNotifyEvent& Event)
			{
				return Cast<UVBAnimNotify_InteractionRagdoll>(Event.Notify) != nullptr;
			});
			if (!Release || Montage->bEnableAutoBlendOut || Release->GetTriggerTime() <= Warp->GetEndTriggerTime() ||
				Release->GetTriggerTime() >= Montage->GetPlayLength()) return TEXT("ERROR: invalid victim release window");
		}
	}
	using namespace UE::PoseSearch;
	const EAsyncBuildIndexResult Status = FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
		bStartBuild ? ERequestAsyncBuildFlag::NewRequest : ERequestAsyncBuildFlag::ContinueRequest);
	if (Status == EAsyncBuildIndexResult::InProgress) return TEXT("PENDING");
	if (Status != EAsyncBuildIndexResult::Success) return TEXT("ERROR: index build failed");
	const int32 Poses = Database->GetSearchIndex().GetNumPoses();
	return Poses > 0 ? FString::Printf(TEXT("OK: %d poses"), Poses) : TEXT("ERROR: empty search index");
}
