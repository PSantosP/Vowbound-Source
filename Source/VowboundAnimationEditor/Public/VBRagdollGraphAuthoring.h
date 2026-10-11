#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBRagdollGraphAuthoring.generated.h"

class UAnimBlueprint;
class UChooserTable;
class UAnimMontage;
class UPoseSearchSchema;
class UPoseSearchDatabase;

/** 중첩 state machine의 원본 노드/핀/프로퍼티를 확인하기 위한 읽기 전용 도구. */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBRagdollGraphAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static FString DescribeRagdollGraphs(UAnimBlueprint* Blueprint);

	// 저작된 내부 state machine 전체를 복사하고 native RagdollState 입력에 연결한다.
	// 컴파일/저장은 호출자가 검증 후 수행한다. 실패하면 기존 최종 포즈 연결을 복구한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool InstallRagdollProducer(UAnimBlueprint* Blueprint, UAnimBlueprint* ReferenceBlueprint);

	// 초기 설치본의 생성 노드에 빠진 editor lifecycle을 복구한다. 컴파일/저장은 호출자 소유다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static int32 RepairGeneratedNodeBindings(UAnimBlueprint* Blueprint);

	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool InstallGASPRecovery(UAnimBlueprint* Blueprint, UAnimBlueprint* ReferenceBlueprint);

	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool ConfigureGetUpChooser(UChooserTable* Chooser, const TArray<UAnimMontage*>& Montages, UPoseSearchSchema* Schema);

	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static FString BuildAndDescribePoseSearchDatabase(UPoseSearchDatabase* Database, bool bStartBuild);
};
