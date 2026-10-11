#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Camera/VBEnvironmentCameraPolicy.h"
#include "Camera/VBEnvironmentCameraSolver.h"
#include "Camera/VBPlayerCameraManager.h"
#include "Camera/VBEnvironmentCameraComponent.h"
#include "Data/VBEnvironmentCameraConfig.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "Core/CameraAsset.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraSafetyTest, "Vowbound.Camera.Environment.SafetyAndHysteresis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraSafetyTest::RunTest(const FString& Parameters)
{
	using namespace VBEnvironmentCameraPolicy;
	FCandidate Candidates[3];
	Candidates[0] = {FVector::ZeroVector, -1000, false};
	Candidates[1] = {FVector::ZeroVector, 10, true};
	Candidates[2] = {FVector::ZeroVector, 5, true};
	TestEqual(TEXT("Unsafe candidate cannot win by score"), SelectCandidate(Candidates, 0, 8), 2);
	TestEqual(TEXT("Small improvement retains current shoulder candidate"), SelectCandidate(Candidates, 1, 8), 1);
	Candidates[1].bSafe = false;
	TestEqual(TEXT("Safety bypasses hysteresis"), SelectCandidate(Candidates, 1, 1000), 2);
	Candidates[2].Score = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("No safe finite candidate is explicit"), SelectCandidate(Candidates, 2, 8), INDEX_NONE);
	FRecovery Recovery;
	TestEqual(TEXT("Zero dt cannot delay collision avoidance"), Recovery.Update(0.3, 0, 0.2, 0.2, false), 0.3);
	TestEqual(TEXT("Clearance flicker cannot restore distance"), Recovery.Update(1, 0.1, 0.2, 0.2, true), 0.3);
	Recovery.Update(1, 0.1, 0.2, 0.2, false);
	TestEqual(TEXT("Blocked frame resets hold"), Recovery.ClearTime, 0.0);
	Recovery.Update(1, 0.4, 0.2, 0.2, true);
	TestTrue(TEXT("One half-life after hold restores half the deficit"), FMath::IsNearlyEqual(Recovery.Fraction, 0.65, 0.00001));
	TestEqual(TEXT("New moving obstacle clamps immediately"), Recovery.Update(0.1, 1.0/120, 0.2, 0.2, false), 0.1);
	TestEqual(TEXT("Invalid sample is conservative"), Recovery.Update(std::numeric_limits<double>::quiet_NaN(), 1, 0.2, 0.2, true), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraRateTest, "Vowbound.Camera.Environment.FrameRateRecovery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraRateTest::RunTest(const FString& Parameters)
{
	using namespace VBEnvironmentCameraPolicy;
	for (int32 FPS : {30, 60, 120})
	{
		FRecovery Recovery;
		Recovery.Update(0.25, 0, 0.2, 0.2, false);
		for (int32 Frame = 0; Frame < FPS; ++Frame) Recovery.Update(1, 1.0/FPS, 0.2, 0.2, true);
		TestTrue(FString::Printf(TEXT("%dfps: one second includes four half-lives after hold"), FPS),
			FMath::IsNearlyEqual(Recovery.Fraction, 1.0 - 0.75/16, 0.00001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraGeometryTest, "Vowbound.Camera.Environment.WorldCollision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraGeometryTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Collision fixture world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Pawn = World->SpawnActor<ACharacter>();
	auto* PC = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Controller"), PC)) return false;
	PC->SetAsLocalPlayerController();
	// This isolated world never starts a game: create the camera normally supplied by PostInitializeComponents.
	if (!PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
		PC->PlayerCameraManager->InitializeFor(PC);
	}
	PC->Possess(Pawn);
	PC->SetViewTarget(Pawn);
	if (!TestTrue(TEXT("Fixture is locally possessed"), Pawn->GetController() == PC && PC->IsLocalController()) ||
		!TestTrue(TEXT("Fixture view target is the pawn"), PC->GetViewTarget() == Pawn)) return false;
	Pawn->SetActorLocation(FVector(0, 0, 90));
	UVBEnvironmentCameraConfig* Config = NewObject<UVBEnvironmentCameraConfig>();
	FVBEnvironmentCameraSolver Solver;
	FMinimalViewInfo Desired;
	Desired.Location = FVector(-300, 0, 120);
	Desired.Rotation = FRotator(-10, 5, 0);
	Desired.FOV = 90;
	FMinimalViewInfo Pose = Desired;
	Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	TestTrue(TEXT("Open space preserves desired position"), Pose.Location.Equals(Desired.Location, 0.01));

	auto* Wall = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Wall);
	Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(10, 200, 200));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Box->RegisterComponent();
	Wall->SetActorLocation(FVector(-150, 0, 100));
	Pose = Desired;
	Solver.Update(Pawn, Config, 0, false, Pose);
	TestTrue(TEXT("New wall immediately shortens the camera, even at zero dt"), Pose.Location.X > -140);
	TestTrue(TEXT("Environment correction preserves rotation"), Pose.Rotation.Equals(Desired.Rotation));
	TestEqual(TEXT("Environment correction preserves FOV"), Pose.FOV, Desired.FOV);
	const FVector Constrained = Pose.Location;
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pose = Desired;
	Solver.Update(Pawn, Config, 0.05f, false, Pose);
	TestTrue(TEXT("Wall removal does not immediately pop back"), FVector::Distance(Pose.Location, Desired.Location) > 50);
	for (int32 I=0; I<180; ++I) { Pose=Desired; Solver.Update(Pawn, Config, 1.f/60, false, Pose); }
	TestTrue(TEXT("Open space fully recovers, including final clearance margin"), Pose.Location.Equals(Desired.Location, 0.2));

	// A real short capsule under an overhead slab; desired standing-height camera is obstructed.
	Pawn->GetCapsuleComponent()->SetCapsuleSize(34, 44);
	Pawn->SetActorLocation(FVector(0,0,44));
	Box->SetBoxExtent(FVector(400,400,10));
	Wall->SetActorLocation(FVector(-150,0,110));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Pose = Desired;
	Solver.Update(Pawn, Config, 1.f/60, true, Pose);
	TestTrue(TEXT("Crouched anchor stays below slab"), Pose.Location.Z < 100);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VBCameraTest), false, Pawn);
	TestFalse(TEXT("Final camera minimum envelope is not overlapping slab"), World->OverlapBlockingTestByChannel(
		Pose.Location, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Config->MinimumProbeRadius), Query));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pose = Desired;
	Solver.Update(Pawn, Config, 0, true, Pose);
	TestTrue(TEXT("Camera cut resets prior constraint immediately"), Pose.Location.Equals(Desired.Location, 0.01));
	return true;
}

// 2026-10-07 user 증상 «벽에 닿은 뒤 캐릭터는 가는데 카메라가 그 자리에 서 있다가 한참 뒤 돌아온다» 의 재현.
//   직전 위치와 이번 위치가 둘 다 기준점에서 보이는데 그 사이 직선이 기둥 모서리를 지나면, 직전 위치 -> 이번 위치
//   재스윕이 카메라를 직전 위치에 세운다. 다음 프레임도 거기서 시작하니 길이 열릴 때까지 월드에 박힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraCornerTest, "Vowbound.Camera.Environment.CornerDoesNotPin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraCornerTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Collision fixture world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Pawn = World->SpawnActor<ACharacter>();
	auto* PC = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Controller"), PC)) return false;
	PC->SetAsLocalPlayerController();
	if (!PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager = World->SpawnActor<APlayerCameraManager>();
		PC->PlayerCameraManager->InitializeFor(PC);
	}
	PC->Possess(Pawn);
	PC->SetViewTarget(Pawn);
	if (!TestTrue(TEXT("Fixture is locally possessed"), Pawn->GetController() == PC && PC->IsLocalController())) return false;
	Pawn->SetActorLocation(FVector(0, 0, 90));

	// 기둥이 x,y 100~200 을 차지한다. 기준점(원점 근처)에서는 +X 쪽과 +Y 쪽이 다 보이지만 둘을 잇는 직선은 기둥을 지난다.
	auto* Pillar = World->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Pillar);
	Pillar->SetRootComponent(Box); Pillar->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(50, 50, 300));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Box->RegisterComponent();
	Pillar->SetActorLocation(FVector(150, 150, 100));

	UVBEnvironmentCameraConfig* Config = NewObject<UVBEnvironmentCameraConfig>();
	FVBEnvironmentCameraSolver Solver;
	FMinimalViewInfo Before;
	Before.Location = FVector(300, 0, 120);
	Before.Rotation = FRotator(-10, 180, 0);
	Before.FOV = 90;
	FMinimalViewInfo Pose = Before;
	Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	if (!TestTrue(TEXT("Control: the +X side is clear from the anchor"), Pose.Location.Equals(Before.Location, 0.01))) return false;

	FMinimalViewInfo After = Before;
	After.Location = FVector(0, 300, 120);
	After.Rotation = FRotator(-10, -90, 0);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VBCameraCornerTest), false, Pawn);
	FHitResult Hit;
	TestTrue(TEXT("Control: the straight path between the two sides crosses the pillar"), World->SweepSingleByChannel(
		Hit, Before.Location, After.Location, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Config->MinimumProbeRadius), Query));
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Pose = After;
		Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	}
	TestTrue(FString::Printf(TEXT("Clear desired camera is not held at the previous side (actual %s)"), *Pose.Location.ToString()),
		Pose.Location.Equals(After.Location, 0.2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraAssetsTest, "Vowbound.Camera.Environment.AuthoredAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraAssetsTest::RunTest(const FString& Parameters)
{
	UClass* CharacterClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/Vowbound/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C"));
	UClass* ControllerClass = LoadClass<APlayerController>(nullptr, TEXT("/Game/Vowbound/Blueprints/GameModes/BP_VBPlayerController.BP_VBPlayerController_C"));
	if (!TestNotNull(TEXT("Character class"), CharacterClass) || !TestNotNull(TEXT("Controller class"), ControllerClass)) return false;
	auto* Pawn = Cast<ACharacter>(CharacterClass->GetDefaultObject());
	auto* PC = Cast<APlayerController>(ControllerClass->GetDefaultObject());
	TestTrue(TEXT("Actual controller uses final camera correction"), PC->PlayerCameraManagerClass->IsChildOf(AVBPlayerCameraManager::StaticClass()));
	auto* Component = Pawn->FindComponentByClass<UVBEnvironmentCameraComponent>();
	auto* Camera = Pawn->FindComponentByClass<UGameplayCameraComponent>();
	if (!TestNotNull(TEXT("Environment component"), Component) || !TestNotNull(TEXT("Gameplay camera"), Camera)) return false;
	TestNotNull(TEXT("Authored settings"), Component->Config.Get());
	if (Camera->CameraReference.IsValid()) TestEqual(TEXT("Project-owned camera is connected"),
		Camera->CameraReference.GetCameraAsset()->GetPathName(), FString(TEXT("/Game/Vowbound/Blueprints/Cameras/CameraAsset_VBCharacter.CameraAsset_VBCharacter")));
	else AddError(TEXT("Gameplay camera has no asset"));
	return true;
}
#endif
