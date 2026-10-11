#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBSkeletonMigrationRepair.generated.h"

class USkeleton;
class USkeletalMesh;

/** 기존 골격을 유지하며 원본에서 확인한 누락 BlendProfile만 추가하는 에디터 도구. */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBSkeletonMigrationRepair : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool RestoreRagdollTraceSocket(USkeletalMesh* Mesh);
	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Editor")
	static TArray<FName> GetBlendProfileNames(USkeleton* Skeleton);
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool CopyMissingBlendProfile(USkeleton* Target, USkeleton* Source, FName ProfileName);

	/** ModeName: TimeFactor, WeightFactor, BlendMask. 기존 동일 이름의 값은 덮어쓰지 않는다. 저장은 호출자 몫. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool AddMissingBlendProfile(USkeleton* Skeleton, FName ProfileName, FName ModeName,
	                                  const TArray<FName>& BoneNames, const TArray<float>& Scales);
};
