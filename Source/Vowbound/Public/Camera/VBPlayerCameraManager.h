#pragma once
#include "CoreMinimal.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/VBEnvironmentCameraSolver.h"
#include "VBPlayerCameraManager.generated.h"

class UVBEnvironmentCameraComponent;

/** Applies environment safety after GameplayCameras and ordinary camera modifiers. */
UCLASS()
class VOWBOUND_API AVBPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()
protected:
	virtual void DoUpdateCamera(float DeltaTime) override;
private:
	FVBEnvironmentCameraSolver EnvironmentSolver;
	TWeakObjectPtr<UVBEnvironmentCameraComponent> LastComponent;
};
