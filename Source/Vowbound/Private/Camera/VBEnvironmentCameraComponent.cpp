#include "Camera/VBEnvironmentCameraComponent.h"
#include "Camera/VBPlayerCameraManager.h"
#include "Data/VBEnvironmentCameraConfig.h"
#include "Core/CameraAsset.h"
#include "Directors/BlueprintCameraDirector.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

UVBEnvironmentCameraComponent::UVBEnvironmentCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UVBEnvironmentCameraComponent::Prepare(UGameplayCameraComponentBase* Camera)
{
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	APlayerController* PC = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	if (!Camera || !Config || !Config->bEnabled || !PC || !PC->IsLocalController() ||
		GetNetMode() == NM_DedicatedServer || !GetWorld() || !GetWorld()->IsGameWorld()) return false;

	auto* GameplayCamera = Cast<UGameplayCameraComponent>(Camera);
	if (!GameplayCamera || !PC->PlayerCameraManager || !PC->PlayerCameraManager->IsA<AVBPlayerCameraManager>()) return false;
	ActiveCamera = Camera;
	ActiveCameraAsset = GameplayCamera->CameraReference.GetCameraAsset();
	return true;
}

void UVBEnvironmentCameraComponent::ActivateEnvironmentCollision(UObject* EvaluationContextOwner,
	UBlueprintCameraDirectorEvaluator* Director, UCameraRigAsset* FallbackRig)
{
	if (!Director) return;
	auto* Camera = Cast<UGameplayCameraComponentBase>(EvaluationContextOwner);
	auto* Component = Camera && Camera->GetOwner() ?
		Camera->GetOwner()->FindComponentByClass<UVBEnvironmentCameraComponent>() : nullptr;
	if (!Component || !Component->Prepare(Camera))
	{
		if (Component) Component->ReleaseCollisionOwnership();
		if (FallbackRig) Director->ActivatePersistentGlobalCameraRig(FallbackRig);
		return;
	}
	// Also clears a persistent instance left by a previous fallback activation, if present.
	if (FallbackRig) Director->DeactivatePersistentGlobalCameraRig(FallbackRig);
}

bool UVBEnvironmentCameraComponent::OwnsCollision() const
{
	const auto* Camera = Cast<UGameplayCameraComponent>(ActiveCamera.Get());
	return Camera && Camera->IsActive() && Camera->HasEvaluationContext() && ActiveCameraAsset.IsValid() &&
		Camera->CameraReference.GetCameraAsset() == ActiveCameraAsset.Get();
}

void UVBEnvironmentCameraComponent::ReleaseCollisionOwnership()
{
	ActiveCamera.Reset();
	ActiveCameraAsset.Reset();
}

void UVBEnvironmentCameraComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	ReleaseCollisionOwnership();
	Super::EndPlay(Reason);
}
