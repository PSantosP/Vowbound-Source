#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPCMCGraphAuthoring.generated.h"

class UAnimBlueprint;
class UMirrorDataTable;

/** Editor-only installation of the native CMC playback contract. */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBGASPCMCGraphAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Empty on success; caller must compile/read back before saving. Does not compile or save. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static FString InstallCMCBlendStackBindings(UAnimBlueprint* Base, UMirrorDataTable* MirrorTable);
};
