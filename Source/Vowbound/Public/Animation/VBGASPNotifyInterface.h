// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h"
#include "UObject/Interface.h"
#include "VBGASPNotifyInterface.generated.h"

/** Native contract for the GASP early-transition notify; implemented by the locomotion AnimInstance. */
UINTERFACE(BlueprintType)
class VOWBOUND_API UVBGASPNotifyInterface : public UInterface
{
	GENERATED_BODY()
};

class VOWBOUND_API IVBGASPNotifyInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Vowbound|GASP|Notify")
	EVBGait GetGASPNotifyGait() const;

	/** Sets the selected transition request to true. The animation transition owns clearing it. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Vowbound|GASP|Notify")
	void RequestGASPEarlyTransition(bool bToLoop);
};
