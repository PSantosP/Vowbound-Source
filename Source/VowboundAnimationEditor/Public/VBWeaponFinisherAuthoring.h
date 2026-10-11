#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBWeaponFinisherAuthoring.generated.h"

class UVBFinisherConfig;
class UPoseSearchSchema;
class UPoseSearchDatabase;

/** CAS의 저작된 역할/원점을 보존해 프로젝트 소유 MMI 데이터를 만든다. 저장/활성화는 호출자 책임이다. */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBWeaponFinisherAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// 성공은 빈 문자열. 실패 시 Config는 변경하지 않는다. 원본 몽타주/시퀀스도 변경하지 않는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static FString BuildWeaponFinisherAssets(UVBFinisherConfig* Config, UPoseSearchSchema* Schema, const FString& DestinationPath);

	// 여러 후보 DB를 같은 척도로 비교하도록 정규화 집합을 공유한다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static bool NormalizeWeaponDatabases(const TArray<UPoseSearchDatabase*>& Databases, const FString& PackagePath);

	// 비동기 인덱스만 요청/확인한다. 재생이나 PIE를 실행하지 않는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Animation|Editor")
	static FString CheckWeaponDatabase(UPoseSearchDatabase* Database, bool bStartBuild);
};
