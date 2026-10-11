#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "VBEnvironmentInteractionAuthoring.generated.h"

class UAnimMontage;

UCLASS()
class VOWBOUNDANIMATIONEDITOR_API UVBEnvironmentInteractionAuthoring : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// Segments must be Grab, Idle, Push, Pull, Release, in that order.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Authoring")
	static bool ConfigurePushMontage(UAnimMontage* Montage);
};
