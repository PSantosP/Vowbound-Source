#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBGASPNotifyAuthoring.generated.h"

class UAnimSequenceBase;
class UAnimSequence;
class USoundBase;
class USoundAttenuation;
class UVBGASPFoleyBank;

USTRUCT(BlueprintType)
struct FVBGASPNotifyMigrationEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Index = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) FString Kind;
	UPROPERTY(BlueprintReadOnly) FString ClassPath;
	UPROPERTY(BlueprintReadOnly) bool bNative = false;
	/** The complete event data with only notify object references cleared, for before/after equality. */
	UPROPERTY(BlueprintReadOnly) FString EventData;
	UPROPERTY(BlueprintReadOnly) FString NotifyData;
};

/** Explicit editor operations. Nothing here saves packages or modifies the external GASP project. */
UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBGASPNotifyAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static TArray<FVBGASPNotifyMigrationEntry> InspectNotifies(UAnimSequenceBase* Animation);

	/** Replaces only six known GASP notify families. Returns -1 if any contract cannot be copied. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static int32 ReplaceNotifies(UAnimSequenceBase* Animation, UVBGASPFoleyBank* Bank);

	/** Adds one phase-aligned foot contact to an Armed locomotion sequence. Safe to call repeatedly. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool AddFoleyStep(UAnimSequence* Animation, UVBGASPFoleyBank* Bank, float Time,
		bool bLeftFoot, bool bRun);

	/** All inputs are validated before replacing the bank map. */
	UFUNCTION(BlueprintCallable, Category="Vowbound|GASP|Editor")
	static bool ConfigureFoleyBank(UVBGASPFoleyBank* Bank, const TArray<FName>& Tags,
		const TArray<USoundBase*>& Sounds, USoundAttenuation* Attenuation);

	// 캐릭터 BP 의 옛 Foley 컴포넌트에 뱅크를 꽂던 AssignFoleyBank · GetAssignedFoleyBank · HasNativeFoleyComponent 는
	//   그 컴포넌트와 함께 뗐다(2026-09-30, design .sw/context-effects/design.md §10). 발소리 표는 ContextEffectComponent.Libraries 에 꽂는다.
};
