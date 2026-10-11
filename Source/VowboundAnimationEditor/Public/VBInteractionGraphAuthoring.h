#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBInteractionGraphAuthoring.generated.h"

class UAnimBlueprint;
class UAnimSequenceBase;
class USkeletalMesh;

// 원본 그래프의 저작된 FullBodyIK 분기를 복사한다. 저장과 컴파일은 호출자가 검증 후 수행한다.
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBInteractionGraphAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool InstallInteractionRig(UAnimBlueprint* Blueprint, UAnimBlueprint* ReferenceBlueprint,
	                                  FName TargetNodeName);

	// 이벤트 시각·트랙·발화 조건을 보존하고 실행 클래스만 native 접점으로 바꾼다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static int32 ReplaceInteractionRagdollNotifies(UAnimSequenceBase* Animation);

	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Editor")
	static TArray<float> GetInteractionRagdollNotifyTimes(UAnimSequenceBase* Animation);

	UFUNCTION(BlueprintPure, Category="Vowbound|Animation|Editor")
	static TArray<FName> GetPhysicsBodyNames(USkeletalMesh* Mesh);

	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool EnsureFinalInertialization(UAnimBlueprint* Blueprint);
};
