#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// Exercise the production poll/trace without ticking a shared scheduler or changing GFrameCounter.
class FVBPlayerRestoreTestAccess
{
public:
	static void Poll(UVBPlayerRestoreComponent& Restore) { Restore.PollStreaming(); }
};

class FVBPlayerReadinessTestAccess
{
public:
	static void NotifyDeferred(UVBPlayerReadinessComponent& Component)
	{
		const auto Handle = Component.TagNotificationHandle;
		if (!Component.NotifyAfterTagChange(0.0f)) FTSTicker::RemoveTicker(Handle);
	}
};

namespace
{
	uint32 Bit(EVBPlayerPreparationReason Reason) { return static_cast<uint32>(Reason); }
	// 이름은 파일마다 달라야 한다 - 익명 네임스페이스도 unity 묶음에서는 한 번역 단위라 같은 이름이 부딪힌다.
	constexpr EAutomationTestFlags PreparationTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FPreparationWorld
	{
		UWorld* World = nullptr;
		ACharacter* Pawn = nullptr;
		UVBPlayerRestoreComponent* Restore = nullptr;
		UVBPlayerReadinessComponent* Readiness = nullptr;
		UAbilitySystemComponent* ASC = nullptr;

		~FPreparationWorld()
		{
			if (!World) return;
			// CreateWorld(nullptr) creates a package owned exclusively by this fixture.
			const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
			if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false);
			if (GEngine) GEngine->DestroyWorldContext(World);
			OwnedPackage->SetDirtyFlag(false);
		}

		bool Initialize(FAutomationTestBase& Test, bool bGround = false)
		{
			if (!Test.TestNotNull(TEXT("engine"), GEngine)) return false;
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("isolated world"), World)) return false;
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Pawn = World->SpawnActor<ACharacter>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator, Spawn);
			if (!Test.TestNotNull(TEXT("pawn"), Pawn)) return false;
			Restore = NewObject<UVBPlayerRestoreComponent>(Pawn);
			Pawn->AddInstanceComponent(Restore);
			Restore->RegisterComponent();
			ASC = NewObject<UAbilitySystemComponent>(Pawn);
			Pawn->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitAbilityActorInfo(Pawn, Pawn);
			Readiness = NewObject<UVBPlayerReadinessComponent>(Pawn);
			Pawn->AddInstanceComponent(Readiness);
			Readiness->RegisterComponent();
			if (bGround)
			{
				AActor* Floor = World->SpawnActor<AActor>();
				if (!Test.TestNotNull(TEXT("floor actor"), Floor)) return false;
				UBoxComponent* Box = NewObject<UBoxComponent>(Floor);
				Floor->AddInstanceComponent(Box);
				Floor->SetRootComponent(Box);
				Box->SetMobility(EComponentMobility::Static);
				Box->SetBoxExtent(FVector(1000.0, 1000.0, 10.0));
				Box->SetCollisionObjectType(ECC_WorldStatic);
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionResponseToAllChannels(ECR_Block);
				Floor->SetActorLocation(FVector(0.0, 0.0, -10.0));
				Box->RegisterComponent();
			}
			AGameStateBase* GS = World->SpawnActor<AGameStateBase>();
			if (!Test.TestNotNull(TEXT("game state"), GS)) return false;
			World->SetGameState(GS);
			World->InitializeActorsForPlay(FURL());
			GS->HandleBeginPlay();
			return Test.TestTrue(TEXT("world began play"), World->HasBegunPlay());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPreparationReasonsTest, "Vowbound.Unit.PlayerPreparation.Reasons", PreparationTestFlags)
bool FVBPreparationReasonsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("default snapshot is unavailable"), FVBPlayerPreparationSnapshot().State == EVBPlayerPreparationState::Unavailable);
	TestTrue(TEXT("null pawn fails closed"), UVBPlayerReadinessComponent::QueryForPawn(nullptr).State == EVBPlayerPreparationState::Unavailable);
	TestTrue(TEXT("no reasons is ready"), UVBPlayerReadinessComponent::EvaluateReasons(0, 0) == EVBPlayerPreparationState::Ready);
	for (uint32 Index = 0; Index < 16; ++Index)
	{
		const uint32 Reason = 1u << Index;
		TestTrue(TEXT("each blocker blocks"), UVBPlayerReadinessComponent::EvaluateReasons(Reason, 0) == EVBPlayerPreparationState::Blocked);
		TestTrue(TEXT("missing evidence takes precedence"), UVBPlayerReadinessComponent::EvaluateReasons(~0u, Reason) == EVBPlayerPreparationState::Unavailable);
	}
	FVBPlayerRestoreReceipt Receipt;
	for (uint8 Index = 0; Index <= static_cast<uint8>(EVBPlayerRestoreOutcome::Cancelled); ++Index)
	{
		Receipt.Outcome = static_cast<EVBPlayerRestoreOutcome>(Index);
		Receipt.bGroundFound = true;
		Receipt.bStreamingTimedOut = false;
		TestEqual(TEXT("only explicit safe outcomes admit"), Receipt.IsSafeForPreparation(),
			Receipt.Outcome == EVBPlayerRestoreOutcome::NotRequired || Receipt.Outcome == EVBPlayerRestoreOutcome::Completed);
	}
	Receipt.Outcome = EVBPlayerRestoreOutcome::Completed;
	Receipt.bGroundFound = false;
	TestFalse(TEXT("completed without ground is not safe"), Receipt.IsSafeForPreparation());
	Receipt.bGroundFound = true;
	Receipt.bStreamingTimedOut = true;
	TestFalse(TEXT("timeout fallback with ground is not safe"), Receipt.IsSafeForPreparation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPreparationHoldsAndTagsTest, "Vowbound.Unit.PlayerPreparation.HoldsAndTags", PreparationTestFlags)
bool FVBPreparationHoldsAndTagsTest::RunTest(const FString& Parameters)
{
	FPreparationWorld F;
	if (!F.Initialize(*this)) return false;
	const FGuid First = FGuid::NewGuid();
	F.Readiness->BeginInitialization(First, EVBGameStartIntent::NewGame);
	TestTrue(TEXT("new game has explicit no-restore receipt"), F.Restore->GetRestoreReceipt().Outcome == EVBPlayerRestoreOutcome::NotRequired);
	TestTrue(TEXT("initialization remains blocked before its end"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::Initialization)) != 0);
	F.Readiness->CompleteInitialization(First, false);
	TestFalse(TEXT("new game does not require Continue dispatch"), (F.Readiness->Query().UnavailableReasons & Bit(EVBPlayerPreparationReason::Initialization)) != 0);
	TestTrue(TEXT("missing UI/controller/start source still fails closed"), F.Readiness->Query().State == EVBPlayerPreparationState::Unavailable);
	const auto One = F.Readiness->AcquirePreparationHold(F.Pawn, TEXT("One"));
	const auto Two = F.Readiness->AcquirePreparationHold(F.Pawn, TEXT("Two"));
	F.Readiness->ReleasePreparationHold(One);
	TestTrue(TEXT("second hold survives first release"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::ExternalHold)) != 0);
	F.Readiness->BeginInitialization(First, EVBGameStartIntent::NewGame);
	TestTrue(TEXT("duplicate begin preserves live holds"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::ExternalHold)) != 0);
	F.Readiness->ReleasePreparationHold(Two);
	F.Readiness->ReleasePreparationHold(Two);
	TestFalse(TEXT("last hold releases idempotently"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::ExternalHold)) != 0);
	const FGuid Second = FGuid::NewGuid();
	F.Readiness->BeginInitialization(Second, EVBGameStartIntent::Continue);
	const auto Current = F.Readiness->AcquirePreparationHold(F.Pawn, TEXT("Current"));
	F.Readiness->ReleasePreparationHold(Two);
	F.Readiness->CompleteInitialization(First, true);
	TestTrue(TEXT("stale completion leaves current initialization blocked"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::Initialization)) != 0);
	TestTrue(TEXT("stale hold cannot release current hold"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::ExternalHold)) != 0);
	F.Readiness->ReleasePreparationHold(Current);
	F.Readiness->CompleteInitialization(Second, false);
	F.Readiness->CompleteInitialization(Second, true);
	TestTrue(TEXT("failed Continue is not promoted by a duplicate end"), (F.Readiness->Query().UnavailableReasons & Bit(EVBPlayerPreparationReason::Initialization)) != 0);
	int32 Signals = 0;
	const FDelegateHandle Handle = F.Readiness->OnPreparationChanged.AddLambda([&Signals]() { ++Signals; });
	ON_SCOPE_EXIT { F.Readiness->OnPreparationChanged.Remove(Handle); };
	F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Interacting);
	TestTrue(TEXT("interaction signal rechecks current blocking tag"), Signals > 0 && (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::Interaction)) != 0);
	F.ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Interacting);
	TestFalse(TEXT("interaction release removes only its reason"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::Interaction)) != 0);
	APlayerController* PC = F.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("controller for ordered lock release"), PC)) return false;
	AController* PreviousController = F.Pawn->GetController();
	PC->Possess(F.Pawn);
	if (!TestTrue(TEXT("fixture controller possesses the current pawn"), F.Pawn->GetController() == PC && PC->GetPawn() == F.Pawn)) return false;
	if (PreviousController && PreviousController != PC)
	{
		TestFalse(TEXT("controller replacement invalidates the old initialization"), F.Readiness->Query().InitializationId.IsValid());
	}
	const FGuid ControllerInitialization = FGuid::NewGuid();
	F.Readiness->BeginInitialization(ControllerInitialization, EVBGameStartIntent::NewGame);
	F.Readiness->CompleteInitialization(ControllerInitialization, false);
	if (!TestTrue(TEXT("hold test uses the current controller initialization"), F.Readiness->Query().InitializationId == ControllerInitialization)) return false;
	bool bLastSignalSawIgnoredInput = false;
	const FDelegateHandle LockHandle = F.Readiness->OnPreparationChanged.AddLambda([PC, &bLastSignalSawIgnoredInput]()
	{
		bLastSignalSawIgnoredInput = PC->IsMoveInputIgnored();
	});
	ON_SCOPE_EXIT { F.Readiness->OnPreparationChanged.Remove(LockHandle); };
	PC->SetIgnoreMoveInput(true);
	F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Interacting);
	F.ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Interacting);
	TestTrue(TEXT("tag callback precedes the owner's input unlock"), bLastSignalSawIgnoredInput);
	const int32 BeforeUnlock = Signals;
	PC->SetIgnoreMoveInput(false);
	TestEqual(TEXT("controller unlock does not itself send a tag signal"), Signals, BeforeUnlock);
	FVBPlayerReadinessTestAccess::NotifyDeferred(*F.Readiness);
	TestTrue(TEXT("deferred native signal rechecks the released input lock"), Signals > BeforeUnlock && !bLastSignalSawIgnoredInput);
	F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	TestTrue(TEXT("dead avatar is unavailable"), (F.Readiness->Query().UnavailableReasons & Bit(EVBPlayerPreparationReason::Dead)) != 0);
	F.ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	AActor* HoldOwner = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("lost hold owner actor"), HoldOwner)) return false;
	const TWeakObjectPtr<AActor> WeakHoldOwner(HoldOwner);
	const auto LostOwner = F.Readiness->AcquirePreparationHold(HoldOwner, TEXT("LostOwner"));
	if (!TestTrue(TEXT("lost owner hold registered in current initialization"), LostOwner.Id.IsValid() && LostOwner.InitializationId == ControllerInitialization)) return false;
	TestTrue(TEXT("live hold owner blocks before destruction"), (F.Readiness->Query().BlockedReasons & Bit(EVBPlayerPreparationReason::ExternalHold)) != 0);
	if (!TestTrue(TEXT("hold owner Destroy succeeded"), HoldOwner->Destroy())) return false;
	TestTrue(TEXT("destroyed hold owner is weak-invalid"), !WeakHoldOwner.IsValid());
	TestTrue(TEXT("lost hold owner is not auto-ready"), (F.Readiness->Query().UnavailableReasons & Bit(EVBPlayerPreparationReason::HoldOwner)) != 0);
	F.Readiness->ReleasePreparationHold(LostOwner);
	TestFalse(TEXT("explicit token release removes only the lost-owner reason"), (F.Readiness->Query().UnavailableReasons & Bit(EVBPlayerPreparationReason::HoldOwner)) != 0);
	F.Readiness->UnregisterComponent();
	TestTrue(TEXT("unregistered component is unavailable"), F.Readiness->Query().State == EVBPlayerPreparationState::Unavailable);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPreparationRestoreContextTest, "Vowbound.Unit.PlayerPreparation.RestoreContext", PreparationTestFlags)
bool FVBPreparationRestoreContextTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UPackage> CleanedPackage;
	{
		FPreparationWorld F;
		if (!F.Initialize(*this)) return false;
		CleanedPackage.Reset(F.World->GetPackage());
		const FGuid Id = FGuid::NewGuid();
		F.Restore->ArmInitialization(Id);
		UVBSaveGame* Save = NewObject<UVBSaveGame>();
		Save->Progress.bHasSavedTransform = false;
		FVBSaveContext Context;
		Context.World = F.World;
		F.Restore->ReadSave(*Save, Context);
		TestTrue(TEXT("wrong pawn does not manufacture NotRequired"), F.Restore->GetRestoreReceipt().Outcome == EVBPlayerRestoreOutcome::Unknown);
		Context.Pawn = F.Pawn;
		F.Restore->ReadSave(*Save, Context);
		TestTrue(TEXT("matching read without position is explicit NotRequired"), F.Restore->GetRestoreReceipt().Outcome == EVBPlayerRestoreOutcome::NotRequired);
		TestTrue(TEXT("receipt belongs to initialization"), F.Restore->GetRestoreReceipt().InitializationId == Id);
		F.Restore->ArmInitialization(FGuid::NewGuid());
		F.Restore->MarkRestoreNotRequired(Id);
		TestTrue(TEXT("stale NotRequired does not open a new initialization"), F.Restore->GetRestoreReceipt().Outcome == EVBPlayerRestoreOutcome::Unknown);
		CleanedPackage->SetDirtyFlag(true);
		TestTrue(TEXT("Preparation package starts dirty before cleanup"), CleanedPackage->IsDirty());
	}
	TestFalse(TEXT("Preparation cleanup clears its own world package"), CleanedPackage->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPreparationGroundRestoreTest, "Vowbound.Unit.PlayerPreparation.GroundRestore", PreparationTestFlags)
bool FVBPreparationGroundRestoreTest::RunTest(const FString& Parameters)
{
	FPreparationWorld F;
	if (!F.Initialize(*this, true)) return false;
	F.Restore->ArmInitialization(FGuid::NewGuid());
	F.Restore->BeginRestore(FTransform(FVector(0.0, 0.0, 100.0)));
	TestTrue(TEXT("actual restore is in progress"), F.Restore->IsRestoring());
	TestTrue(TEXT("pawn is frozen"), F.Pawn->GetCharacterMovement()->MovementMode == MOVE_None);
	FVBPlayerRestoreTestAccess::Poll(*F.Restore);
	TestTrue(TEXT("actual ground trace completes safely"), F.Restore->GetRestoreReceipt().IsSafeForPreparation());
	TestFalse(TEXT("ground completion stops restoring"), F.Restore->IsRestoring());
	TestTrue(TEXT("legacy movement unfreezes"), F.Pawn->GetCharacterMovement()->MovementMode == MOVE_Walking);
	const FGuid Old = FGuid::NewGuid();
	const FGuid New = FGuid::NewGuid();
	F.Restore->ArmInitialization(Old);
	F.Restore->BeginRestore(FTransform(FVector(0.0, 0.0, 100.0)));
	F.Restore->ArmInitialization(New);
	F.Restore->BeginRestore(FTransform(FVector(0.0, 0.0, 100.0)));
	FVBPlayerRestoreTestAccess::Poll(*F.Restore);
	TestTrue(TEXT("old physical job cannot complete new receipt"), F.Restore->GetRestoreReceipt().InitializationId == New
		&& F.Restore->GetRestoreReceipt().Outcome == EVBPlayerRestoreOutcome::StartFailed);
	TestFalse(TEXT("old job still finishes its physical lifetime"), F.Restore->IsRestoring());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPreparationMissingGroundTest, "Vowbound.Unit.PlayerPreparation.MissingGround", PreparationTestFlags)
bool FVBPreparationMissingGroundTest::RunTest(const FString& Parameters)
{
	FPreparationWorld F;
	if (!F.Initialize(*this)) return false;
	const FGuid Id = FGuid::NewGuid();
	F.Readiness->BeginInitialization(Id, EVBGameStartIntent::Continue);
	F.Restore->BeginRestore(FTransform(FVector(0.0, 0.0, 100.0)));
	F.Readiness->CompleteInitialization(Id, true);
	FVBPlayerRestoreTestAccess::Poll(*F.Restore);
	TestTrue(TEXT("trace miss has a distinct receipt"), F.Restore->GetRestoreReceipt().Outcome == EVBPlayerRestoreOutcome::GroundNotFound);
	TestFalse(TEXT("trace miss is not preparation success"), F.Restore->GetRestoreReceipt().IsSafeForPreparation());
	TestTrue(TEXT("native query retains failed restore reason"), (F.Readiness->Query().UnavailableReasons & Bit(EVBPlayerPreparationReason::Restore)) != 0);
	F.Restore->UnregisterComponent();
	F.Restore->BeginRestore(FTransform::Identity);
	TestFalse(TEXT("unregistered restore cannot schedule a new job"), F.Restore->IsRestoring());
	return true;
}

#endif
