#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBEnvironmentCameraAuthoring.generated.h"
class UCameraAsset;
class UVBEnvironmentCameraConfig;

UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBEnvironmentCameraAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Read reflected defaults, including native camera nodes not exposed to Python. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Camera|Editor")
	static FString InspectCameraObject(UObject* Asset);
	/** Only accepts project-owned duplicates. Does not save packages. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Camera|Editor")
	static bool WireEnvironmentCamera(UCameraAsset* Camera, UBlueprint* Director, UBlueprint* Character,
		UVBEnvironmentCameraConfig* Config);
};
