#include "VBSkeletonMigrationRepair.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"

bool UVBSkeletonMigrationRepair::RestoreRagdollTraceSocket(USkeletalMesh* Mesh)
{
	if (!Mesh || !Mesh->GetSkeleton() || Mesh->GetRefSkeleton().FindBoneIndex(TEXT("spine_03")) == INDEX_NONE) return false;
	if (const USkeletalMeshSocket* Existing = Mesh->FindSocket(TEXT("RagdollTrace")))
	{
		return Existing->BoneName == TEXT("spine_03") && Existing->RelativeLocation.Equals(FVector(20, 0, 0)) &&
			Existing->RelativeRotation.Equals(FRotator(0, 90, -90));
	}
	USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Mesh, NAME_None, RF_Transactional);
	Socket->SocketName = TEXT("RagdollTrace");
	Socket->BoneName = TEXT("spine_03");
	Socket->RelativeLocation = FVector(20, 0, 0);
	Socket->RelativeRotation = FRotator(0, 90, -90);
	Mesh->AddSocket(Socket, true);
	return Mesh->FindSocket(TEXT("RagdollTrace")) != nullptr;
}

TArray<FName> UVBSkeletonMigrationRepair::GetBlendProfileNames(USkeleton* Skeleton)
{
	TArray<FName> Names;
	if (Skeleton)
	{
		for (const UBlendProfile* Profile : Skeleton->BlendProfiles)
		{
			if (Profile)
			{
				Names.Add(Profile->GetFName());
			}
		}
	}
	return Names;
}

bool UVBSkeletonMigrationRepair::CopyMissingBlendProfile(USkeleton* Target, USkeleton* Source, FName ProfileName)
{
	const UBlendProfile* Profile = Source ? Source->GetBlendProfile(ProfileName) : nullptr;
	if (!Target || !Profile) return false;
	if (Target->GetBlendProfile(ProfileName)) return true; // Preserve existing authored profiles.
	TArray<FName> Bones;
	TArray<float> Scales;
	for (int32 Index = 0; Index < Profile->GetNumBlendEntries(); ++Index)
	{
		const FBlendProfileBoneEntry& Entry = Profile->GetEntry(Index);
		Bones.Add(Entry.BoneReference.BoneName);
		Scales.Add(Entry.BlendScale);
	}
	const FName Mode = Profile->GetMode() == EBlendProfileMode::TimeFactor ? TEXT("TimeFactor") :
		Profile->GetMode() == EBlendProfileMode::WeightFactor ? TEXT("WeightFactor") : TEXT("BlendMask");
	return AddMissingBlendProfile(Target, ProfileName, Mode, Bones, Scales);
}

bool UVBSkeletonMigrationRepair::AddMissingBlendProfile(USkeleton* Skeleton, FName ProfileName,
                                                       FName ModeName, const TArray<FName>& BoneNames,
                                                       const TArray<float>& Scales)
{
	if (!Skeleton || ProfileName.IsNone() || BoneNames.Num() != Scales.Num())
	{
		return false;
	}
	EBlendProfileMode Mode;
	if (ModeName == TEXT("TimeFactor")) Mode = EBlendProfileMode::TimeFactor;
	else if (ModeName == TEXT("WeightFactor")) Mode = EBlendProfileMode::WeightFactor;
	else if (ModeName == TEXT("BlendMask")) Mode = EBlendProfileMode::BlendMask;
	else return false;

	TSet<FName> SeenBones;
	for (int32 Index = 0; Index < BoneNames.Num(); ++Index)
	{
		if (SeenBones.Contains(BoneNames[Index]) ||
		    Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneNames[Index]) == INDEX_NONE ||
		    !FMath::IsFinite(Scales[Index]) || Scales[Index] < 0.0f)
		{
			return false;
		}
		SeenBones.Add(BoneNames[Index]);
	}
	if (const UBlendProfile* Existing = Skeleton->GetBlendProfile(ProfileName))
	{
		// 반복 실행은 동일 자료면 성공한다. 다른 기존 프로파일은 수정하지 않는다.
		if (Existing->GetMode() != Mode || Existing->GetNumBlendEntries() != BoneNames.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < BoneNames.Num(); ++Index)
		{
			const FBlendProfileBoneEntry& Entry = Existing->GetEntry(Index);
			if (Entry.BoneReference.BoneName != BoneNames[Index] || Entry.BlendScale != Scales[Index])
			{
				return false;
			}
		}
		return true;
	}
	// 같은 이름의 미등록 서브오브젝트도 덮어쓰지 않는다.
	if (FindObject<UObject>(Skeleton, *ProfileName.ToString()))
	{
		return false;
	}
	Skeleton->Modify();
	UBlendProfile* Profile = Skeleton->CreateNewBlendProfile(ProfileName);
	Profile->Modify();
	Profile->Mode = Mode;
	for (int32 Index = 0; Index < BoneNames.Num(); ++Index)
	{
		FBlendProfileBoneEntry& Entry = Profile->ProfileEntries.AddDefaulted_GetRef();
		Entry.BoneReference.BoneName = BoneNames[Index];
		Entry.BlendScale = Scales[Index];
	}
	// 원본 뼈 이름으로 대상 골격의 인덱스를 재구성한다. 기존 골격/가상 뼈/소켓은 건드리지 않는다.
	Profile->SetSkeleton(Skeleton);
	Skeleton->MarkPackageDirty();
	return Profile->GetNumBlendEntries() == BoneNames.Num();
}
