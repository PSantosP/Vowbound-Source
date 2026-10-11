#include "Misc/AutomationTest.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFootPlacementSettingsTest, "Vowbound.Unit.Animation.FootPlacementPelvis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBFootPlacementSettingsTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : {
		TEXT("/Game/Vowbound/Blueprints/Characters/ABP_VB_GASPUnarmed.ABP_VB_GASPUnarmed_C"),
		TEXT("/Game/Vowbound/Blueprints/Characters/ABP_VB_GASPLocomotion.ABP_VB_GASPLocomotion_C")})
	{
		UClass* Class = LoadClass<UAnimInstance>(nullptr, Path);
		if (!TestNotNull(Path, Class)) continue;
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>();
		UAnimInstance* Instance = NewObject<UAnimInstance>(Mesh, Class);
		const IAnimClassInterface* AnimClass = IAnimClassInterface::GetFromClass(Class);
		if (!TestNotNull(TEXT("animation class interface"), AnimClass)) continue;
		int32 Found = 0;
		for (const FStructProperty* Property : AnimClass->GetAnimNodeProperties())
		{
			if (!Property || Property->Struct != FAnimNode_FootPlacement::StaticStruct()) continue;
			++Found;
			FAnimNode_FootPlacement* Node = Property->ContainerPtrToValuePtr<FAnimNode_FootPlacement>(Instance);
			const float Alpha = Node->Alpha;
			const float TraceStart = Node->TraceSettings.StartOffset;
			const float PlantRadius = Node->PlantSettings.UnplantRadius;
			const EActorMovementCompensationMode CompensationMode = Node->PelvisSettings.ActorMovementCompensationMode;
			const FAnimNode_FootPlacement* Default = Property->ContainerPtrToValuePtr<FAnimNode_FootPlacement>(Class->GetDefaultObject());
			const float DefaultOffset = Default->PelvisSettings.MaxOffset;
			const float DefaultStiffness = Default->PelvisSettings.LinearStiffness;
			TestTrue(TEXT("linked instance configured"), UVBGASPLocomotionLibrary::ConfigureFootPlacementPelvis(Instance, 35.f, 1000.f) > 0);
			TestEqual(TEXT("offset"), Node->PelvisSettings.MaxOffset, 35.f);
			TestEqual(TEXT("stiffness"), Node->PelvisSettings.LinearStiffness, 1000.f);
			TestEqual(TEXT("IK alpha preserved"), Node->Alpha, Alpha);
			TestEqual(TEXT("trace start preserved"), Node->TraceSettings.StartOffset, TraceStart);
			TestEqual(TEXT("plant radius preserved"), Node->PlantSettings.UnplantRadius, PlantRadius);
			TestTrue(TEXT("step compensation preserved"), Node->PelvisSettings.ActorMovementCompensationMode == CompensationMode);
			TestEqual(TEXT("class offset unchanged"), Default->PelvisSettings.MaxOffset, DefaultOffset);
			TestEqual(TEXT("class stiffness unchanged"), Default->PelvisSettings.LinearStiffness, DefaultStiffness);
			TestEqual(TEXT("CDO mutation refused"), UVBGASPLocomotionLibrary::ConfigureFootPlacementPelvis(Class->GetDefaultObject<UAnimInstance>(), 10.f, 10.f), 0);
			TestEqual(TEXT("invalid settings refused"), UVBGASPLocomotionLibrary::ConfigureFootPlacementPelvis(Instance, -1.f, 0.f), 0);
			TestEqual(TEXT("invalid settings preserve offset"), Node->PelvisSettings.MaxOffset, 35.f);
		}
		TestTrue(TEXT("actual FootPlacement node exists"), Found > 0);
	}
	TestEqual(TEXT("null refused"), UVBGASPLocomotionLibrary::ConfigureFootPlacementPelvis(nullptr, 35.f, 1000.f), 0);
	return true;
}
#endif
