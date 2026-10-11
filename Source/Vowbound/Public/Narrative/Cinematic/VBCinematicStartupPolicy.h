#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBCinematicStartupPolicy.generated.h"

class APlayerController;
class UVBCinematicDefinition;
class UVBCinematicDirectorSubsystem;
class FVBStartupLease;
class FVBCinematicGameLease;

UCLASS()
class VOWBOUND_API UVBCinematicStartupPolicy : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	// true는 초기화 소유를 받았다는 뜻이다. 영상/복귀 성공은 Director의 영수증으로 본다.
	bool BeginNewGameCinematic(APlayerController* Controller, UVBCinematicDefinition* Definition, FGuid AttemptId);

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	TSharedPtr<FVBCinematicGameLease> AcquireForController(APlayerController* Controller, FGuid AttemptId);
	bool TickStartup(float DeltaTime);
	void Stop();
	UPROPERTY(Transient)
	TObjectPtr<UVBCinematicDefinition> PendingDefinition;
	TWeakObjectPtr<UVBCinematicDirectorSubsystem> Director;
	TSharedPtr<FVBStartupLease> StartupLease;
	TSet<FGuid> StartedAttempts;
	FTSTicker::FDelegateHandle TickerHandle;
	bool bRequested = false;
	bool bStopping = true;
};
