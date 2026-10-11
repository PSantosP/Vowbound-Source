#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/AnimNodeReference.h"
#include "BoneControllers/AnimNode_OffsetRootBone.h"
#include "VBGASPBridgeLibrary.generated.h"

class ACharacter;
class UAnimInstance;

/** Adapter at the imported Blueprint boundary; gameplay remains owned by Vowbound. */
UCLASS()
class VOWBOUND_API UVBGASPBridgeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// The wildcard is connected to the original GASP return struct by the editor authoring tool.
	UFUNCTION(BlueprintCallable, CustomThunk, Category="Vowbound|GASP", meta=(CustomStructureParam="Properties"))
	static void ReadCharacterProperties(ACharacter* Character, int32& Properties);
	DECLARE_FUNCTION(execReadCharacterProperties);

	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static void ApplyWeaponDatabases(UAnimInstance* LinkedInstance, const FAnimNodeReference& Node);

	UFUNCTION(BlueprintPure, Category="Vowbound|GASP", meta=(BlueprintThreadSafe))
	static EOffsetRootBoneMode ConstrainRootOffsetMode(UAnimInstance* LinkedInstance, EOffsetRootBoneMode Mode);

	static bool FillCharacterProperties(ACharacter* Character, UScriptStruct* Struct, void* Output);
	/** Capture gameplay-owned values before worker animation evaluation. */
	static void CaptureCMCSelection(UAnimInstance* LinkedInstance);
};
