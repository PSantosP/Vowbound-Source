#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPGraphAuthoring.generated.h"

class UBlueprint;
class UAnimBlueprint;

/** Editor-only authoring for the imported GASP contract. Never starts a world. */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBGASPGraphAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static FString DescribeGraph(UBlueprint* Blueprint, FName GraphName);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static FString DescribeStruct(UScriptStruct* Struct);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool InstallCharacterInterface(UBlueprint* Blueprint, UBlueprint* Interface);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool AppendWeaponDatabaseOverride(UAnimBlueprint* Blueprint);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool InstallLinkedLocomotion(UAnimBlueprint* Blueprint, UAnimBlueprint* Locomotion);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool InstallRootOffsetOverride(UAnimBlueprint* Blueprint, FName FunctionName);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool InstallAnimationInterfaceForwarding(UAnimBlueprint* Blueprint, UBlueprint* Interface);
};
