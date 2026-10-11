#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBGA_LightAttack.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Animation/VBANS_ComboWindow.h"
#include "Animation/VBAnimInstance.h"
#include "Animation/VBMotionWarpingNames.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBGASPLocomotionStateLibrary.h"
#include "Animation/VBGASPLocomotionUpdateLibrary.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimClassInterface.h"
#include "AnimNodes/AnimNode_Slot.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Chooser.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBMovementConfig.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/RootMotionSource.h"
#include "HAL/IConsoleManager.h"
#include "MotionWarpingComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatComboWindowOwnershipTest, "Vowbound.Combat.Recovery.ComboWindowOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBCombatComboWindowOwnershipTest::RunTest(const FString& Parameters)
{
	UClass* AbilityClass = LoadClass<UVBGA_LightAttack>(nullptr, TEXT("/Game/Vowbound/GAS/Abilities/BP_GA_LightAttack.BP_GA_LightAttack_C"));
	if (!TestNotNull(TEXT("Concrete player light attack ability"), AbilityClass)) return false;
	auto* Ability = NewObject<UVBGA_LightAttack>(GetTransientPackage(), AbilityClass);
	Ability->ActiveConfig = NewObject<UVBAttackConfig>();
	Ability->ActiveConfig->bAutoChainCombo = false;
	Ability->ActiveConfig->bCancelRecoveryAtWindowClose = false;
	auto* OldWindow = NewObject<UVBANS_ComboWindow>();
	auto* NewWindow = NewObject<UVBANS_ComboWindow>();
	FGameplayEventData Event; Event.OptionalObject = OldWindow;
	Ability->OnComboWindowOpen(Event);
	Event.OptionalObject = NewWindow; Ability->OnComboWindowOpen(Event);
	Ability->bSaveAttack = true;
	Event.OptionalObject = OldWindow; Event.EventMagnitude = 1;
	Ability->OnComboWindowClose(Event);
	TestTrue(TEXT("Late end of previous swing cannot close the new combo window"), Ability->bComboWindowOpen);
	TestTrue(TEXT("Late end cannot consume the next swing's buffered click"), Ability->bSaveAttack);
	Event.OptionalObject = NewWindow; Event.EventMagnitude = 0;
	Ability->OnComboWindowClose(Event);
	TestFalse(TEXT("Interrupted current window closes"), Ability->bComboWindowOpen);
	TestTrue(TEXT("Interrupted window cannot jump a section"), Ability->bSaveAttack);
	Ability->bSaveAttack = false; Ability->OnComboWindowOpen(Event);
	Event.EventMagnitude = 1; Ability->OnComboWindowClose(Event);
	TestFalse(TEXT("Natural end closes its own window"), Ability->bComboWindowOpen);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceWorldCollisionTest, "Vowbound.Combat.MeleeTrace.WorldCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceWorldCollisionTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Trace physics world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AActor* Owner = World->SpawnActor<AActor>();
	auto* Root = NewObject<USceneComponent>(Owner); Owner->SetRootComponent(Root); Root->RegisterComponent();
	auto* Trace = NewObject<UVBMeleeTraceComponent>(Owner); Trace->RegisterComponent();
	auto MakeBox = [&](FVector Position, ECollisionChannel Channel, ECollisionResponse Response)
	{
		auto* Actor = World->SpawnActor<AActor>(); auto* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(2, 2, 20)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(Channel, Response);
		Box->RegisterComponent(); Actor->SetActorLocation(Position); return Box;
	};
	UBoxComponent* Target = MakeBox(FVector(500, 0, 50), ECC_GameTraceChannel3, ECR_Overlap);
	UVBMeleeTraceComponent::FWindow Window;
	Window.Shape.Bone = TEXT("root"); Window.Shape.TipLocal = FVector(500, 0, 0); Window.Shape.Radius = 5;
	Window.PrevBone = FTransform(FVector(0, 0, 50)); Window.bHasPrev = true;
	const FTransform HalfTurn(FQuat(FVector::UpVector, PI), FVector(0, 0, 50));
	TArray<FHitResult> Hits;
	Trace->GatherWindowHits(Window, HalfTurn, 1.f / 15, Hits);
	TestEqual(TEXT("Captured first pose remains hittable even when rotation sampling reaches its cap"), Hits.Num(), 1);
	if (Hits.Num()) TestTrue(TEXT("Overlap becomes a gameplay contact"), Hits[0].bBlockingHit);
	Target->GetOwner()->SetActorLocation(FVector(-500, 0, 50)); Window.LastHitTime.Reset(); Hits.Reset();
	Trace->GatherWindowHits(Window, HalfTurn, 1.f / 15, Hits);
	TestEqual(TEXT("Captured final pose is queried"), Hits.Num(), 1);
	Hits.Reset(); Trace->GatherWindowHits(Window, HalfTurn, 1.f / 60, Hits);
	TestEqual(TEXT("A stationary window does not damage the same actor twice"), Hits.Num(), 0);
	Window.RehitInterval = 0.2f; Window.Elapsed += 0.21f;
	Hits.Reset(); Trace->GatherWindowHits(Window, HalfTurn, 0, Hits);
	TestEqual(TEXT("Authored multihit interval can rehit"), Hits.Num(), 1);
	UBoxComponent* Wall = MakeBox(FVector(-250, 0, 50), ECC_Visibility, ECR_Block);
	Wall->SetBoxExtent(FVector(5, 40, 100)); Window.LastHitTime.Reset(); Hits.Reset();
	Trace->GatherWindowHits(Window, HalfTurn, 0, Hits);
	TestEqual(TEXT("A wall between attacker and contact rejects damage"), Hits.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatAnimationRecoveryTest, "Vowbound.Combat.Recovery.AnimationIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBCombatAnimationRecoveryTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Animation fixture world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACharacter* Character = World->SpawnActor<ACharacter>();
	auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	auto* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Vowbound/Animations/Montage/AM_BigSword_LightAttack.AM_BigSword_LightAttack"));
	if (!TestNotNull(TEXT("Player skeleton"), Mesh) || !TestNotNull(TEXT("Attack montage"), Montage)) return false;
	Character->GetMesh()->SetSkeletalMeshAsset(Mesh); Character->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	auto* Anim = Cast<UVBAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("Actual native anim instance"), Anim)) return false;
	auto* ASC = NewObject<UAbilitySystemComponent>(Character); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Character, Character);
	auto* CMC = Character->GetCharacterMovement(); CMC->SetMovementMode(MOVE_Walking); CMC->Velocity = FVector(300, 0, 0);
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	TestTrue(TEXT("Authored attack montage can play"), Anim->Montage_Play(Montage) > 0);
	TestFalse(TEXT("Other root motion is not an attack"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
	TestTrue(TEXT("Grounded attack root motion without input is separate from locomotion"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	Anim->bSuppressAttackLocomotion = true; Anim->UpdateEssentialValues(1.f / 60, Character, CMC);
	TestEqual(TEXT("Locomotion speed is zero while the actual capsule retains attack speed"), Anim->Speed2D, 0.0f);
	TestEqual(TEXT("Attack lunge is preserved"), CMC->Velocity.X, 300.0);
	FTransformTrajectory History, Generated; float Yaw = 0;
	UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, FPoseSearchTrajectoryData(), 1.f / 60, true, History, Yaw, Generated);
	FVector Future;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Generated, 0.4f, 0.5f, Future, false);
	TestTrue(TEXT("Future trajectory cannot request a running recovery"), Future.IsNearlyZero());
	TestEqual(TEXT("GASP sampling contract is retained"), Generated.Samples.Num(), 46);
	CMC->SetMovementMode(MOVE_Falling);
	TestFalse(TEXT("Airborne movement is preserved"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	CMC->SetMovementMode(MOVE_Walking);
	const auto* Accel = FindFProperty<FStructProperty>(UCharacterMovementComponent::StaticClass(), TEXT("Acceleration"));
	if (!TestNotNull(TEXT("CMC input acceleration fixture"), Accel)) return false;
	*Accel->ContainerPtrToValuePtr<FVector>(CMC) = FVector(100, 0, 0);
	TestFalse(TEXT("Movement input immediately resumes locomotion"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	Anim->bIsSprinting = true; Anim->LocomotionState = EVBLocomotionState::ArmedCombat; Anim->bIsLockedOn = true;
	Anim->UpdateStates(); TestTrue(TEXT("Armed sprint follows CMC free rotation even while locked on"), Anim->RotationMode == EVBRotationMode::OrientToMovement);
	Anim->bIsSprinting = false; Anim->UpdateStates(); TestTrue(TEXT("Lock-on returns after sprint release"), Anim->RotationMode == EVBRotationMode::OrientToTarget);
	Anim->bIsLockedOn = false; Anim->UpdateStates(); TestTrue(TEXT("Armed jog remains strafe"), Anim->RotationMode == EVBRotationMode::Strafe);
	Anim->RotationMode_LastFrame = EVBRotationMode::OrientToMovement;
	Anim->MovementMode = Anim->MovementMode_LastFrame = EVBMovementMode::Grounded;
	Anim->MMState = Anim->MMState_LastFrame = EVBMMState::Moving;
	Anim->Gait = Anim->Gait_LastFrame; Anim->Stance = Anim->Stance_LastFrame;
	TestTrue(TEXT("Rotation mode handoff refreshes the database search"), Anim->GetMMInterruptMode() == EPoseSearchInterruptMode::InterruptOnDatabaseChange);
	*Accel->ContainerPtrToValuePtr<FVector>(CMC) = FVector::ZeroVector;
	FAnimMontageInstance* Instance = Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("Root-motion instance before recovery"), Instance)) return false;
	Instance->UpdateWeight(0.1f);
	Anim->Montage_Stop(0.25f, Montage);
	TestNull(TEXT("Engine clears root-motion owner at blend-out start"), Character->GetRootMotionAnimMontageInstance());
	TestTrue(TEXT("Attack intent remains idle throughout weighted recovery"), UVBAnimInstance::ShouldSuppressAttackLocomotion(Character));
	AVBCharacter* RecoveryCharacter = World->SpawnActor<AVBCharacter>();
	RecoveryCharacter->GetMesh()->SetSkeletalMeshAsset(Mesh);
	RecoveryCharacter->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	auto* RecoveryAnim = RecoveryCharacter->GetMesh()->GetAnimInstance();
	RecoveryCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (!TestTrue(TEXT("Recovery montage can play"), RecoveryAnim->Montage_Play(Montage) > 0)) return false;
	RecoveryAnim->GetActiveInstanceForMontage(Montage)->UpdateWeight(0.1f);
	RecoveryAnim->Montage_Stop(0.25f, Montage);
	RecoveryCharacter->StopResidualAttackMotion(0.03f, Montage);
	TestEqual(TEXT("Blending instance suppresses the last extracted attack translation"), RecoveryCharacter->GetAnimRootMotionTranslationScale(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRecoveryAssetsTest, "Vowbound.Combat.Recovery.AuthoredAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBCombatRecoveryAssetsTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Weapon : {TEXT("BigSword"), TEXT("Katana"), TEXT("Fighter")})
	{
		const FString Path = FString::Printf(TEXT("/Game/Vowbound/Data/DA_Attack_%s_Light.DA_Attack_%s_Light"), Weapon, Weapon);
		const auto* Config = LoadObject<UVBAttackConfig>(nullptr, *Path);
		if (!TestNotNull(Weapon, Config)) continue;
		TestFalse(TEXT("A single click retains authored recovery"), Config->bCancelRecoveryAtWindowClose);
		const UAnimMontage* Montage = Config->AttackMontage;
		if (!TestNotNull(TEXT("Attack montage"), Montage)) continue;
		for (const FCompositeSection& Section : Montage->CompositeSections)
			TestTrue(TEXT("No montage section automatically skips into another swing"), Section.NextSectionName.IsNone());
	}
	UClass* Class = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Vowbound/Blueprints/Characters/ABP_VBCharacter_58.ABP_VBCharacter_58_C"));
	if (!TestNotNull(TEXT("Actual player AnimBP"), Class)) return false;
	const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(Class);
	bool bFound = false;
	for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
		if (Property->Struct == FAnimNode_Slot::StaticStruct())
		{
			const auto* Slot = Property->ContainerPtrToValuePtr<FAnimNode_Slot>(Class->GetDefaultObject());
			if (Slot->SlotName == TEXT("DefaultSlot"))
			{
				bFound = true; TestTrue(TEXT("Locomotion keeps updating while the attack owns the full pose"), Slot->bAlwaysUpdateSourcePose);
			}
		}
	TestTrue(TEXT("Full body attack slot is wired"), bFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMouseLookTargetFacingTest, "Vowbound.Animation.CMC.MouseLookTargetFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMouseLookTargetFacingTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Mouse-look movement fixture world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	auto* Config = LoadObject<UVBMovementConfig>(nullptr, TEXT("/Game/Vowbound/Data/DA_Movement_Default.DA_Movement_Default"));
	auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	auto* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/Vowbound/Animations/Montage/AM_BigSword_LightAttack.AM_BigSword_LightAttack"));
	if (!TestNotNull(TEXT("Authored movement config"), Config) || !TestNotNull(TEXT("Player skeleton"), Mesh)
		|| !TestNotNull(TEXT("Authored root-motion attack"), Montage)) return false;
	TestTrue(TEXT("GASP grounded target facing uses authored instant rotation"), Config->DefaultRotationRate.Yaw < 0.0);
	TestTrue(TEXT("Armed idle retains a finite authored rotation rate"), Config->ArmedGroundedRotationRate.Yaw > 0.0);
	IConsoleVariable* FacingFromMesh = IConsoleManager::Get().FindConsoleVariable(TEXT("a.PoseSearchTrajectory.FacingFromMesh"));
	if (!TestNotNull(TEXT("Actual trajectory facing policy"), FacingFromMesh)) return false;
	TestEqual(TEXT("Production trajectory starts from mesh facing"), FacingFromMesh->GetInt(), 1);

	const auto* ConfigProperty = FindFProperty<FObjectPropertyBase>(AVBCharacter::StaticClass(), TEXT("MovementConfig"));
	const auto* AccelerationProperty = FindFProperty<FStructProperty>(UCharacterMovementComponent::StaticClass(), TEXT("Acceleration"));
	const auto* AnalogProperty = FindFProperty<FFloatProperty>(UCharacterMovementComponent::StaticClass(), TEXT("AnalogInputModifier"));
	const auto* StateProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("LocomotionState"));
	const auto* WeaponProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("CurrentWeaponType"));
	if (!TestNotNull(TEXT("Character config fixture property"), ConfigProperty)
		|| !TestNotNull(TEXT("CMC acceleration fixture property"), AccelerationProperty)
		|| !TestNotNull(TEXT("CMC analog input fixture property"), AnalogProperty)
		|| !TestNotNull(TEXT("Weapon state fixture property"), StateProperty)
		|| !TestNotNull(TEXT("Weapon type fixture property"), WeaponProperty)) return false;
	AVBCharacter* Character = World->SpawnActor<AVBCharacter>();
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Player fixture"), Character) || !TestNotNull(TEXT("Controller fixture"), Controller)) return false;
	// 컨트롤러 참조만 연결한다. Possess를 호출하면 게임 재시작과 카메라 설정까지 실행된다.
	Character->SetController(Controller);
	ConfigProperty->SetObjectPropertyValue_InContainer(Character, Config);
	Character->GetMesh()->SetSkeletalMeshAsset(Mesh);
	Character->GetMesh()->SetAnimInstanceClass(UVBAnimInstance::StaticClass());
	auto* Anim = Cast<UVBAnimInstance>(Character->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("Native animation route fixture"), Anim)) return false;
	Anim->bUseGASPLocomotion = true;
	auto* Weapons = Character->GetWeaponStateComponent();
	if (!TestNotNull(TEXT("Weapon state fixture"), Weapons)) return false;
	StateProperty->GetUnderlyingProperty()->SetIntPropertyValue(StateProperty->ContainerPtrToValuePtr<void>(Weapons),
		static_cast<uint64>(EVBLocomotionState::ArmedCombat));
	Character->ApplyRotationMode();
	auto* CMC = Character->GetCharacterMovement();
	CMC->SetMovementMode(MOVE_Walking);
	CMC->MaxAcceleration = Config->MaxAcceleration;
	CMC->GroundFriction = Config->GroundFriction;
	CMC->BrakingDecelerationWalking = Config->BrakingDecelerationWalking;
	AnalogProperty->SetPropertyValue_InContainer(CMC, 1.0f);
	constexpr float FrameSeconds = 1.f / 60;
	auto SetAcceleration = [&](const FVector& Value)
	{
		*AccelerationProperty->ContainerPtrToValuePtr<FVector>(CMC) = Value;
	};
	auto ResetMouseTurn = [&]
	{
		Character->SetActorRotation(FRotator::ZeroRotator);
		Controller->SetControlRotation(FRotator(0, 90, 0));
	};
	for (EVBWeaponType Weapon : { EVBWeaponType::BigSword, EVBWeaponType::Katana, EVBWeaponType::Fighter })
	{
		WeaponProperty->GetUnderlyingProperty()->SetIntPropertyValue(WeaponProperty->ContainerPtrToValuePtr<void>(Weapons), static_cast<uint64>(Weapon));
		CMC->MaxWalkSpeed = Config->ResolveProfile(Weapon, true).RunSpeeds.X;
		ResetMouseTurn();
		SetAcceleration(FVector(0, Config->MaxAcceleration, 0)); // W만 유지한 채 새 카메라 방향으로 이동한다.
		CMC->Velocity = FVector(CMC->MaxWalkSpeed, 0, 0); // 회전 직전의 실제 속도 관성을 유지한다.
		CMC->RotationRate = Config->ArmedGroundedRotationRate;
		CMC->PhysicsRotation(FrameSeconds);
		TestTrue(TEXT("The first moving physics frame updates its target-facing rate"), CMC->RotationRate.Equals(Config->DefaultRotationRate));
		TestTrue(TEXT("Armed W plus mouse aligns the capsule target this frame"), FMath::Abs(Character->GetActorRotation().Yaw - 90.0) < 0.01);
		FPoseSearchTrajectoryData Data;
		Data.RotateTowardsMovementSpeed = Data.MaxControllerYawRate = Data.BendVelocityTowardsAcceleration = 0;
		FTransformTrajectory History, Generated;
		float LastYaw = 90;
		UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, Data, FrameSeconds, false, History, LastYaw, Generated);
		FVector Future;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(Generated, 0.4f, 0.5f, Future, false);
		const FVector LocalFuture = Character->GetActorRotation().UnrotateVector(Future);
		const double FutureYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalFuture.Y, LocalFuture.X));
		TestTrue(FString::Printf(TEXT("Weapon %d future query stays forward after mouse-only steering (yaw %.2f)"), static_cast<int32>(Weapon), FutureYaw),
			Future.Size2D() > 1.0 && FMath::Abs(FutureYaw) < 15.0);
		FTransformTrajectorySample Sample;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleAtTime(Generated, 0.5f, Sample, false);
		const FQuat PredictedActorFacing = UPoseSearchTrajectoryLibrary::GetTransformTrajectorySampleTransform(Sample).GetRotation()
			* Character->GetMesh()->GetRelativeRotation().Quaternion().Inverse();
		TestTrue(TEXT("Zero-rate GASP prediction retains the aligned actor-facing reference"),
			FMath::Abs(FRotator::NormalizeAxis(PredictedActorFacing.Rotator().Yaw - 90.0)) < 0.01);
	}
	for (float WorldXSign : { -1.f, 1.f })
	{
		// 카메라 Yaw=90일 때 실제 D는 월드 -X, A는 월드 +X 방향이다.
		Character->SetActorRotation(FRotator(0, 90, 0));
		Controller->SetControlRotation(FRotator(0, 90, 0));
		SetAcceleration(FVector(WorldXSign * Config->MaxAcceleration, 0, 0));
		CMC->Velocity = FVector(WorldXSign * CMC->MaxWalkSpeed, 0, 0);
		CMC->PhysicsRotation(FrameSeconds);
		TestTrue(TEXT("Actual lateral input preserves camera-facing strafe"), FMath::Abs(Character->GetActorRotation().Yaw - 90.0) < 0.01);
		FPoseSearchTrajectoryData StrafeData;
		StrafeData.RotateTowardsMovementSpeed = StrafeData.MaxControllerYawRate = StrafeData.BendVelocityTowardsAcceleration = 0;
		FTransformTrajectory StrafeHistory, StrafeGenerated;
		float StrafeLastYaw = 90;
		UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, StrafeData, FrameSeconds, false, StrafeHistory, StrafeLastYaw, StrafeGenerated);
		FVector StrafeFuture;
		UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(StrafeGenerated, 0.4f, 0.5f, StrafeFuture, false);
		const FVector LocalStrafe = Character->GetActorRotation().UnrotateVector(StrafeFuture);
		const double StrafeYaw = FMath::RadiansToDegrees(FMath::Atan2(LocalStrafe.Y, LocalStrafe.X));
		const double ExpectedYaw = -WorldXSign * 90.0;
		TestTrue(FString::Printf(TEXT("Actual %s input keeps its lateral future query (yaw %.2f)"), WorldXSign < 0 ? TEXT("D") : TEXT("A"), StrafeYaw),
			StrafeFuture.Size2D() > 1.0 && FMath::Abs(FRotator::NormalizeAxis(StrafeYaw - ExpectedYaw)) < 15.0);
	}

	Anim->bUseGASPLocomotion = false;
	SetAcceleration(FVector(0, Config->MaxAcceleration, 0));
	CMC->Velocity = FVector(CMC->MaxWalkSpeed, 0, 0);
	ResetMouseTurn(); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Legacy animation retains the armed finite rate"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	TestTrue(TEXT("Legacy physics rotation still advances by its finite frame allowance"),
		FMath::Abs(Character->GetActorRotation().Yaw - FMath::Min(Config->ArmedGroundedRotationRate.Yaw * FrameSeconds, 90.0)) < 0.01);
	FPoseSearchTrajectoryData LegacyData;
	LegacyData.RotateTowardsMovementSpeed = LegacyData.MaxControllerYawRate = LegacyData.BendVelocityTowardsAcceleration = 0;
	FTransformTrajectory LegacyHistory, LegacyGenerated;
	float LegacyLastYaw = 90;
	UVBGASPLocomotionUpdateLibrary::GenerateLocomotionTrajectory(Anim, LegacyData, FrameSeconds, false, LegacyHistory, LegacyLastYaw, LegacyGenerated);
	FVector LegacyFuture;
	UPoseSearchTrajectoryLibrary::GetTransformTrajectoryVelocity(LegacyGenerated, 0.4f, 0.5f, LegacyFuture, false);
	const FVector LegacyLocalFuture = Character->GetActorRotation().UnrotateVector(LegacyFuture);
	const double LegacyFutureYaw = FMath::RadiansToDegrees(FMath::Atan2(LegacyLocalFuture.Y, LegacyLocalFuture.X));
	// 같은 입력·예측 설정에서 지연된 캡슐 기준은 미래 이동을 측면 방향으로 읽는다.
	TestTrue(FString::Printf(TEXT("Finite facing preserves the prior lateral future-query counterexample (yaw %.2f)"), LegacyFutureYaw),
		LegacyFuture.Size2D() > 1.0 && FMath::Abs(LegacyFutureYaw) > 60.0);
	Anim->bUseGASPLocomotion = true;
	SetAcceleration(FVector::ZeroVector); CMC->Velocity = FVector(250, 0, 0);
	CMC->RotationRate = Config->DefaultRotationRate;
	ResetMouseTurn(); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("The first released-input frame restores armed idle rate despite residual velocity"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	SetAcceleration(FVector(0, Config->MaxAcceleration, 0));
	CMC->SetMovementMode(MOVE_Falling); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Airborne configured rotation is preserved"), CMC->RotationRate.Equals(Config->AirborneRotationRate));
	CMC->SetMovementMode(MOVE_Flying); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Flying does not enter the grounded target-facing override"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	CMC->SetMovementMode(MOVE_Walking);
	auto* Warping = Character->GetMotionWarpingComponent();
	if (!TestNotNull(TEXT("Motion-warping ownership fixture"), Warping)) return false;
	Warping->AddOrUpdateWarpTargetFromTransform(VBMotionWarpingNames::AttackTarget, FTransform(FVector(100, 0, 0)));
	CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Feature-owned facing retains its prior rotation policy"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	Warping->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	auto Source = MakeShared<FRootMotionSource_ConstantForce>();
	Source->InstanceName = TEXT("MouseLookTestRootMotion"); Source->Duration = 1; Source->StartTime = 0; Source->Force = FVector(100, 0, 0);
	CMC->ApplyRootMotionSource(Source); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Root-motion sources retain their prior rotation policy"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	CMC->CurrentRootMotion.Clear();
	Anim->SetRootMotionMode(ERootMotionMode::RootMotionFromMontagesOnly);
	if (!TestTrue(TEXT("Attack ownership fixture can play"), Anim->Montage_Play(Montage) > 0)) return false;
	FAnimMontageInstance* Attack = Anim->GetActiveInstanceForMontage(Montage);
	if (!TestNotNull(TEXT("Active root-motion attack fixture"), Attack)) return false;
	Attack->UpdateWeight(0.1f); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Active attack does not gain locomotion's instant rotation"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));
	Anim->Montage_Stop(0.25f, Montage); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("Weighted attack blend-out still keeps its existing rotation policy"), CMC->RotationRate.Equals(Config->ArmedGroundedRotationRate));

	UClass* AnimClass = LoadClass<UVBAnimInstance>(nullptr, TEXT("/Game/Vowbound/Blueprints/Characters/ABP_VBCharacter_58.ABP_VBCharacter_58_C"));
	if (!TestNotNull(TEXT("Actual player animation class fallback"), AnimClass)) return false;
	TestTrue(TEXT("Actual animation class declares GASP ownership"), AnimClass->GetDefaultObject<UVBAnimInstance>()->bUseGASPLocomotion);
	// 그래프 전체를 초기화하지 않고 AnimClass만 존재하며 실행 인스턴스가 없는 경계를 만든다.
	Character->GetMesh()->ClearAnimScriptInstance();
	Character->GetMesh()->AnimClass = AnimClass;
	TestNull(TEXT("Class fallback fixture has no live animation instance"), Character->GetMesh()->GetAnimInstance());
	CMC->RotationRate = Config->ArmedGroundedRotationRate;
	ResetMouseTurn(); CMC->PhysicsRotation(FrameSeconds);
	TestTrue(TEXT("AnimClass defaults retain the moving policy without an animation tick"), CMC->RotationRate.Equals(Config->DefaultRotationRate));
	TestTrue(TEXT("Class fallback also aligns this physics frame"), FMath::Abs(Character->GetActorRotation().Yaw - 90.0) < 0.01);
	return true;
}
namespace
{
	struct FVBActualWeaponIdleFixture
	{
		FAutomationTestBase* Test = nullptr;
		UWorld* World = nullptr;
		AVBCharacter* Character = nullptr;
		UVBAnimInstance* Main = nullptr;
		const FEnumProperty* WeaponProperty = nullptr;
		const FEnumProperty* LocomotionProperty = nullptr;
		int32 WarmFrames = 0;
		int32 HandoffFrames = 0;
		uint64 LastFrame = MAX_uint64;

		~FVBActualWeaponIdleFixture()
		{
			if (World)
			{
				if (GEngine) GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
		void SetWeaponState(EVBWeaponType Weapon, EVBLocomotionState Locomotion) const
		{
			UVBWeaponStateComponent* Weapons = Character->GetWeaponStateComponent();
			WeaponProperty->GetUnderlyingProperty()->SetIntPropertyValue(WeaponProperty->ContainerPtrToValuePtr<void>(Weapons), static_cast<uint64>(Weapon));
			LocomotionProperty->GetUnderlyingProperty()->SetIntPropertyValue(LocomotionProperty->ContainerPtrToValuePtr<void>(Weapons), static_cast<uint64>(Locomotion));
		}
		void TickPose() const
		{
			constexpr float FrameSeconds = 1.f / 60;
			// 시간만 진행하고 이동 physics/BeginPlay는 실행하지 않는다. 실제 AnimGraph는 평가한다.
			World->Tick(LEVELTICK_TimeOnly, FrameSeconds);
			Character->GetMesh()->TickAnimation(FrameSeconds, false);
			Character->GetMesh()->RefreshBoneTransforms();
		}
	};

	class FVBActualWeaponIdleWarmHistoryCommand : public IAutomationLatentCommand
	{
	public:
		explicit FVBActualWeaponIdleWarmHistoryCommand(TSharedPtr<FVBActualWeaponIdleFixture> InFixture) : Fixture(MoveTemp(InFixture)) {}
		virtual bool Update() override
		{
			FVBActualWeaponIdleFixture& F = *Fixture;
			// linked thread-safe update는 GFrameCounter별 1회다. 단일 RunTest 내 반복으로 데운 척하지 않는다.
			if (F.LastFrame == GFrameCounter) return false;
			F.LastFrame = GFrameCounter;
			F.TickPose();
			if (F.WarmFrames < 120 && ++F.WarmFrames < 120) return false;
			UAnimInstance* Linked = F.Main->GetLinkedAnimGraphInstanceByTag(TEXT("GASPLocomotion"));
			if (!F.Test->TestNotNull(TEXT("Actual running linked GASP graph"), Linked)) return true;
			const auto* StateProperty = FindFProperty<FStructProperty>(Linked->GetClass(), TEXT("CMCState"));
			if (!F.Test->TestNotNull(TEXT("Actual linked native CMC state"), StateProperty)
				|| !F.Test->TestTrue(TEXT("Linked state has the production CMC struct"), StateProperty->Struct == FVBGASPCMCState::StaticStruct())) return true;
			auto& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(Linked);
			if (F.HandoffFrames == 0)
			{
				const FPoseHistoryReference History = UVBGASPLocomotionStateLibrary::Get_PoseHistoryReference(Linked);
				const int32 Entries = History.PoseHistory.IsValid() ? History.PoseHistory->GetNumEntries() : 0;
				F.Test->AddInfo(FString::Printf(TEXT("Warm None: history_entries=%d route=%d pool=%d failed=%d has_selection=%d asset=%s"),
					Entries, State.bUsesWeaponLocomotion, State.CapturedWeaponPool, State.bSelectionContractFailed,
					State.bHasSelection, *GetNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Linked).Anim)));
				if (!F.Test->TestTrue(TEXT("Actual PoseHistory collector was warmed by graph evaluation"), Entries >= 2)
					|| !F.Test->TestTrue(TEXT("None pool is on the weapon chooser route"), State.bUsesWeaponLocomotion)
					|| !F.Test->TestFalse(TEXT("None idle lifecycle remains valid before weapon handoff"), State.bSelectionContractFailed)) return true;
				F.SetWeaponState(EVBWeaponType::Katana, EVBLocomotionState::ArmedCombat);
				F.Test->TestTrue(TEXT("Actual weapon data selects Katana's authored pool"),
					F.Character->GetWeaponStateComponent()->GetMMPoolForState(EVBLocomotionState::ArmedCombat) == EVBWeaponType::Katana);
				// 부모 값과 linked GT snapshot만 갱신해 이전 실제 재생 샘플로 첫 warm 검색을 분리한다.
				F.Main->NativeUpdateAnimation(1.f / 60);
				const auto* CollisionProperty = FindFProperty<FStructProperty>(F.Main->GetClass(), TEXT("TrajectoryCollision"));
				if (!F.Test->TestNotNull(TEXT("Original raw collision fixture field"), CollisionProperty)) return true;
				auto& Collision = *CollisionProperty->ContainerPtrToValuePtr<FPoseSearchTrajectory_WorldCollisionResults>(F.Main);
				const float GeneratedCollisionTime = Collision.TimeToLand;
				// 지상 Chooser의 TimeToLand()=0과 달리 collision utility에는 양의 샘플 시간이 남을 수 있다.
				// world/floor별 utility 결과에 의존하지 않도록 이 snapshot 입력 차이를 고정한다.
				Collision.TimeToLand = 0.06f;
				UVBGASPLocomotionUpdateLibrary::Update_PropertiesFromCharacter(Linked);
				const TArray<UPoseSearchDatabase*> OriginalDatabases = F.Main->GetAllActivePoseSearchDBs();
				const bool bOriginalIdle = OriginalDatabases.ContainsByPredicate([](const UPoseSearchDatabase* Database)
				{
					return Database && Database->GetFName() == TEXT("PSD_VB_Katana_Stand_Idles");
				});
				F.Test->AddInfo(FString::Printf(TEXT("Grounded TimeToLand contract: main_helper=%.9g generated_collision=%.9g representative_collision=%.9g captured_query=%.9g original_databases=%d original_katana_idle=%d"),
					F.Main->TimeToLand(), GeneratedCollisionTime, Collision.TimeToLand,
					State.WeaponQuery.TimeToLand, OriginalDatabases.Num(), bOriginalIdle));
				F.Test->TestEqual(TEXT("Original grounded chooser's TimeToLand function returns zero"), F.Main->TimeToLand(), 0.0f);
				F.Test->TestTrue(TEXT("Original player chooser admits Katana idle for the same grounded player"), bOriginalIdle);
				F.Test->AddInfo(FString::Printf(TEXT("TimeToLand snapshot=%g original_chooser_function=%g"),
					State.WeaponQuery.TimeToLand, F.Main->TimeToLand()));
				F.Test->TestEqual(TEXT("CMC snapshot preserves the original TimeToLand function semantics"),
					State.WeaponQuery.TimeToLand, F.Main->TimeToLand());
				F.Test->TestEqual(TEXT("CMC snapshot separately preserves raw collision prediction"),
					State.WeaponQuery.TrajectoryTimeToLand, Collision.TimeToLand);
				UAnimationAsset* Animation = nullptr;
				FVBGASPCMCChooserOutput Output;
				const bool bEvaluated = UVBGASPLocomotionStateLibrary::EvaluateCMCAnimation(Linked,
					State.SelectionContext.Get(), State.ActiveChooser.Get(), Animation, Output);
				F.Test->AddInfo(FString::Printf(TEXT("Warm Katana query: history=%d phase=%d phase_data=%d candidate_chooser=%d phase_candidates=%d allowed=%d cost=%.9g source=%s animation=%s evaluated=%d valid=%d"),
					Output.bHistoryReady, static_cast<int32>(Output.Phase), Output.bPhaseDataValid,
					Output.bCandidateChooserEvaluated, Output.bHasPhaseCandidates, Output.AllowedDatabases.Num(),
					Output.SearchCost, *GetPathNameSafe(Output.SourceDatabase.Get()), *GetPathNameSafe(Animation), bEvaluated,
					UVBGASPLocomotionStateLibrary::IsValidCMCSelection(Animation, Output)));
				F.Test->TestTrue(TEXT("Warm weapon query has a valid context contract"), bEvaluated);
				F.Test->TestTrue(TEXT("Standing Katana loop has an admitted phase candidate"), Output.bHasPhaseCandidates);
				F.Test->TestTrue(TEXT("Admitted warm Katana candidate produces a valid PoseSearch result"),
					UVBGASPLocomotionStateLibrary::IsValidCMCSelection(Animation, Output));
				F.HandoffFrames = 1;
				return false;
			}
			if (++F.HandoffFrames < 3) return false;
			F.Test->AddInfo(FString::Printf(TEXT("Katana handoff lifecycle: pool=%d failed=%d waiting=%d selected_pool=%d selected=%s"),
				State.CapturedWeaponPool, State.bSelectionContractFailed, State.bWaitingForPhase,
				State.SelectedWeaponPool, *GetPathNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Linked).Anim)));
			F.Test->TestFalse(TEXT("A warmed None to Katana idle handoff does not permanently disable CMC"), State.bSelectionContractFailed);
			// Linked GT snapshot precedes Main update on a real landing frame. Keep
			// physics grounded while reproducing the previous Main airborne cache.
			const auto* ModeProperty = FindFProperty<FEnumProperty>(F.Main->GetClass(), TEXT("MovementMode"));
			const auto* VelocityProperty = FindFProperty<FStructProperty>(F.Main->GetClass(), TEXT("Velocity"));
			if (!F.Test->TestNotNull(TEXT("Landing fixture cached movement mode"), ModeProperty)
				|| !F.Test->TestNotNull(TEXT("Landing fixture cached velocity"), VelocityProperty)) return true;
			ModeProperty->GetUnderlyingProperty()->SetIntPropertyValue(ModeProperty->ContainerPtrToValuePtr<void>(F.Main),
				static_cast<uint64>(EVBMovementMode::InAir));
			*VelocityProperty->ContainerPtrToValuePtr<FVector>(F.Main) = FVector(0, 0, -500);
			F.Test->TestTrue(TEXT("Previous Main airborne helper is nonzero on this grounded physics frame"), F.Main->TimeToLand() > 0);
			UVBGASPLocomotionUpdateLibrary::Update_PropertiesFromCharacter(Linked);
			F.Test->TestEqual(TEXT("Grounded native snapshot does not retain previous Main airborne time"), State.WeaponQuery.TimeToLand, 0.0f);
			// The real Landed callback supplies this light-landing pulse before the
			// animation snapshot. Its delivery is separate from this stale-cache test.
			State.WeaponQuery.JustLandedLight = true;
			State.WeaponQuery.JustLandedHeavy = false;
			UVBGASPLocomotionStateLibrary::OnStateEntry_TransitionToIdle(Linked, State.ActiveChooser.Get());
			F.Test->AddInfo(FString::Printf(TEXT("Grounded stale-cache landing: main_helper=%g captured=%g source=%s asset=%s waiting=%d failed=%d"),
				F.Main->TimeToLand(), State.WeaponQuery.TimeToLand, *GetNameSafe(State.LastSelection.SourceDatabase.Get()),
				*GetNameSafe(UVBGASPLocomotionStateLibrary::GetCMCBlendInputs(Linked).Anim), State.bWaitingForPhase, State.bSelectionContractFailed));
			F.Test->TestTrue(TEXT("Actual Katana transition selects the light landing database before Idle Loop"),
				State.LastSelection.SourceDatabase && State.LastSelection.SourceDatabase->GetFName() == TEXT("PSD_VB_Stand_Idle_Lands_Light"));
			F.Test->TestFalse(TEXT("Grounded landing has no empty-phase hold"), State.bWaitingForPhase);
			F.Test->TestFalse(TEXT("Grounded landing keeps CMC enabled"), State.bSelectionContractFailed);
			return true;
		}
	private:
		TSharedPtr<FVBActualWeaponIdleFixture> Fixture;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBActualWeaponIdleWarmHistoryTest, "Vowbound.Animation.CMC.ActualWeaponIdleWarmHistory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBActualWeaponIdleWarmHistoryTest::RunTest(const FString& Parameters)
{
	const auto Fixture = MakeShared<FVBActualWeaponIdleFixture>();
	Fixture->Test = this;
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	Fixture->World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Actual CMC animation fixture world"), Fixture->World)) return false;
	if (!TestNotNull(TEXT("Fixture world context owner"), GEngine)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Fixture->World);
	UClass* CharacterClass = LoadClass<AVBCharacter>(nullptr, TEXT("/Game/Vowbound/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C"));
	UClass* AnimClass = LoadClass<UVBAnimInstance>(nullptr, TEXT("/Game/Vowbound/Blueprints/Characters/ABP_VBCharacter_58.ABP_VBCharacter_58_C"));
	auto* SkeletonMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (!TestNotNull(TEXT("Actual player character defaults"), CharacterClass)
		|| !TestNotNull(TEXT("Actual player animation class"), AnimClass)
		|| !TestNotNull(TEXT("Actual player skeletal mesh"), SkeletonMesh)) return false;
	Fixture->Character = Fixture->World->SpawnActor<AVBCharacter>(CharacterClass);
	APlayerController* Controller = Fixture->World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("Actual player fixture"), Fixture->Character) || !TestNotNull(TEXT("Fixture controller"), Controller)) return false;
	Fixture->Character->SetController(Controller);
	if (!TestNotNull(TEXT("Actual character movement configuration"), Fixture->Character->GetMovementConfig())) return false;
	Fixture->WeaponProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("CurrentWeaponType"));
	Fixture->LocomotionProperty = FindFProperty<FEnumProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("LocomotionState"));
	if (!TestNotNull(TEXT("Actual weapon component"), Fixture->Character->GetWeaponStateComponent())
		|| !TestNotNull(TEXT("Weapon type fixture field"), Fixture->WeaponProperty)
		|| !TestNotNull(TEXT("Locomotion fixture field"), Fixture->LocomotionProperty)) return false;
	// 납도 None 풀부터 시작한다. 소환/GAS/몽타주를 실행하는 fixture는 아니다.
	Fixture->SetWeaponState(EVBWeaponType::None, EVBLocomotionState::CombatSheathed);
	USkeletalMeshComponent* Mesh = Fixture->Character->GetMesh();
	Mesh->SetComponentTickEnabled(false);
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Mesh->SetSkeletalMeshAsset(SkeletonMesh);
	Mesh->SetAnimInstanceClass(AnimClass);
	Fixture->Main = Cast<UVBAnimInstance>(Mesh->GetAnimInstance());
	if (!TestNotNull(TEXT("Actual running player AnimBP"), Fixture->Main)) return false;
	Fixture->Main->bUseMultiThreadedAnimationUpdate = false;
	Fixture->Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Fixture->Character->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	IConsoleVariable* Setup = IConsoleManager::Get().FindConsoleVariable(TEXT("DDCVar.LocomotionSetupCMC"));
	if (!TestNotNull(TEXT("Production CMC setup CVar"), Setup)
		|| !TestEqual(TEXT("Fixture observes the production CMC route"), Setup->GetInt(), 1)) return false;
	ADD_LATENT_AUTOMATION_COMMAND(FVBActualWeaponIdleWarmHistoryCommand(Fixture));
	return true;
}
#endif
