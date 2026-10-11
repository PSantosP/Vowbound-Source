#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPSmartObjectAuthoring.generated.h"
class UBlueprint;
class UPoseSearchDatabase;
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBGASPSmartObjectAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Duplicates original authoring assets; replaces executable BP nodes/contexts before compiling. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static FString BuildSmartObjectAssets(UBlueprint* CharacterReference, const FString& DestinationPath);
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static FString CheckSmartObjectSearchIndex(const FString& DestinationPath, bool bStartBuild);
	/** Assign only after all copied StateTrees compile and the native distance index builds. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static bool ConnectSmartObjectCharacter(UBlueprint* CharacterBlueprint, const FString& DestinationPath);
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static FString InspectSmartObjectAssets(const FString& DestinationPath);
	/** Copies and compiles the four source trees in a transient package; does not save or run gameplay. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static FString ValidateSmartObjectStateTreeCopies();
};
