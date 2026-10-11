#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBLookAtPOIAuthoring.generated.h"
class UAnimBlueprint;
class UDataAsset;
class USkeleton;

UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBLookAtPOIAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static UAnimBlueprint* CreateLookAtLayer(UAnimBlueprint* Reference, UDataAsset* Settings, const FString& PackagePath);
	/** Configure a project-owned copy for mannequins without an animated poi bone. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static bool ConfigureLookAtLayerForSkeleton(UAnimBlueprint* Layer, USkeleton* Skeleton, UDataAsset* Settings, FVector BaselineOffset, const TArray<FName>& DrivenBones);
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static bool ConnectLookAtLayer(UAnimBlueprint* Target, UAnimBlueprint* Layer, bool bRequirePoseHistory = true);
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static FString InspectLookAtLayer(UAnimBlueprint* Target, UAnimBlueprint* Layer, bool bRequirePoseHistory = true);
};
