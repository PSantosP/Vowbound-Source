#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBPushableConfig.h"

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentContactTest, "Vowbound.Unit.Interaction.ContactWindow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnvironmentContactTest::RunTest(const FString& Parameters)
{
	FVBEnvironmentHandContact Contact;
	Contact.StartTime = 1.0f;
	Contact.EndTime = 2.0f;
	TestEqual(TEXT("unauthored contact stays disabled"), Contact.GetWeight(1.5f), 0.0f);
	Contact.PropBone = TEXT("Door");
	Contact.BlendTime = 0.2f;
	TestEqual(TEXT("no contact before or on entry"), Contact.GetWeight(1.0f), 0.0f);
	TestTrue(TEXT("smooth entry reaches half weight"), FMath::IsNearlyEqual(Contact.GetWeight(1.1f), 0.5f, 1.e-5f));
	TestEqual(TEXT("hold reaches full weight"), Contact.GetWeight(1.5f), 1.0f);
	TestTrue(TEXT("release is symmetric"), FMath::IsNearlyEqual(Contact.GetWeight(1.9f), 0.5f, 1.e-5f));
	TestEqual(TEXT("release ends completely"), Contact.GetWeight(2.0f), 0.0f);
	Contact.BlendTime = 3.0f;
	TestEqual(TEXT("short windows still reach full weight"), Contact.GetWeight(1.5f), 1.0f);
	Contact.EndTime = 0.5f;
	TestEqual(TEXT("invalid window cannot grab"), Contact.GetWeight(1.5f), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentMotionTest, "Vowbound.Unit.Interaction.MotionSelection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnvironmentMotionTest::RunTest(const FString& Parameters)
{
	UVBInteractableConfig* Config = NewObject<UVBInteractableConfig>();
	Config->ActivateMotion.CharacterMontage = NewObject<UAnimMontage>();
	Config->ActivateMotionBack.CharacterMontage = NewObject<UAnimMontage>();
	Config->ActivateMotion.StandTransform.SetLocation(FVector(0, 170, 0));
	Config->ActivateMotionBack.StandTransform.SetLocation(FVector::ZeroVector);
	const FTransform Prop(FRotator(0, 61, 0), FVector(800, -350, 90), FVector(1.5));
	const FVBInteractionMotion* Back = Config->SelectMotion(true, Prop, Prop.TransformPosition(FVector(0, 5, 95)));
	TestTrue(TEXT("back side selection works for rotated/scaled props"), Back == &Config->ActivateMotionBack);
	const uint8 SelectedIndex = Config->GetMotionIndex(*Back);
	TestEqual(TEXT("selected side has stable identity"), SelectedIndex, uint8(2));
	TestTrue(TEXT("moving after selection cannot replace stored motion"), Config->GetMotionByIndex(SelectedIndex) == Back);
	TestNull(TEXT("missing close animation is not substituted by open"), Config->SelectMotion(false, Prop, FVector::ZeroVector));
	TestNull(TEXT("invalid replicated motion index is rejected"), Config->GetMotionByIndex(MAX_uint8));
	FTransform Bone(FRotator(0, 32, 0), FVector(-83, 111, 0));
	const FTransform Grip(FRotator(15, 20, 5), FVector(-12, -90, 110));
	const FTransform Mesh(FRotator(0, -90, 0), FVector(720, -220, 5));
	const FTransform WorldTarget = Grip * Bone * Prop;
	const FTransform ComponentTarget = WorldTarget.GetRelativeTransform(Mesh);
	TestTrue(TEXT("component-space target returns to the moving prop in world space"),
		(ComponentTarget * Mesh).Equals(WorldTarget, 0.001f));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDoorSideKickTest, "Vowbound.Unit.Interaction.DoorSideAndKick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDoorSideKickTest::RunTest(const FString& Parameters)
{
	UVBInteractableConfig* Config = NewObject<UVBInteractableConfig>();
	Config->bUseSidePlane = true;
	Config->SidePlaneOrigin = FVector(0, 111.4, 0);
	Config->SidePlaneNormal = FVector::YAxisVector;
	Config->ActivateMotion.CharacterMontage = NewObject<UAnimMontage>();
	Config->ActivateMotionBack.CharacterMontage = NewObject<UAnimMontage>();
	Config->DeactivateMotion.CharacterMontage = NewObject<UAnimMontage>();
	Config->KickOpenMotion.CharacterMontage = NewObject<UAnimMontage>();
	Config->KickOpenMotion.PropAnimation = NewObject<UAnimSequence>();
	Config->ActivateMotion.StandTransform.SetLocation(FVector(0, 170, 0));
	Config->KickOpenMotion.StandTransform = FTransform(FRotator(0, -90, 0), FVector(0, 225, 0));
	const FTransform Prop(FRotator(0, 73, 0), FVector(1000, -800, 50), FVector(1.5));
	const FVector Front = Prop.TransformPosition(FVector(0, 180, 90));
	const FVector Back = Prop.TransformPosition(FVector(0, 100, 90));
	const FVector Forward = Prop.TransformVectorNoScale(FVector(0, -400, 0));
	TestTrue(TEXT("door plane wins in band where nearest stand chooses wrong side"),
		Config->SelectMotion(true, Prop, Back) == &Config->ActivateMotionBack);
	TestTrue(TEXT("sprinting into push side selects kick"),
		Config->SelectMotion(true, Prop, Front, true, Forward) == &Config->KickOpenMotion);
	TestTrue(TEXT("walking keeps hands even at high speed"),
		Config->SelectMotion(true, Prop, Front, false, Forward) == &Config->ActivateMotion);
	TestTrue(TEXT("holding sprint while stationary cannot kick"),
		Config->SelectMotion(true, Prop, Front, true) == &Config->ActivateMotion);
	TestTrue(TEXT("retreating while sprinting cannot kick"),
		Config->SelectMotion(true, Prop, Front, true, -Forward) == &Config->ActivateMotion);
	TestTrue(TEXT("pull side keeps pull motion"),
		Config->SelectMotion(true, Prop, Back, true, -Forward) == &Config->ActivateMotionBack);
	TestTrue(TEXT("open door closes normally even while sprinting"),
		Config->SelectMotion(false, Prop, Front, true, Forward) == &Config->DeactivateMotion);
	TestEqual(TEXT("kick has stable replicated index"), Config->GetMotionIndex(Config->KickOpenMotion), uint8(4));
	TestTrue(TEXT("kick index resolves after alignment"), Config->GetMotionByIndex(4) == &Config->KickOpenMotion);
	Config->ActivateMotionBack.CharacterMontage = nullptr;
	TestNull(TEXT("missing side never aligns across the door"), Config->SelectMotion(true, Prop, Back));
	Config->KickOpenMotion.PropAnimation = nullptr;
	TestTrue(TEXT("incomplete kick pair falls back to hands"),
		Config->SelectMotion(true, Prop, Front, true, Forward) == &Config->ActivateMotion);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPushTravelTest, "Vowbound.Unit.Interaction.PushTravel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPushTravelTest::RunTest(const FString& Parameters)
{
	UVBPushableConfig* Config = NewObject<UVBPushableConfig>();
	TestEqual(TEXT("pull cannot pass the initial position"), Config->ClampTravel(-10), 0.0f);
	TestEqual(TEXT("large frame cannot overshoot the window destination"), Config->ClampTravel(900), 345.0f);
	TestEqual(TEXT("partial travel is preserved"), Config->ClampTravel(123), 123.0f);
	TestEqual(TEXT("wall in front stops both"), UVBPushableConfig::SharedMoveFraction(0.2f, 1), 0.2f);
	TestEqual(TEXT("wall behind player stops both"), UVBPushableConfig::SharedMoveFraction(1, 0.1f), 0.1f);
	TestEqual(TEXT("initial penetration cannot move the pair"), UVBPushableConfig::SharedMoveFraction(1, 0), 0.0f);
	Config->MaxTravel = -100;
	TestEqual(TEXT("inverted travel limits collapse safely"), Config->ClampTravel(10), 0.0f);
	return true;
}
#endif
