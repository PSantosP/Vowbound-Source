#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VBTargetable.generated.h"

UINTERFACE(MinimalAPI)
class UVBTargetable : public UInterface
{
	GENERATED_BODY()
};

class IVBTargetable
{
	GENERATED_BODY()

public:
	// 이 액터가 현재 타겟팅 가능한 상태인가?
	virtual bool IsTargetable() const = 0;
	virtual FVector GetTargetMarkerLocation() const = 0;
};
