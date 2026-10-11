#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/Character.h"
#include "Interaction/VBGASPNPC.h"
#include "LevelSequence.h"
#include "Narrative/Cinematic/VBCinematicStartupLease.h"
#include "Narrative/Cinematic/VBCinematicStartupPolicy.h"
#include "UObject/Package.h"

class FVBStartupLeaseTestAccess
{
public:
	static void BindSave(FVBStartupLease& Lease, UVBSaveGameSubsystem& Save) { Lease.Save = &Save; }
	static bool HasInput(const FVBStartupLease& Lease) { return Lease.InputGuard.IsValid(); }
	static bool AssertCurrent(FAutomationTestBase& Test, const FVBStartupLease& Lease)
	{
		const APawn* Pawn = Lease.Pawn.Get();
		const APlayerController* PC = Lease.Controller.Get();
		const UWorld* World = Lease.Owner.IsValid() ? Lease.Owner->GetWorld() : nullptr;
		const UVBSaveGameSubsystem* Save = Lease.Save.Get();
		const UVBPlayerReadinessComponent* Readiness = Lease.Readiness.Get();
		bool bValid = true;
		const auto Check = [&](const TCHAR* Name, bool bCondition)
		{
			bValid = Test.TestTrue(Name, bCondition) && bValid;
		};
		Check(TEXT("Context.OwnerValid"), Lease.Owner.IsValid());
		Check(TEXT("Context.WorldValid"), IsValid(World));
		Check(TEXT("Context.WorldNotTearingDown"), World && !World->bIsTearingDown);
		Check(TEXT("Context.PawnValid"), IsValid(Pawn));
		Check(TEXT("Context.PawnNotBeingDestroyed"), IsValid(Pawn) && !Pawn->IsActorBeingDestroyed());
		Check(TEXT("Context.ControllerValid"), IsValid(PC));
		Check(TEXT("Context.ControllerNotBeingDestroyed"), IsValid(PC) && !PC->IsActorBeingDestroyed());
		Check(TEXT("Context.LocalController"), IsValid(PC) && PC->IsLocalController());
		Check(TEXT("Context.PawnAuthority"), IsValid(Pawn) && Pawn->HasAuthority());
		Check(TEXT("Context.PawnWorldMatches"), IsValid(Pawn) && World && Pawn->GetWorld() == World);
		Check(TEXT("Context.ControllerWorldMatches"), IsValid(PC) && World && PC->GetWorld() == World);
		Check(TEXT("Context.ControllerPawnMatches"), IsValid(PC) && IsValid(Pawn) && PC->GetPawn() == Pawn);
		Check(TEXT("Context.PawnControllerMatches"), IsValid(Pawn) && IsValid(PC) && Pawn->GetController() == PC);
		Check(TEXT("Context.SaveValid"), IsValid(Save));
		Check(TEXT("Context.SaveAttemptMatches"), IsValid(Save) && Save->GetGameStartContext().AttemptId == Lease.Attempt);
		Check(TEXT("Context.SaveIntentNewGame"), IsValid(Save) && Save->GetGameStartContext().Intent == EVBGameStartIntent::NewGame);
		Check(TEXT("Context.ReadinessValid"), IsValid(Readiness));
		Check(TEXT("Context.ReadinessAttemptMatches"), IsValid(Readiness) && Readiness->Query().InitializationId == Lease.Attempt);
		return bValid && Test.TestTrue(TEXT("Lease context current after named preconditions"), Lease.IsCurrent());
	}
	static void ObserveRestore(FVBStartupLease& Lease)
	{
		// Narrow Finish contract fixture, not a claim of camera/UI/EngineLoop restoration.
		Lease.bRestored = true;
		Lease.RemoveInputGuard();
		Lease.Readiness->ReleasePreparationHold(Lease.PreparationHold);
	}
};

namespace
{
	constexpr EAutomationTestFlags StartupSafetyFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	struct FStartupSafetyFixture
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
		TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
		TStrongObjectPtr<UVBCinematicDefinition> Definition{NewObject<UVBCinematicDefinition>()};
		TStrongObjectPtr<UVBCinematicStartupPolicy> Owner;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		UWorld* World = nullptr;
		APlayerController* Controller = nullptr;
		APawn* Pawn = nullptr;
		UVBPlayerReadinessComponent* Readiness = nullptr;
		TSharedPtr<FVBStartupLease> Lease;

		~FStartupSafetyFixture()
		{
			Lease.Reset();
			if (LocalPlayer.IsValid())
			{
				APlayerController* BoundController = LocalPlayer->PlayerController;
				if (IsValid(BoundController) && BoundController->Player == LocalPlayer.Get()) BoundController->Player = nullptr;
				LocalPlayer->PlayerController = nullptr;
				// Never registered with AddLocalPlayer/PlayerAdded; release the sole fixture strong reference.
				LocalPlayer.Reset();
			}
			if (!World) return;
			const TStrongObjectPtr<UPackage> Package(World->GetPackage());
			World->DestroyWorld(false);
			Package->SetDirtyFlag(false);
		}
		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Fixture.EngineAvailable"), GEngine)) return false;
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("Owned fixture world"), World)) return false;
			World->SetGameInstance(Instance.Get());
			Controller = World->SpawnActor<APlayerController>();
			Pawn = World->SpawnActor<APawn>();
			if (!Test.TestNotNull(TEXT("Controller"), Controller) || !Test.TestNotNull(TEXT("Pawn"), Pawn)) return false;
			Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
			Controller->Possess(Pawn);
			Save->StartNewGame();
			const FGuid Attempt = Save->GetGameStartContext().AttemptId;
			Readiness = NewObject<UVBPlayerReadinessComponent>(Pawn);
			Pawn->AddInstanceComponent(Readiness);
			Readiness->RegisterComponent();
			Readiness->BeginInitialization(Attempt, EVBGameStartIntent::NewGame);
			Owner.Reset(NewObject<UVBCinematicStartupPolicy>(World));
			Definition->Sequence = TSoftObjectPtr<ULevelSequence>(FSoftObjectPath(TEXT("/Engine/Transient.UnitSequence")));
			Lease = MakeShared<FVBStartupLease>(Owner.Get(), Controller, Attempt);
			// Supply the actual isolated Save instance without initializing unrelated GI subsystems.
			FVBStartupLeaseTestAccess::BindSave(*Lease, *Save.Get());
			if (!Test.TestNull(TEXT("Fixture has no NetDriver"), World->GetNetDriver())
				|| !Test.TestFalse(TEXT("Bare controller without ULocalPlayer is not local"), Controller->IsLocalController())) return false;
			Test.TestFalse(TEXT("Lease rejects controller before local player attachment"), Lease->IsCurrent());
			// Native backlink only: no viewport, input-system or unrelated local-player subsystem initialization.
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient));
			if (!Test.TestTrue(TEXT("Fixture.LocalPlayerEngineOuter"), LocalPlayer->GetOuter() == GEngine)) return false;
			Test.TestFalse(TEXT("Fixture.LocalPlayerNotRooted"), LocalPlayer->IsRooted());
			Test.TestEqual(TEXT("Fixture.NoGameInstancePlayerRegistration"), Instance->GetNumLocalPlayers(), 0);
			Test.TestNull(TEXT("Fixture.NoViewportRegistration"), LocalPlayer->GetViewportClient());
			LocalPlayer->SwitchController(Controller);
			if (!Test.TestTrue(TEXT("Fixture.LocalPlayerBacklink"), Controller->GetLocalPlayer() == LocalPlayer.Get()
				&& LocalPlayer->PlayerController == Controller)) return false;
			return FVBStartupLeaseTestAccess::AssertCurrent(Test, *Lease);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStartupDefinitionSafetyTest, "Vowbound.Unit.Cinematic.Startup.DefinitionProtection", StartupSafetyFlags)
bool FVBStartupDefinitionSafetyTest::RunTest(const FString& Parameters)
{
	FStartupSafetyFixture F;
	if (!F.Initialize(*this)) return false;
	TestFalse(TEXT("Null definition cannot initialize lease"), F.Lease->Initialize(nullptr));
	TestFalse(TEXT("Null definition acquires no save hold"), F.Save->IsGameSaveHeld());
	TestFalse(TEXT("Null definition creates no input guard"), FVBStartupLeaseTestAccess::HasInput(*F.Lease));
	TestEqual(TEXT("Null definition creates no preparation hold"), F.Readiness->Query().BlockedReasons
		& static_cast<uint32>(EVBPlayerPreparationReason::ExternalHold), 0u);
	F.Definition->PlaybackSettings.PlayRate = 0.f;
	TestFalse(TEXT("Invalid settings cannot initialize lease"), F.Lease->Initialize(F.Definition.Get()));
	TestFalse(TEXT("Invalid settings acquire no save hold"), F.Save->IsGameSaveHeld());
	F.Definition->PlaybackSettings.PlayRate = 1.f;
	TestTrue(TEXT("Valid definition initializes protection"), F.Lease->Initialize(F.Definition.Get()));
	TestTrue(TEXT("Valid definition acquires save hold"), F.Lease->IsSaveHeld());
	TestTrue(TEXT("Valid definition installs input guard"), FVBStartupLeaseTestAccess::HasInput(*F.Lease));
	AddExpectedError(TEXT("새 게임 시네마틱 정의 거절"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("Public startup rejects null before protection"), F.Owner->BeginNewGameCinematic(F.Controller, nullptr,
		F.Save->GetGameStartContext().AttemptId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStartupUnrestoredSafetyTest, "Vowbound.Unit.Cinematic.Startup.UnrestoredKeepsHold", StartupSafetyFlags)
bool FVBStartupUnrestoredSafetyTest::RunTest(const FString& Parameters)
{
	FStartupSafetyFixture F;
	if (!F.Initialize(*this) || !TestTrue(TEXT("Protection initialized"), F.Lease->Initialize(F.Definition.Get()))) return false;
	F.Lease->Finish(true);
	TestTrue(TEXT("Success request before restoration cannot release save hold"), F.Lease->IsSaveHeld());
	TestTrue(TEXT("Premature success retains input guard"), FVBStartupLeaseTestAccess::HasInput(*F.Lease));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStartupFailedSafetyTest, "Vowbound.Unit.Cinematic.Startup.AbortedKeepsHold", StartupSafetyFlags)
bool FVBStartupFailedSafetyTest::RunTest(const FString& Parameters)
{
	FStartupSafetyFixture F;
	if (!F.Initialize(*this) || !TestTrue(TEXT("Protection initialized"), F.Lease->Initialize(F.Definition.Get()))) return false;
	FVBStartupLeaseTestAccess::ObserveRestore(*F.Lease);
	F.Lease->Finish(false);
	TestTrue(TEXT("Failure after restoration still retains save hold"), F.Lease->IsSaveHeld());
	F.Lease->Finish(true);
	TestTrue(TEXT("Later success cannot revive failed lease"), F.Lease->IsSaveHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStartupFollowupSafetyTest, "Vowbound.Unit.Cinematic.Startup.FollowupBlockReleasesOwnHold", StartupSafetyFlags)
bool FVBStartupFollowupSafetyTest::RunTest(const FString& Parameters)
{
	FStartupSafetyFixture F;
	if (!F.Initialize(*this) || !TestTrue(TEXT("Protection initialized"), F.Lease->Initialize(F.Definition.Get()))) return false;
	FVBStartupLeaseTestAccess::ObserveRestore(*F.Lease);
	const FVBPlayerPreparationHoldToken Followup = F.Readiness->AcquirePreparationHold(F.Owner.Get(), TEXT("CompletionFollowup"));
	if (!TestTrue(TEXT("Follow-up preparation hold admitted"), Followup.Id.IsValid())) return false;
	TestTrue(TEXT("Follow-up adds genuine Blocked reason"), (F.Readiness->Query().BlockedReasons
		& static_cast<uint32>(EVBPlayerPreparationReason::ExternalHold)) != 0);
	const FVBSaveHoldToken Other = F.Save->BeginSaveHold(TEXT("OtherTransition"));
	if (!TestTrue(TEXT("Independent save hold admitted"), Other.IsValid())) return false;
	F.Lease->Finish(true);
	TestFalse(TEXT("Restored success releases its own save hold despite follow-up block"), F.Lease->IsSaveHeld());
	TestTrue(TEXT("Other save hold is preserved"), F.Save->IsSaveHoldActive(Other));
	TestTrue(TEXT("Follow-up preparation remains blocked"), (F.Readiness->Query().BlockedReasons
		& static_cast<uint32>(EVBPlayerPreparationReason::ExternalHold)) != 0);
	F.Save->EndSaveHold(Other);
	TestFalse(TEXT("No orphaned startup save hold remains"), F.Save->IsGameSaveHeld());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStartupNPCDefaultsTest, "Vowbound.Unit.Cinematic.Startup.NoNPCDefaultReadiness", StartupSafetyFlags)
bool FVBStartupNPCDefaultsTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("Base character CDO does not attach player readiness"), GetDefault<AVBCharacter>()->FindComponentByClass<UVBPlayerReadinessComponent>());
	TestNull(TEXT("GASP NPC CDO does not attach player readiness"), GetDefault<AVBGASPNPC>()->FindComponentByClass<UVBPlayerReadinessComponent>());
	return true;
}
#endif
