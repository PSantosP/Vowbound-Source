#include "Misc/AutomationTest.h"
#include "Character/VBFinisherComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherLifecycleTest, "Vowbound.Unit.Finisher.SessionCleanup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBFinisherLifecycleTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}
	ACharacter* Attacker = World->SpawnActor<ACharacter>();
	ACharacter* Victim = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("attacker"), Attacker) || !TestNotNull(TEXT("victim"), Victim))
	{
		World->DestroyWorld(false);
		return false;
	}
	UVBFinisherComponent* Finisher = NewObject<UVBFinisherComponent>(Attacker);
	Attacker->AddInstanceComponent(Finisher);
	Finisher->RegisterComponent();
	UCapsuleComponent* OwnerRoot = Attacker->GetCapsuleComponent();
	UCapsuleComponent* VictimRoot = Victim->GetCapsuleComponent();
	USkeletalMeshComponent* OwnerMesh = Attacker->GetMesh();
	USkeletalMeshComponent* VictimMesh = Victim->GetMesh();

	// A missing half of a pair must fail before installing any ownership or relationship.
	TArray<FPoseSearchBlueprintResult> IncompletePair;
	IncompletePair.AddDefaulted();
	TestFalse(TEXT("incomplete pair rejected"), Finisher->PlayInteractionLocally(IncompletePair, 0.0f));
	TestTrue(TEXT("failed validation leaves no participants"), Finisher->InteractionParticipants.IsEmpty());
	TestFalse(TEXT("failed validation leaves no owner registration"), UVBFinisherComponent::ParticipantOwners.Contains(Attacker));

	// Exercise the real session teardown against actual registered scene components.
	// Montage/pose rendering is intentionally outside this test; PIE covers those assets.
	auto InstallSession = [&](int32 Session, bool bOwnRelationships)
	{
		Finisher->LocalInteractionSession = Session;
		Finisher->InteractionReplay.SessionId = Session;
		Finisher->InteractionReplay.bActive = true;
		++Finisher->LocalGeneration;
		Finisher->InteractionParticipants = {Attacker, Victim};
		Finisher->InteractionMontages.SetNum(2);
		Finisher->InteractionMontageInstanceIds = {INDEX_NONE, INDEX_NONE};
		Finisher->AddedTickPrerequisite = {false, bOwnRelationships};
		Finisher->AddedOwnerCollisionIgnore = {false, bOwnRelationships};
		Finisher->AddedVictimCollisionIgnore = {false, bOwnRelationships};
		UVBFinisherComponent::ParticipantOwners.Add(Attacker, Finisher);
		UVBFinisherComponent::ParticipantOwners.Add(Victim, Finisher);
		OwnerMesh->AddTickPrerequisiteComponent(VictimMesh);
		OwnerRoot->IgnoreComponentWhenMoving(VictimRoot, true);
		VictimRoot->IgnoreComponentWhenMoving(OwnerRoot, true);
	};
	InstallSession(1, true);
	const int32 FirstGeneration = Finisher->LocalGeneration;
	Finisher->EarlyOut();
	TestTrue(TEXT("cancel outcome retained"), Finisher->WasLastFinisherCancelled());
	TestFalse(TEXT("cancel ends session immediately"), Finisher->IsPlaying());
	TestFalse(TEXT("cancel replicates inactive state"), Finisher->InteractionReplay.bActive);
	TestFalse(TEXT("cancel removes attacker collision ignore"), OwnerRoot->GetMoveIgnoreComponents().Contains(VictimRoot));
	TestFalse(TEXT("cancel removes victim collision ignore"), VictimRoot->GetMoveIgnoreComponents().Contains(OwnerRoot));
	TestFalse(TEXT("cancel removes tick prerequisite"), OwnerMesh->PrimaryComponentTick.GetPrerequisites().Contains(
		FTickPrerequisite(VictimMesh, VictimMesh->PrimaryComponentTick)));
	TestFalse(TEXT("cancel removes notify owner"), UVBFinisherComponent::ParticipantOwners.Contains(Victim));

	InstallSession(2, false);
	Finisher->HandleInteractionMontageEnded(nullptr, false, FirstGeneration);
	TestTrue(TEXT("old montage completion cannot finish next session"), Finisher->IsPlaying());
	Finisher->HandleInteractionVictimMontageEnded(nullptr, true, FirstGeneration);
	TestTrue(TEXT("old victim interruption cannot cancel next session"), Finisher->IsPlaying());
	Finisher->FinishInteraction(true, false, true); // Same rollback path used by partial start failure.
	TestTrue(TEXT("rollback preserves pre-existing attacker ignore"), OwnerRoot->GetMoveIgnoreComponents().Contains(VictimRoot));
	TestTrue(TEXT("rollback preserves pre-existing victim ignore"), VictimRoot->GetMoveIgnoreComponents().Contains(OwnerRoot));
	TestTrue(TEXT("rollback preserves pre-existing tick relationship"), OwnerMesh->PrimaryComponentTick.GetPrerequisites().Contains(
		FTickPrerequisite(VictimMesh, VictimMesh->PrimaryComponentTick)));
	TestFalse(TEXT("rollback releases registry"), UVBFinisherComponent::ParticipantOwners.Contains(Attacker));

	// Exercise the actual ASC OnRep entry point: an interrupted GAS wait must not reclaim the slot.
	UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(Attacker);
	Attacker->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Attacker, Attacker);
	Attacker->SetRole(ROLE_SimulatedProxy);
	// ASC caches authority when registered; changing the actor role alone does not update it.
	ASC->CacheIsNetSimulated();
	TestFalse(TEXT("ASC fixture is a simulated proxy"), ASC->IsOwnerActorAuthoritative());
	FGameplayAbilityRepAnimMontage& Rep = ASC->GetRepAnimMontageInfo_Mutable();
	Rep.Animation = NewObject<UAnimMontage>(ASC);
	Rep.PlayInstanceId = 7;
	Rep.IsStopped = false;
	TestTrue(TEXT("external producer acquires GAS montage ownership"), ASC->AcquireExternalMontageOwnership(Finisher, 7));
	TestFalse(TEXT("another producer cannot steal ownership"), ASC->AcquireExternalMontageOwnership(Victim, 7));
	TestEqual(TEXT("GAS play is rejected while external producer owns slots"),
		ASC->PlayMontage(nullptr, FGameplayAbilityActivationInfo(), Cast<UAnimMontage>(Rep.Animation), 1.0f), 0.0f);
	ASC->OnRep_ReplicatedAnimMontage();
	TestTrue(TEXT("replication is deferred while external producer owns slots"), ASC->bDeferredExternalMontageRep);
	ASC->ReleaseExternalMontageOwnership(Victim);
	TestTrue(TEXT("unrelated release cannot remove ownership"), ASC->ExternalMontageOwner.IsValid());
	ASC->ReleaseExternalMontageOwnership(Finisher);
	TestFalse(TEXT("matching release relinquishes montage ownership"), ASC->ExternalMontageOwner.IsValid());
	TestFalse(TEXT("superseded wait does not reach engine replay after release"), ASC->bPendingMontageRep);
	ASC->OnRep_ReplicatedAnimMontage();
	TestFalse(TEXT("late old actor-channel update cannot resurrect superseded wait"), ASC->bPendingMontageRep);
	Rep.PlayInstanceId = 8;
	TestTrue(TEXT("replay uses authoritative previous play id"), ASC->AcquireExternalMontageOwnership(Finisher, 7));
	ASC->OnRep_ReplicatedAnimMontage();
	ASC->ReleaseExternalMontageOwnership(Finisher);
	TestTrue(TEXT("new play arriving during old session reaches engine preparation on release"), ASC->bPendingMontageRep);
	Rep.IsStopped = true;
	ASC->OnRep_ReplicatedAnimMontage();
	TestFalse(TEXT("stopped state never starts a missing montage"), ASC->bPendingMontageRep);
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	Rep.IsStopped = false;
	Rep.PlayInstanceId = 9;
	ASC->OnRep_ReplicatedAnimMontage();
	TestFalse(TEXT("dead avatar rejects obsolete ability montage replay"), ASC->bPendingMontageRep);
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	Attacker->SetRole(ROLE_Authority);
	ASC->CacheIsNetSimulated();

	// A session first observed after its second second still needs a retry budget for actor channels.
	Finisher->InteractionReplay.SessionId = 3;
	Finisher->InteractionReplay.bActive = true;
	Finisher->InteractionReplay.ServerStartTime = World->GetTimeSeconds() - 3.0;
	Finisher->OnRep_InteractionReplay();
	TestTrue(TEXT("late relevancy retries unresolved participants"),
		World->GetTimerManager().IsTimerActive(Finisher->InteractionReplayRetryTimer));
	TestNotEqual(TEXT("late relevancy does not mark unresolved session finished"), Finisher->FinishedInteractionSession, 3);
	const double ReceiptDeadline = Finisher->ReplayRetryDeadline;
	Finisher->OnRep_InteractionReplay();
	TestEqual(TEXT("same-session updates preserve bounded receipt deadline"), Finisher->ReplayRetryDeadline, ReceiptDeadline);
	Finisher->InteractionReplay.bActive = false;
	Finisher->OnRep_InteractionReplay();
	TestFalse(TEXT("completion while waiting cancels replay timer"),
		World->GetTimerManager().IsTimerActive(Finisher->InteractionReplayRetryTimer));
	TestEqual(TEXT("completion records never-started session as finished"), Finisher->FinishedInteractionSession, 3);

	InstallSession(4, true);
	Attacker->SetRole(ROLE_SimulatedProxy);
	Victim->OnDestroyed.AddUniqueDynamic(Finisher, &UVBFinisherComponent::HandleInteractionParticipantDestroyed);
	Victim->Destroy();
	TestFalse(TEXT("local participant loss releases current playback"), Finisher->IsPlaying());
	TestTrue(TEXT("local participant loss preserves authoritative active state"), Finisher->InteractionReplay.bActive);
	TestNotEqual(TEXT("local participant loss does not declare server completion"), Finisher->FinishedInteractionSession, 4);
	TestTrue(TEXT("local participant loss retries active replay"),
		World->GetTimerManager().IsTimerActive(Finisher->InteractionReplayRetryTimer));
	// A later reference-remapping update may retry even after the earlier bounded attempt elapsed.
	World->GetTimerManager().ClearTimer(Finisher->InteractionReplayRetryTimer);
	Finisher->ReplayRetryDeadline = World->GetTimeSeconds() - 1.0;
	Finisher->OnRep_InteractionReplay();
	TestTrue(TEXT("new remapping update reopens bounded preparation window"),
		Finisher->ReplayRetryDeadline > World->GetTimeSeconds());
	Finisher->InteractionReplay.bActive = false;
	Finisher->OnRep_InteractionReplay();
	TestEqual(TEXT("authoritative completion prevents reentry"), Finisher->FinishedInteractionSession, 4);
	Attacker->SetRole(ROLE_Authority);

	OwnerMesh->RemoveTickPrerequisiteComponent(VictimMesh);
	OwnerRoot->IgnoreComponentWhenMoving(VictimRoot, false);
	VictimRoot->IgnoreComponentWhenMoving(OwnerRoot, false);
	World->DestroyWorld(false);
	return true;
}
#endif
