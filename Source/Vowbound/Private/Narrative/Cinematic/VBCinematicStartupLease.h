#pragma once

#include "EnhancedInputComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCinematicDefinition.h"

namespace VBCinematicStartup
{
	inline bool ValidateDefinition(const UVBCinematicDefinition* Definition, TArray<FText>& Errors)
	{
		if (!IsValid(Definition))
		{
			Errors.Add(FText::FromString(TEXT("시네마틱 정의가 없습니다.")));
			return false;
		}
		return Definition->ValidateSettings(Errors);
	}
}

class FVBStartupLease final : public FVBCinematicGameLease
{
public:
	FVBStartupLease(UObject* InOwner, APlayerController* InController, FGuid InAttempt)
		: Owner(InOwner), Controller(InController), Pawn(InController->GetPawn()), Attempt(InAttempt)
	{
		Readiness = Pawn->FindComponentByClass<UVBPlayerReadinessComponent>();
		Save = UVBSaveGameSubsystem::Get(Pawn.Get());
	}

	virtual ~FVBStartupLease() override { RemoveInputGuard(); }

	bool Initialize(const UVBCinematicDefinition* Definition)
	{
		TArray<FText> Errors;
		if (!VBCinematicStartup::ValidateDefinition(Definition, Errors) || !IsCurrent() || !Readiness.IsValid()) return false;
		SaveHold = Save->BeginSaveHold(TEXT("StartupCinematic"));
		if (!IsCurrent()) return false;
		InputGuard.Reset(NewObject<UEnhancedInputComponent>(Controller.Get(), NAME_None, RF_Transient));
		InputGuard->Priority = MAX_int32;
		InputGuard->bBlockInput = true;
		InputGuard->RegisterComponent();
		Controller->PushInputComponent(InputGuard.Get());
		PreparationHold = Readiness->AcquirePreparationHold(Owner.Get(), TEXT("StartupCinematic"));
		return IsCurrent() && PreparationHold.Id.IsValid();
	}

	bool Matches(APlayerController* PC, FGuid Id) const { return Controller.Get() == PC && Attempt == Id; }
	bool IsCurrent() const
	{
		const APawn* CurrentPawn = Pawn.Get();
		const APlayerController* PC = Controller.Get();
		const UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
		return IsValid(World) && !World->bIsTearingDown && IsValid(CurrentPawn) && !CurrentPawn->IsActorBeingDestroyed()
			&& IsValid(PC) && !PC->IsActorBeingDestroyed() && PC->IsLocalController() && CurrentPawn->HasAuthority()
			&& CurrentPawn->GetWorld() == World && PC->GetWorld() == World
			&& PC->GetPawn() == CurrentPawn && CurrentPawn->GetController() == PC && Save.IsValid()
			&& Save->GetGameStartContext().AttemptId == Attempt
			&& Save->GetGameStartContext().Intent == EVBGameStartIntent::NewGame
			&& Readiness.IsValid() && Readiness->Query().InitializationId == Attempt;
	}

	bool PrepareToPlay()
	{
		if (bFinished || !IsCurrent() || !InputGuard.IsValid() || !Controller->IsInputComponentInStack(InputGuard.Get())) return false;
		if (!SaveHold.IsValid()) SaveHold = Save->BeginSaveHold(TEXT("StartupCinematic"));
		if (!IsSaveHeld() || !IsCurrent()) return false;
		const FVBPlayerPreparationSnapshot Preparation = Readiness->Query();
		const uint32 ExpectedBlocks = static_cast<uint32>(EVBPlayerPreparationReason::Initialization)
			| static_cast<uint32>(EVBPlayerPreparationReason::ExternalHold);
		if (Preparation.UnavailableReasons || (Preparation.BlockedReasons & ~ExpectedBlocks)) return false;
		if (!IsValid(Controller->PlayerCameraManager) || !IsValid(Controller->PlayerInput)
			|| !IsValid(Pawn->InputComponent) || !Pawn->InputComponent->IsRegistered()) return false;
		GameplayViewTarget = Controller->GetViewTarget();
		return GameplayViewTarget.IsValid() && GameplayViewTarget->GetWorld() == Owner->GetWorld();
	}

	virtual bool IsSaveHeld() const override { return Save.IsValid() && Save->IsSaveHoldActive(SaveHold); }
	virtual void RestoreGameplay() override { bRestoreRequested = true; }
	void TickRestore()
	{
		if (bFinished || bRestored || !bRestoreRequested || !IsCurrent() || !IsSaveHeld()) return;
		UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(Controller.Get());
		const FVBUIInputAvailability Input = UI ? UI->QueryInputAvailability(Controller.Get()) : FVBUIInputAvailability{};
		if (!Input.bAvailable || Input.bBlocked || !GameplayViewTarget.IsValid()
			|| Controller->GetViewTarget() != GameplayViewTarget.Get()
			|| !Readiness->CanFinishPreparationHold(PreparationHold)) return;
		RemoveInputGuard();
		if (!IsCurrent()) return;
		bRestored = true;
		Readiness->ReleasePreparationHold(PreparationHold);
	}
	virtual bool IsGameplayRestored() const override
	{
		return bRestored && IsCurrent() && GameplayViewTarget.IsValid()
			&& Controller->GetViewTarget() == GameplayViewTarget.Get()
			&& Readiness->Query().State == EVBPlayerPreparationState::Ready;
	}
	void CompleteInitialization()
	{
		if (IsCurrent()) Readiness->CompleteInitialization(Attempt, false);
	}
	APlayerState* GetInstigator() const { return IsCurrent() ? Pawn->GetPlayerState() : nullptr; }
	virtual void Finish(bool bSucceeded) override
	{
		if (bFinished) return;
		bFinished = true;
		// 복귀를 확인한 뒤 완료 효과가 새 보류를 만들더라도 자기 저장 hold는 끝낸다.
		if (bSucceeded && bRestored && IsCurrent() && IsSaveHeld()) Save->EndSaveHold(SaveHold);
		// 실패의 GI 저장 보류는 새 게임/성공 로드만 폐기한다. 월드 종료는 입력 가드만 정리한다.
	}
	bool IsFinished() const { return bFinished; }

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBStartupLeaseTestAccess;
#endif
	void RemoveInputGuard()
	{
		if (!InputGuard.IsValid()) return;
		if (Controller.IsValid()) Controller->PopInputComponent(InputGuard.Get());
		InputGuard->DestroyComponent();
		InputGuard.Reset();
	}
	TWeakObjectPtr<UObject> Owner;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<APawn> Pawn;
	TWeakObjectPtr<AActor> GameplayViewTarget;
	TWeakObjectPtr<UVBPlayerReadinessComponent> Readiness;
	TWeakObjectPtr<UVBSaveGameSubsystem> Save;
	TStrongObjectPtr<UEnhancedInputComponent> InputGuard;
	FGuid Attempt;
	FVBSaveHoldToken SaveHold;
	FVBPlayerPreparationHoldToken PreparationHold;
	bool bRestoreRequested = false;
	bool bRestored = false;
	bool bFinished = false;
};
