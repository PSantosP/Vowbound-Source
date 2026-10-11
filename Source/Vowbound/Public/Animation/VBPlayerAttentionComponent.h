#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBPlayerAttentionComponent.generated.h"

class UVBLookAtInterestComponent;

UENUM(BlueprintType)
enum class EVBPlayerAttentionSource : uint8 { None, Conversation, Interaction, Interest };

USTRUCT()
struct FVBPlayerAttentionTarget
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AActor> Actor = nullptr;
	UPROPERTY() FVector LocalPoint = FVector::ZeroVector;
	UPROPERTY() FName Socket = NAME_None;
	UPROPERTY() EVBPlayerAttentionSource Source = EVBPlayerAttentionSource::None;
};

/** Server chooses visual attention; the existing LookAt layer consumes it on every machine. */
UCLASS(ClassGroup=(Animation), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBPlayerAttentionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBPlayerAttentionComponent();
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Look At") bool SetConversationTarget(AActor* Actor);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Look At") void ClearConversationTarget(AActor* ExpectedActor);
	UFUNCTION(BlueprintPure, Category="Look At") AActor* GetSelectedActor() const { return Selected.Actor; }
	UFUNCTION(BlueprintPure, Category="Look At") EVBPlayerAttentionSource GetSelectedSource() const { return Selected.Source; }
	bool ResolveAttention(FVector& OutLocation, bool bConversationOnly) const;
	bool IsPoseSuppressed() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At") bool bAutomaticAttention = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float AcquireDistance = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float ReleaseDistance = 500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", ClampMax="89", Units="deg")) float AcquireHalfAngle = 55.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", ClampMax="89", Units="deg")) float ReleaseHalfAngle = 75.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float ConversationMaxDistance = 600.f;
	/** Automatic interaction gaze yields as the player reaches the object. Reach/prompt anchors stay unchanged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float InteractionGazeMinDistance = 125.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="cm")) float InteractionGazeMaxDropFromHead = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Look At", meta=(ClampMin="0", Units="s")) float InteractionGazeResumeDelay = 0.65f;
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	void UpdateAttention();
	bool IsUsableActor(const AActor* Actor) const;
	bool IsVisible(const AActor* Actor, const FVector& Location) const;
	bool IsInView(const FVector& Location, float Distance, float HalfAngle) const;
	bool IsEligibleInterest(const UVBLookAtInterestComponent* Interest, bool bRetain) const;
	void Select(AActor* Actor, const FVector& WorldPoint, FName Socket, EVBPlayerAttentionSource Source);
	UPROPERTY(Replicated) FVBPlayerAttentionTarget Selected;
	TWeakObjectPtr<AActor> Conversation;
	TWeakObjectPtr<UVBLookAtInterestComponent> CurrentInterest;
	float InteractionGazeResumeTime = 0.f;
};
