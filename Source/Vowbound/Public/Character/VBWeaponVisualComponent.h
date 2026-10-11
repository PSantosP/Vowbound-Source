#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/VBWeaponVisualConfig.h"
#include "VBWeaponVisualComponent.generated.h"

class UAnimInstance;
class AVBCharacter;
class AVBSummonedWeapon;
class UVBWeaponStateComponent;

UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBWeaponVisualComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBWeaponVisualComponent();
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Weapons") TObjectPtr<UVBWeaponVisualConfig> VisualConfig;
	// 재생 성공한 인스턴스만 동기화한다. nullptr은 미재생 폴백이며 이전 전환도 취소한다.
	void BeginWeaponTransition(EVBWeaponType ToType, UAnimInstance* AnimInstance, UAnimMontage* Montage);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	void DestroyVisuals();
	void CreateVisuals(EVBWeaponType Type);
	void ApplyVisual(EVBWeaponType Type, float NewProgress);
	void ResetTransition();
	bool UpdateTransition();
	UPROPERTY(Transient) TObjectPtr<AVBCharacter> Character;
	UPROPERTY(Transient) TObjectPtr<UVBWeaponStateComponent> WeaponState;
	UPROPERTY(Transient) TArray<TObjectPtr<AVBSummonedWeapon>> Visuals;
	TWeakObjectPtr<UAnimInstance> TransitionAnimInstance;
	int32 TransitionInstanceId = INDEX_NONE;
	FVBWeaponVisualTiming TransitionTiming;
	EVBWeaponType TransitionFrom = EVBWeaponType::None;
	EVBWeaponType TransitionTo = EVBWeaponType::None;
	float TransitionStartProgress = 0.0f;
	float ReplicationGraceRemaining = 0.0f;
	EVBWeaponType VisibleType = EVBWeaponType::None;
	float Progress = 0.0f;
	float FormationDuration = 0.65f;
	float DissolveDuration = 0.25f;
};
