#include "Camera/VBPlayerCameraManager.h"
#include "Camera/VBEnvironmentCameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

void AVBPlayerCameraManager::DoUpdateCamera(float DeltaTime)
{
	const bool bWasBlending = PendingViewTarget.Target != nullptr;
	Super::DoUpdateCamera(DeltaTime);
	auto* Character = PCOwner ? Cast<ACharacter>(PCOwner->GetPawn()) : nullptr;
	auto* Component = Character ? Character->FindComponentByClass<UVBEnvironmentCameraComponent>() : nullptr;
	if (!PCOwner || !PCOwner->IsLocalController() || !Character || ViewTarget.Target != Character ||
		bWasBlending || PendingViewTarget.Target || !Component || !Component->OwnsCollision() || !Component->Config ||
		(CameraStyle != NAME_Default && CameraStyle != NAME_None))
	{
		EnvironmentSolver.Reset();
		LastComponent.Reset();
		return;
	}
	if (LastComponent.Get() != Component) EnvironmentSolver.Reset();
	LastComponent = Component;
	FMinimalViewInfo Final = GetCameraCacheView();
	EnvironmentSolver.Update(Character, Component->Config, DeltaTime, bGameCameraCutThisFrame, Final);
	// Engine keeps the previous-frame cache when filled a second time at the same timestamp.
	FillCameraCache(Final);
	SetActorLocationAndRotation(Final.Location, Final.Rotation);
}
