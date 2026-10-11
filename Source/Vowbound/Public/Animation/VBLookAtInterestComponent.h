#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBLookAtInterestComponent.generated.h"

class UVBLookAtInterestComponent;

/** World-local, weak registry: streamed-out interests must not retain their actors. */
UCLASS()
class VOWBOUND_API UVBLookAtInterestSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	void Register(UVBLookAtInterestComponent* Interest);
	void Unregister(UVBLookAtInterestComponent* Interest);
	const TArray<TWeakObjectPtr<UVBLookAtInterestComponent>>& GetInterests();
private:
	TArray<TWeakObjectPtr<UVBLookAtInterestComponent>> Interests;
};

/** Opt-in visual interest. Position is this component, or the owner's character socket. */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBLookAtInterestComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bInterestEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") FName TargetSocket = NAME_None;
	FVector GetInterestLocation() const;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
