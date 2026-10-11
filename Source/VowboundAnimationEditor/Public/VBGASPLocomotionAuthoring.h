#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPLocomotionAuthoring.generated.h"

class UAnimBlueprint;
class UInputAction;
class UInputMappingContext;
class UChooserTable;
class UAnimationAsset;
class UPoseSearchDatabase;

UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBGASPLocomotionAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// Empty string means success. The installer saves only after all contracts pass.
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static FString BuildCMCAnimationChooser(UChooserTable* Source, UChooserTable* Destination, UClass* ContextClass, UChooserTable* PoseMatchReference, UAnimationAsset* WalkSlope, UAnimationAsset* RunSlope, UPoseSearchDatabase* TransitionReference);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static FString BuildWeaponAnimationChooser(UChooserTable* MasterChooser, UChooserTable* Destination, UClass* ContextClass);

	// Empty string means success, including after reload. Optional index check can return "index pending".
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static FString ValidateCMCAnimationChooser(UChooserTable* Root, bool bValidateSearchIndex = false);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static FString BuildCMCCompatibilityDatabaseChooser(UChooserTable* Source, UChooserTable* Destination, UClass* ContextClass);

	// The Python installer creates inherited overrides first. Only the selected
	// project override is replaced; the original GASP class/context stays intact.
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool InstallNativeOverride(UAnimBlueprint* Blueprint, FName FunctionName, TSubclassOf<UBlueprintFunctionLibrary> Library, UObject* ConstantObject = nullptr);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static TArray<FName> GetNativeFunctions(TSubclassOf<UBlueprintFunctionLibrary> Library);

	// Compile before saving: thumbnail evaluation requires the Slot mixer subsystem.
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool CompileAndValidateMixer(UAnimBlueprint* Blueprint);

	UFUNCTION(BlueprintPure, Category="Vowbound|GASP|Editor")
	static FString DescribeEnum(UEnum* Enum);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool MapUnassignedInput(UInputMappingContext* Context, UInputAction* Action, FName KeyName);
};
