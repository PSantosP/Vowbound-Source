#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "VBSaveGateFixtureParticipant.h"

class FVBSaveGateTestAccess
{
public:
	static void SetDispatch(UVBSaveGameSubsystem& Save,
		TFunction<void(UVBSaveGame*, const FString&, int32, const FAsyncSaveGameToSlotDelegate&)> Dispatch)
	{
		Save.TestSaveDispatch = MoveTemp(Dispatch);
	}
	static void SetDeleteDispatch(UVBSaveGameSubsystem& Save, TFunction<void(const FString&, int32)> Dispatch)
	{
		Save.TestDeleteDispatch = MoveTemp(Dispatch);
	}
};

namespace
{
	struct FSaveFixture
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
		TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
		int32 Dispatches = 0;
		TArray<FAsyncSaveGameToSlotDelegate> Pending;
		TArray<FString> DeletedSlots;
		int32 DeletedUserIndex = INDEX_NONE;
		FSaveFixture()
		{
			// 플랫폼 디스크에 닿지 않는다. 예상 밖 gate 누락도 실제 슬롯을 덮지 못한다.
			FVBSaveGateTestAccess::SetDispatch(*Save.Get(), [this](UVBSaveGame*, const FString&, int32,
				const FAsyncSaveGameToSlotDelegate& Completion) { ++Dispatches; Pending.Add(Completion); });
			FVBSaveGateTestAccess::SetDeleteDispatch(*Save.Get(), [this](const FString& Slot, int32 UserIndex)
			{
				DeletedSlots.Add(Slot);
				DeletedUserIndex = UserIndex;
			});
			Save->StartNewGame();
		}
		~FSaveFixture()
		{
			FVBSaveGateTestAccess::SetDispatch(*Save.Get(), {});
			FVBSaveGateTestAccess::SetDeleteDispatch(*Save.Get(), {});
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSavePublicGateTest, "Vowbound.Unit.Save.Gate.PublicAndReentry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSavePublicGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	UVBSaveGame* Before = F.Save->GetActiveGameSave();
	Before->Progress.PlayTimeSeconds = 7.0;
	const FVBSaveHoldToken Hold = F.Save->BeginSaveHold(TEXT("Fixture"));
	if (!TestTrue(TEXT("Hold admitted"), Hold.IsValid())) return false;
	int32 Refusals = 0;
	const FDelegateHandle Handle = F.Save->OnGameSaveRefused.AddLambda([&](int32 Slot, FName Reason)
	{
		++Refusals;
		TestEqual(TEXT("Refusal is not disk failure"), Reason, FName(TEXT("Held")));
		F.Save->SaveGameSlot(Slot);
	});
	F.Save->SaveGameSlot(0);
	TestEqual(TEXT("Nested refusal does not recurse"), Refusals, 1);
	TestEqual(TEXT("No transport while held"), F.Dispatches, 0);
	TestTrue(TEXT("Active object preserved"), F.Save->GetActiveGameSave() == Before);
	TestEqual(TEXT("Active data preserved"), Before->Progress.PlayTimeSeconds, 7.0);
	TestEqual(TEXT("Slot not selected"), F.Save->GetActiveSlotIndex(), INDEX_NONE);
	F.Save->OnGameSaveRefused.Remove(Handle);
	F.Save->EndSaveHold(Hold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSavePendingGateTest, "Vowbound.Unit.Save.Gate.PendingAndSyncCompletion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSavePendingGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	F.Save->SaveGameSlot(0);
	F.Save->SaveGameSlot(0);
	TestEqual(TEXT("Same slot is not submitted twice"), F.Dispatches, 1);
	TestFalse(TEXT("Scene cannot protect an already submitted write"), F.Save->BeginSaveHold(TEXT("Scene")).IsValid());
	F.Pending[0].ExecuteIfBound(TEXT("Mock"), 0, false);
	FVBSaveHoldToken Hold = F.Save->BeginSaveHold(TEXT("Scene"));
	TestTrue(TEXT("False completion still ends in-flight admission"), Hold.IsValid());
	F.Save->EndSaveHold(Hold);
	FVBSaveGateTestAccess::SetDispatch(*F.Save.Get(), [&](UVBSaveGame*, const FString& Slot, int32 User,
		const FAsyncSaveGameToSlotDelegate& Completion)
	{
		TestFalse(TEXT("Transport call has already reserved its ID"), F.Save->BeginSaveHold(TEXT("InsideDispatch")).IsValid());
		Completion.ExecuteIfBound(Slot, User, false);
	});
	F.Save->SaveGameSlot(1);
	TestTrue(TEXT("Synchronous completion leaves no orphaned request"), F.Save->BeginSaveHold(TEXT("AfterSync")).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveDeleteGateTest, "Vowbound.Unit.Save.Gate.DeleteProtection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveDeleteGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	F.Save->StartNewGameInSlot(0);
	if (!TestEqual(TEXT("Slot seed has an in-flight request"), F.Pending.Num(), 1)) return false;
	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Pending seed cannot be deleted"), F.DeletedSlots.Num(), 0);
	F.Pending[0].ExecuteIfBound(TEXT("Mock"), 0, false);
	const FVBSaveHoldToken Hold = F.Save->BeginSaveHold(TEXT("Scene"));
	if (!TestTrue(TEXT("Scene hold admitted after transport completion"), Hold.IsValid())) return false;
	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Held active slot cannot be deleted"), F.DeletedSlots.Num(), 0);
	TestTrue(TEXT("Rejected deletion preserves hold"), F.Save->IsSaveHoldActive(Hold));
	TestEqual(TEXT("Rejected deletion preserves active slot"), F.Save->GetActiveSlotIndex(), 0);
	F.Save->DeleteGameSave(1);
	TestEqual(TEXT("Held inactive slot can be deleted"), F.DeletedSlots.Num(), 1);
	if (F.DeletedSlots.Num() == 1)
		TestEqual(TEXT("Delete retains slot naming"), F.DeletedSlots[0], UVBSaveGameSubsystem::MakeGameSlotName(1));
	TestEqual(TEXT("Delete retains user index"), F.DeletedUserIndex, 0);
	F.Save->DeleteGameSave(-1);
	F.Save->DeleteGameSave(F.Save->GetMaxSlotCount());
	TestEqual(TEXT("Invalid slots never reach transport"), F.DeletedSlots.Num(), 1);
	const FDelegateHandle Refusal = F.Save->OnGameSaveRefused.AddLambda([&](int32, FName)
	{
		F.Save->DeleteGameSave(2);
	});
	F.Save->SaveGameSlot(0);
	F.Save->OnGameSaveRefused.Remove(Refusal);
	TestEqual(TEXT("Refusal callback cannot reenter delete transport"), F.DeletedSlots.Num(), 1);
	F.Save->EndSaveHold(Hold);
	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Released active slot can be deleted"), F.DeletedSlots.Num(), 2);
	F.Save->SaveGameSlot(0);
	if (!TestEqual(TEXT("Second write captured"), F.Pending.Num(), 2)) return false;
	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Same-slot write prevents deletion"), F.DeletedSlots.Num(), 2);
	F.Save->DeleteGameSave(2);
	TestEqual(TEXT("Other-slot write does not block unrelated delete"), F.DeletedSlots.Num(), 3);
	const FDelegateHandle Changing = F.Save->OnGameStartContextChanged.AddLambda([&]()
	{
		F.Save->DeleteGameSave(1);
	});
	F.Save->StartNewGame();
	F.Save->OnGameStartContextChanged.Remove(Changing);
	TestEqual(TEXT("Start-context callback cannot reenter delete transport"), F.DeletedSlots.Num(), 3);
	F.Pending[1].ExecuteIfBound(TEXT("Mock"), 0, false);
	F.Save->Deinitialize();
	F.Save->DeleteGameSave(1);
	TestEqual(TEXT("Stopped subsystem cannot delete"), F.DeletedSlots.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveGenerationGateTest, "Vowbound.Unit.Save.Gate.GenerationAndBootReceipt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveGenerationGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	const FGuid OldAttempt = F.Save->GetGameStartContext().AttemptId;
	const FVBSaveHoldToken OldHold = F.Save->BeginSaveHold(TEXT("Old"));
	TestTrue(TEXT("First explicit new game bootstrap"), F.Save->ConsumeInitialNewGameStart(OldAttempt));
	TestFalse(TEXT("Same attempt does not reboot"), F.Save->ConsumeInitialNewGameStart(OldAttempt));
	F.Save->StartNewGame();
	TestFalse(TEXT("New game discards previous holds"), F.Save->IsGameSaveHeld());
	const FVBSaveHoldToken NewHold = F.Save->BeginSaveHold(TEXT("New"));
	TestFalse(TEXT("Old token cannot release new hold"), F.Save->EndSaveHold(OldHold));
	TestTrue(TEXT("New hold survives old release"), F.Save->IsSaveHoldActive(NewHold));
	TestFalse(TEXT("Old bootstrap receipt is rejected"), F.Save->ConsumeInitialNewGameStart(OldAttempt));
	TestTrue(TEXT("New attempt has its own receipt"), F.Save->ConsumeInitialNewGameStart(F.Save->GetGameStartContext().AttemptId));
	TestEqual(TEXT("New game itself never writes disk"), F.Dispatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveSnapshotGateTest, "Vowbound.Unit.Save.Gate.SnapshotReentry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveSnapshotGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Owned world"), World)) return false;
	UPackage* Package = World->GetPackage();
	World->SetGameInstance(F.Instance.Get());
	APlayerController* PC = World->SpawnActor<APlayerController>();
	APawn* Pawn = World->SpawnActor<APawn>();
	APlayerState* State = World->SpawnActor<APlayerState>();
	if (!PC || !Pawn || !State) { World->DestroyWorld(false); Package->SetDirtyFlag(false); return false; }
	PC->PlayerState = State;
	Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
	PC->Possess(Pawn);
	UVBSaveGateFixtureParticipant* Writer = NewObject<UVBSaveGateFixtureParticipant>(Pawn);
	Writer->RegisterComponent();
	UVBSaveParticipantSubsystem::Register(Writer);
	UVBSaveGame* Before = F.Save->GetActiveGameSave();
	Before->Progress.PlayTimeSeconds = 7.0;
	int32 Writes = 0;
	Writer->OnWrite = [&](UVBSaveGame& Snapshot, const FVBSaveContext&)
	{
		++Writes;
		Snapshot.Progress.PlayTimeSeconds = 99.0;
		F.Save->DeleteGameSave(0);
		F.Save->BeginSaveHold(TEXT("DuringCollection"));
	};
	TestFalse(TEXT("New hold aborts current collection"), F.Save->SaveCurrentGameToSlot(Pawn, 0));
	TestEqual(TEXT("Actual participant callback entered"), Writes, 1);
	TestTrue(TEXT("Published object is unchanged"), F.Save->GetActiveGameSave() == Before);
	TestEqual(TEXT("Partial snapshot never published"), Before->Progress.PlayTimeSeconds, 7.0);
	TestEqual(TEXT("Aborted snapshot never dispatches"), F.Dispatches, 0);
	TestEqual(TEXT("Snapshot writer cannot reenter delete transport"), F.DeletedSlots.Num(), 0);
	F.Save->StartNewGame();
	Writer->OnWrite = [&](UVBSaveGame& Snapshot, const FVBSaveContext&)
	{
		++Writes;
		Snapshot.Progress.PlayTimeSeconds = 99.0;
		F.Save->StartNewGame();
	};
	TestFalse(TEXT("New generation aborts current collection"), F.Save->SaveCurrentGameToSlot(Pawn, 0));
	TestEqual(TEXT("Second actual participant callback entered"), Writes, 2);
	TestEqual(TEXT("New generation stays clean"), F.Save->GetActiveGameSave()->Progress.PlayTimeSeconds, 0.0);
	TestEqual(TEXT("No transport from either aborted generation"), F.Dispatches, 0);
	Writer->OnWrite = {};
	UVBSaveParticipantSubsystem::Unregister(Writer);
	World->DestroyWorld(false);
	// 직접 만든 월드 패키지 하나만 정리한다. 다른 패키지/프로젝트 자산에는 쓰지 않는다.
	Package->SetDirtyFlag(false);
	return true;
}
#endif
