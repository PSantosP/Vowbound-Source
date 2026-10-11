#pragma once
#include "CoreMinimal.h"
class UWorld;
class APlayerController;
class APawn;
class UVBPlayerReadinessComponent;

// 준비 소비자의 현재 첫 플레이어 구독. 알림은 승인 값이 아니라 재조회 요청이다.
class VOWBOUND_API FVBPlayerPreparationBinding
{
public:
 FVBPlayerPreparationBinding() = default;
 ~FVBPlayerPreparationBinding();
 FVBPlayerPreparationBinding(const FVBPlayerPreparationBinding&) = delete;
 FVBPlayerPreparationBinding& operator=(const FVBPlayerPreparationBinding&) = delete;
 void Refresh(UObject* Owner, UWorld* World, FSimpleDelegate Requery);
 void Disconnect();
 APawn* GetPawn() const;
 APlayerController* GetController() const;
private:
 TWeakObjectPtr<UObject> BoundOwner;
 TWeakObjectPtr<UWorld> BoundWorld;
 TWeakObjectPtr<APlayerController> Controller;
 TWeakObjectPtr<APawn> Pawn;
 TWeakObjectPtr<UVBPlayerReadinessComponent> Preparation;
 FSimpleDelegate RequestRequery;
 FDelegateHandle SpawnHandle, DestroyHandle, PawnHandle, PreparationHandle;
};
