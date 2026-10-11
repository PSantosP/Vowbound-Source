#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBGASPSlideLibrary.h"
#include "Character/VBSlideMovementComponent.h"
#include "Character/VBCharacter.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "../Animation/VBGASPMutableStateView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPSlideExitTest, "Vowbound.Animation.GASPSlide.ExitContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPSlideExitTest::RunTest(const FString& Parameters)
{
	UClass* Class = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Blueprints/SandboxCharacter_Mover_ABP.SandboxCharacter_Mover_ABP_C"));
	if (!TestNotNull(TEXT("Original slide graph class loads"), Class)) return false;
	UAnimInstance* Instance = NewObject<UAnimInstance>(NewObject<USkeletalMeshComponent>(), Class);
	const IAnimClassInterface* ClassInterface = IAnimClassInterface::GetFromClass(Class);
	if (!TestNotNull(TEXT("Original graph has baked state machines"), ClassInterface)) return false;
	TSet<FName> OriginalStateNames;
	for (const FBakedAnimationStateMachine& Machine : ClassInterface->GetBakedStateMachines())
		for (const FBakedAnimationState& State : Machine.States)
			OriginalStateNames.Add(State.StateName);
	const FNameProperty* Name = FindFProperty<FNameProperty>(Class, TEXT("StateName"));
	if (!TestNotNull(TEXT("Original state name is reflected"), Name)) return false;
	for (const FName State : {FName(TEXT("Slide Transition")), FName(TEXT("Slide Loop")),
		FName(TEXT("Idle Transition")), FName(TEXT("Locomotion Transition")), FName(TEXT("In Air Transition"))})
	{
		TestTrue(TEXT("Transition fixture names exist in the original graph"), OriginalStateNames.Contains(State));
		Name->SetPropertyValue_InContainer(Instance, State);
		TestFalse(TEXT("An authored transition cannot release the slide pose"), UVBGASPSlideLibrary::HasCompletedSlideExit(Instance));
	}
	for (const FName State : {FName(TEXT("Idle Loop")), FName(TEXT("Locomotion Loop")), FName(TEXT("In Air Loop"))})
	{
		TestTrue(TEXT("Loop fixture names exist in the original graph"), OriginalStateNames.Contains(State));
		Name->SetPropertyValue_InContainer(Instance, State);
		TestTrue(TEXT("The original destination loop releases slide ownership"), UVBGASPSlideLibrary::HasCompletedSlideExit(Instance));
	}
	const FByteProperty* Mode = CastField<FByteProperty>(VBGASP::FStateView(Instance).Find(TEXT("MovementMode")));
	if (!TestNotNull(TEXT("Original movement mode is an enum"), Mode) || !Mode->Enum) return false;
	for (int32 Index = 0; Index < Mode->Enum->NumEnums(); ++Index)
		if (Mode->Enum->GetAuthoredNameStringByIndex(Index) == TEXT("Sliding"))
			Mode->SetPropertyValue_InContainer(Instance, static_cast<uint8>(Mode->Enum->GetValueByIndex(Index)));
	TestTrue(TEXT("Original Sliding enum is recognized"), UVBGASPSlideLibrary::HasEnteredSlide(Instance));
	for (int32 Index = 0; Index < Mode->Enum->NumEnums(); ++Index)
		if (Mode->Enum->GetAuthoredNameStringByIndex(Index) == TEXT("OnGround"))
			Mode->SetPropertyValue_InContainer(Instance, static_cast<uint8>(Mode->Enum->GetValueByIndex(Index)));
	TestFalse(TEXT("OnGround is not slide entry"), UVBGASPSlideLibrary::HasEnteredSlide(Instance));
	TestFalse(TEXT("A missing graph cannot complete an exit"), UVBGASPSlideLibrary::HasCompletedSlideExit(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPSlideAirVelocityTest, "Vowbound.Animation.GASPSlide.AirVelocity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPSlideAirVelocityTest::RunTest(const FString& Parameters)
{
	UVBSlideMovementComponent* Movement = NewObject<UVBSlideMovementComponent>();
	Movement->SetMovementMode(MOVE_Falling);
	Movement->Velocity = FVector(200.f, 10.f, -450.f);
	Movement->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_Custom_0);
	Movement->CalcVelocity(0.016f, 0.f, false, 0.f);
	TestEqual(TEXT("Airborne movement does not erase vertical velocity"), Movement->Velocity.Z, -450.0);
	TestFalse(TEXT("No owner cannot admit a compressed slide request"), Movement->CanRequestSlide());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPSlideSlopePolicyTest, "Vowbound.Animation.GASPSlide.SlopePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPSlideSlopePolicyTest::RunTest(const FString& Parameters)
{
	const FVBSlideMovementSettings Settings;
	TestEqual(TEXT("Original boost target"), Settings.GetTargetSpeed(0.f, 0.1f), 800.f);
	TestEqual(TEXT("Flat target after boost"), Settings.GetTargetSpeed(0.f, 0.2f), 100.f);
	TestEqual(TEXT("Shallow downhill target"), Settings.GetTargetSpeed(-10.f, 1.f), 500.f);
	TestEqual(TEXT("Downhill interpolation"), Settings.GetTargetSpeed(-25.f, 1.f), 650.f);
	TestEqual(TEXT("Steep downhill target"), Settings.GetTargetSpeed(-40.f, 10.f), 800.f);
	TestEqual(TEXT("Steep downhill clamp"), Settings.GetTargetSpeed(-60.f, 10.f), 800.f);
	TestEqual(TEXT("Uphill target"), Settings.GetTargetSpeed(25.f, 1.f), 100.f);
	TestEqual(TEXT("Flat deceleration"), Settings.GetDeceleration(0.f), 500.f);
	TestEqual(TEXT("Uphill deceleration interpolation"), Settings.GetDeceleration(25.f), 1250.f);
	TestEqual(TEXT("Steep uphill deceleration"), Settings.GetDeceleration(40.f), 2000.f);
	TestEqual(TEXT("Downhill deceleration clamps to flat value"), Settings.GetDeceleration(-40.f), 500.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPSlideNotifyReceiverTest, "Vowbound.Animation.GASPSlide.NotifyReceiver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPSlideNotifyReceiverTest::RunTest(const FString& Parameters)
{
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
	UVBAnimInstance* Main = NewObject<UVBAnimInstance>(Mesh);
	UAnimInstance* Normal = NewObject<UAnimInstance>(Mesh);
	UAnimInstance* Slide = NewObject<UAnimInstance>(Mesh);
	Main->GASPLocomotionInstance = Normal;
	Main->GASPSlideInstance = Slide;
	Main->bUseGASPSlidePose = false;
	TestEqual(TEXT("Normal notify selects normal graph"), Main->GetGASPNotifyInstance(), Normal);
	Main->bUseGASPSlidePose = true;
	TestEqual(TEXT("Entry and exit notify select slide graph"), Main->GetGASPNotifyInstance(), Slide);
	TestEqual(TEXT("Notify routing does not replace normal locomotion cache"), Main->GetGASPLocomotionInstance(), Normal);
	Main->GASPSlideInstance = nullptr;
	TestNull(TEXT("Missing selected receiver is not silently rerouted"), Main->GetGASPNotifyInstance());
	return true;
}

class FVBSlideJumpTestAccess
{
public:
	static void Prepare(AVBCharacter* Character, UVBAbilitySystemComponent* AbilitySystem, bool bQueued)
	{
		Character->CachedASC = AbilitySystem;
		Character->PendingSlideJumpUntil = bQueued ? 0.f : Character->GetWorld()->GetTimeSeconds() + 0.25f;
		Character->bSlideJumpAwaitingMovement = bQueued;
		Character->bSlideJumpReleaseRequested = true;
		Character->bJumpInputHeld = true;
		if (bQueued) Character->Jump();
	}
	static void Consume(AVBCharacter* Character)
	{
		Character->CheckJumpInput(1.f / 60.f);
	}
	static void Tick(AVBCharacter* Character)
	{
		Character->Tick(1.f / 60.f);
	}
	static bool IsCleared(const AVBCharacter* Character)
	{
		return Character->PendingSlideJumpUntil == 0.f && !Character->bSlideJumpAwaitingMovement
			&& !Character->bSlideJumpReleaseRequested && !Character->bJumpInputHeld && !Character->bPressedJump;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPSlideDeferredJumpTest, "Vowbound.Animation.GASPSlide.DeferredJumpSceneGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPSlideDeferredJumpTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Deferred jump world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AVBCharacter* Character = World->SpawnActor<AVBCharacter>();
	if (!TestNotNull(TEXT("Deferred jump character"), Character)) return false;
	UVBAbilitySystemComponent* AbilitySystem = NewObject<UVBAbilitySystemComponent>(Character);
	Character->AddInstanceComponent(AbilitySystem);
	Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	FVBSlideJumpTestAccess::Prepare(Character, AbilitySystem, true);
	TestTrue(TEXT("Unblocked deferred jump retains normal engine admission"), Character->CanJump());
	const TArray<FGameplayTag> SceneBlocks = {VBGameplayTags::State_Combat_Finishing, VBGameplayTags::State_Dead};
	for (const FGameplayTag Block : SceneBlocks)
	{
		for (const bool bQueued : {false, true})
		{
			FVBSlideJumpTestAccess::Prepare(Character, AbilitySystem, bQueued);
			AbilitySystem->AddLooseGameplayTag(Block);
			if (bQueued)
			{
				TestFalse(TEXT("A new scene blocks an already queued slide jump"), Character->CanJump());
				FVBSlideJumpTestAccess::Consume(Character);
				TestEqual(TEXT("Movement consumption cannot jump inside the new scene"), Character->GetCharacterMovement()->Velocity.Z, 0.0);
			}
			FVBSlideJumpTestAccess::Tick(Character);
			TestTrue(TEXT("Scene rejection clears pending, queued, release and held input"), FVBSlideJumpTestAccess::IsCleared(Character));
			AbilitySystem->RemoveLooseGameplayTag(Block);
			TestTrue(TEXT("Scene rejection does not permanently disable normal jumping"), Character->CanJump());
		}
	}
	return true;
}

#endif
