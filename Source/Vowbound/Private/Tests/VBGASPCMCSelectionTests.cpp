#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Character/VBCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/VBGASPActiveDatabaseColumn.h"
#include "Animation/VBGASPCMCSelectionTypes.h"
#include "Animation/VBGASPLocomotionStateLibrary.h"
#include "Animation/VBGASPLocomotionLibrary.h"
#include "Animation/VBGASPLocomotionUpdateLibrary.h"
#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "ChooserIndexArray.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "../Animation/VBGASPMutableStateView.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCSlopeTest, "Vowbound.Animation.CMC.SlopeBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCSlopeTest::RunTest(const FString& Parameters)
{
	const FVBGSlopeSettings Settings;
	using FUpdate = UVBGASPLocomotionUpdateLibrary;
	TestTrue(TEXT("Flat floor is neutral"), FUpdate::CalculateCMCSlopeInput(0, true, Settings).IsZero());
	TestTrue(TEXT("Below the original dead zone is neutral"), FUpdate::CalculateCMCSlopeInput(14.99f, true, Settings).IsZero());
	TestEqual(TEXT("Positive threshold retains GASP mapping"), FUpdate::CalculateCMCSlopeInput(15, true, Settings).X, 0.5);
	TestEqual(TEXT("Negative threshold retains GASP mapping"), FUpdate::CalculateCMCSlopeInput(-15, true, Settings).X, -0.5);
	TestEqual(TEXT("Extreme ascent clamps"), FUpdate::CalculateCMCSlopeInput(80, true, Settings).X, 1.0);
	TestEqual(TEXT("Extreme descent clamps"), FUpdate::CalculateCMCSlopeInput(-80, true, Settings).X, -1.0);
	TestTrue(TEXT("Missing slope tag disables input"), FUpdate::CalculateCMCSlopeInput(30, false, Settings).IsZero());
	TestTrue(TEXT("Nonfinite angle is rejected"), FUpdate::CalculateCMCSlopeInput(
		std::numeric_limits<float>::quiet_NaN(), true, Settings).IsZero());
	FVBGSlopeSettings Invalid = Settings;
	Invalid.MaxSlopeDegrees = 0;
	TestTrue(TEXT("Zero mapping range cannot divide"), FUpdate::CalculateCMCSlopeInput(30, true, Invalid).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCSelectionTest, "Vowbound.Animation.CMC.SelectionContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCSelectionTest::RunTest(const FString& Parameters)
{
	UAnimSequence* Sequence = NewObject<UAnimSequence>(GetTransientPackage());
	using FSelection = UVBGASPLocomotionStateLibrary;
	FVBGASPCMCChooserOutput Output;
	TestFalse(TEXT("No asset cannot be committed"), FSelection::IsValidCMCSelection(nullptr, Output));
	TestFalse(TEXT("Cold history requires explicit fallback"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.bFallback = true;
	TestTrue(TEXT("Authored deterministic fallback bootstraps history"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.bPoseMatched = true;
	Output.SearchCost = 2;
	TestFalse(TEXT("Fallback cannot masquerade as a pose search"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.bFallback = false;
	Output.bHistoryReady = true;
	Output.MaxAcceptedCost = 2;
	TestTrue(TEXT("Exact cost limit is accepted"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.SearchCost = 2.01f;
	TestFalse(TEXT("Above cost limit cannot overwrite playback"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.MaxAcceptedCost = 0;
	TestTrue(TEXT("Nonpositive limit disables cost gate"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.SearchCost = MAX_flt;
	TestFalse(TEXT("Unset pose cost is not success"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.SearchCost = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Infinite pose cost is rejected"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.bPoseMatched = false;
	TestTrue(TEXT("A direct animation row does not need search cost"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.StartTime = -1;
	TestFalse(TEXT("Invalid start time cannot be committed"), FSelection::IsValidCMCSelection(Sequence, Output));
	Output.StartTime = 0;
	Output.BlendTime = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("NaN blend duration cannot reach BlendStack"), FSelection::IsValidCMCSelection(Sequence, Output));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCForceTest, "Vowbound.Animation.CMC.ForceBlendPulse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCForceTest::RunTest(const FString& Parameters)
{
	FVBGASPCMCState State;
	using FSelection = UVBGASPLocomotionStateLibrary;
	TestFalse(TEXT("Fresh instance does not request blend"), FSelection::ConsumeCMCForceRequest(State));
	State.PendingForceSerial = 1;
	TestTrue(TEXT("New request is delivered"), FSelection::ConsumeCMCForceRequest(State));
	TestFalse(TEXT("Repeated graph reads do not restart playback"), FSelection::ConsumeCMCForceRequest(State));
	State.SelectionSerial = 2;
	TestFalse(TEXT("Selection without force cannot replay an old request"), FSelection::ConsumeCMCForceRequest(State));
	State.PendingForceSerial = 3;
	TestTrue(TEXT("Later request is delivered once"), FSelection::ConsumeCMCForceRequest(State));
	TestFalse(TEXT("Later request is also consumed"), FSelection::ConsumeCMCForceRequest(State));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCActiveDBTest, "Vowbound.Animation.CMC.ActiveDatabaseFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCActiveDBTest::RunTest(const FString& Parameters)
{
	UPoseSearchDatabase* Allowed = NewObject<UPoseSearchDatabase>(GetTransientPackage());
	UPoseSearchDatabase* OtherWeapon = NewObject<UPoseSearchDatabase>(GetTransientPackage());
	FVBGASPActiveDatabaseColumn Column;
	Column.SourceDatabases = { OtherWeapon, Allowed, nullptr, Allowed };
	Column.LoopingRows = { false, false, false, true };
	FVBGASPCMCChooserOutput Selection;
	Selection.AllowedDatabases.Add(Allowed);
	FChooserEvaluationContext Context;
	Context.AddStructParam(Selection);
	FChooserIndexArray::FIndexData InputStorage[] = { {0, 0.1f}, {1, 5.f}, {2, 0.f}, {3, 3.f} };
	FChooserIndexArray::FIndexData OutputStorage[] = { {0, 0.f}, {0, 0.f}, {0, 0.f}, {0, 0.f} };
	FChooserIndexArray Input(InputStorage, UE_ARRAY_COUNT(InputStorage));
	Input.SetNum(UE_ARRAY_COUNT(InputStorage));
	FChooserIndexArray Output(OutputStorage, UE_ARRAY_COUNT(OutputStorage));
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("Every permitted row survives, not only first PSD match"), Output.Num(), uint32(2));
	if (Output.Num() == 2)
	{
		TestEqual(TEXT("Cheaper wrong-weapon row is excluded"), Output[0].Index, uint32(1));
		TestEqual(TEXT("Row cost is preserved for subsequent Pose Match"), Output[0].Cost, 5.f);
		TestEqual(TEXT("Second permitted candidate survives"), Output[1].Index, uint32(3));
	}
	Output.SetNum(0);
	Selection.Phase = EVBGASPCMCPhase::Loop;
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("Loop phase excludes a valid weapon's one-shot"), Output.Num(), uint32(1));
	if (Output.Num() == 1) TestEqual(TEXT("Loop row remains"), Output[0].Index, uint32(3));
	Output.SetNum(0);
	Selection.Phase = UVBGASPLocomotionStateLibrary::GetCMCSelectionPhase(TEXT("In Air Loop"), -400.0f);
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("Air loop filters out jump starts from the active jump pool"), Output.Num(), uint32(1));
	if (Output.Num() == 1) TestEqual(TEXT("Air loop keeps the falling loop"), Output[0].Index, uint32(3));
	Output.SetNum(0);
	Selection.Phase = UVBGASPLocomotionStateLibrary::GetCMCSelectionPhase(TEXT("Transition to In Air Loop"), -400.0f);
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("Falling air entry also excludes jump starts"), Output.Num(), uint32(1));
	if (Output.Num() == 1) TestEqual(TEXT("Falling air entry keeps the loop"), Output[0].Index, uint32(3));
	Output.SetNum(0);
	Selection.Phase = EVBGASPCMCPhase::Transition;
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("Transition prefers valid weapon's one-shot"), Output.Num(), uint32(1));
	if (Output.Num() == 1) TestEqual(TEXT("Transition row remains"), Output[0].Index, uint32(1));
	Output.SetNum(0);
	Column.LoopingRows[1] = true;
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("Another weapon's one-shot cannot suppress loop fallback"), Output.Num(), uint32(2));
	Output.SetNum(0);
	Selection.Phase = EVBGASPCMCPhase::Loop;
	Column.LoopingRows[1] = Column.LoopingRows[3] = false;
	Selection.bHasPhaseCandidates = false;
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("Stop-only pool does not leak a one-shot into Loop"), Output.Num(), uint32(0));
	TestFalse(TEXT("Empty phase can wait for the idle pool"), Selection.bHasPhaseCandidates);
	TestTrue(TEXT("Empty phase is not corrupt authoring"), Selection.bPhaseDataValid);
#if WITH_EDITOR
	Column.LoopingRows = { false, false, false, true };
	Column.MoveRow(3, 1);
	TestTrue(TEXT("Move keeps source and loop role together"), Column.LoopingRows[1] && Column.SourceDatabases[1] == Allowed);
	Column.InsertRows(1, 1);
	TestEqual(TEXT("Insert aligns phase data"), Column.SourceDatabases.Num(), Column.LoopingRows.Num());
	TArray<int> RemoveRows = {1};
	Column.DeleteRows(RemoveRows);
	TestTrue(TEXT("Delete preserves shifted loop role"), Column.LoopingRows[1]);
	Column.MoveRow(1, 4);
#endif
	Column.SetOutputs(Context, 1);
	TestTrue(TEXT("Original source database metadata follows selected row"), Selection.SourceDatabase == Allowed);
	Column.SetOutputs(Context, -1);
	TestNull(TEXT("Fallback cannot retain stale source database"), Selection.SourceDatabase.Get());
	Output.SetNum(0);
	FChooserEvaluationContext MissingContext;
	Column.Filter(MissingContext, Input, Output);
	TestEqual(TEXT("Missing typed context fails closed"), Output.Num(), uint32(0));
	Selection.AllowedDatabases.Reset();
	Column.Filter(Context, Input, Output);
	TestEqual(TEXT("No allowed weapon pool means no candidates"), Output.Num(), uint32(0));
	Column.LoopingRows.Reset();
	Column.Filter(Context, Input, Output);
	TestFalse(TEXT("Missing phase authoring fails closed"), Selection.bPhaseDataValid);
	Column.LoopingRows = { false, false, false, true };
	Selection.bPhaseDataValid = true;
	Selection.bRequireCandidateChooser = true;
	Column.Filter(Context, Input, Output);
	TestFalse(TEXT("Production weapon selection requires a candidate chooser"), Selection.bPhaseDataValid);
	return true;
}

// 콘텐츠 없는 피벗 대체 래치(.sw/crouch-mm-completion/pivot-guard-design.md, 2026-10-04 - Codex 01 요청).
//   대검 앉기처럼 피벗 클립이 없는 풀에서 «피벗 -> 반복 클립 대체 -> 즉시 Loop -> 원판정 그대로 참 -> 다시 피벗» 이 매 프레임 왕복했다.
//   대조로 «피벗 클립이 있는 풀(대체 없음)» 과 «출발·정지의 대체(피벗 아님)» 는 래치를 켜지 않는지 같이 본다 - 늘 켜는 구현은 여기서 깨진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCPivotLatchTest, "Vowbound.Animation.CMC.PivotFallbackLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCPivotLatchTest::RunTest(const FString& Parameters)
{
	using FSelection = UVBGASPLocomotionStateLibrary;
	TestFalse(TEXT("Authored pivot clip (no fallback) never latches"), FSelection::StepPivotFallbackLatch(false, false, true, false));
	TestTrue(TEXT("Pivot that fell back to a loop latches"), FSelection::StepPivotFallbackLatch(false, true, true, false));
	TestFalse(TEXT("Start/stop fallback while not pivoting does not latch"), FSelection::StepPivotFallbackLatch(false, true, false, false));
	TestTrue(TEXT("Latch holds while the raw pivot condition stays true"), FSelection::StepPivotFallbackLatch(true, false, true, false));
	TestFalse(TEXT("Latch releases the moment the raw pivot condition is false"), FSelection::StepPivotFallbackLatch(true, false, false, false));
	TestTrue(TEXT("A second fallback while latched keeps the latch"), FSelection::StepPivotFallbackLatch(true, true, true, false));
	TestFalse(TEXT("Stance change releases even while raw pivot remains true"), FSelection::StepPivotFallbackLatch(true, false, true, true));
	// 래치 필드의 기본값 = 꺼짐. 맨손 길은 대체가 없어 켜지지 않으므로 이 기본값이 곧 «맨손 영향 없음» 이다.
	TestFalse(TEXT("Fresh CMC state starts unlatched"), FVBGASPCMCState().bPivotFallbackLatched);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCPivotConsumerTest, "Vowbound.Animation.CMC.PivotFallbackConsumer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCPivotConsumerTest::RunTest(const FString& Parameters)
{
	UClass* LinkedClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Vowbound/Blueprints/Characters/ABP_VB_GASPLocomotion.ABP_VB_GASPLocomotion_C"));
	if (!TestNotNull(TEXT("Production linked GASP class"), LinkedClass)) return false;
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UAnimInstance* Linked = NewObject<UAnimInstance>(Mesh, LinkedClass);
	const VBGASP::FMutableStateView View(Linked);
	const auto* StateProperty = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
	if (!TestNotNull(TEXT("Production CMC state"), StateProperty)
		|| !TestTrue(TEXT("CMC state has native type"), StateProperty->Struct == FVBGASPCMCState::StaticStruct())) return false;
	for (const FName Property : { FName(TEXT("Velocity")), FName(TEXT("Acceleration")), FName(TEXT("Speed2D")),
		FName(TEXT("UseExperimentalStateMachine")), FName(TEXT("Stance")), FName(TEXT("Gait")) })
	{
		if (!TestNotNull(*Property.ToString(), View.Find(Property))) return false;
	}
	View.SetBool(TEXT("UseExperimentalStateMachine"), true);
	View.SetStruct(TEXT("Velocity"), FVector(165.0, 0.0, 0.0));
	View.SetStruct(TEXT("Acceleration"), FVector(-2048.0, 0.0, 0.0));
	View.SetNumber(TEXT("Speed2D"), 165.0);
	View.SetEnumId(TEXT("Stance"), TEXT("NewEnumerator1"));
	View.SetEnumId(TEXT("Gait"), TEXT("NewEnumerator0"));
	auto& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(Linked);
	State.bSetupWasActive = true;
	State.bSelectedWeaponLocomotion = State.bUsesWeaponLocomotion = true;
	State.SelectedWeaponPool = State.CapturedWeaponPool = static_cast<int32>(EVBWeaponType::BigSword);
	using FSelection = UVBGASPLocomotionStateLibrary;
	using FLocomotion = UVBGASPLocomotionLibrary;
	TestTrue(TEXT("Crouch fixture uses authored Crouch enum"), View.Is(TEXT("Stance"), TEXT("Crouch")));
	TestTrue(TEXT("Unlatched production consumer retains pivot"), FLocomotion::IsPivoting(Linked));
	State.bPivotFallbackLatched = State.bPivotFallbackCrouched = true;
	TestTrue(TEXT("Raw predicate remains true under the latch"), FLocomotion::IsPivotingRaw(Linked));
	TestFalse(TEXT("Production consumer blocks repeated fallback pivot"), FLocomotion::IsPivoting(Linked));
	FSelection::RefreshPivotFallbackLatch(Linked, State);
	TestTrue(TEXT("Refresh does not release itself through the blocked predicate"), State.bPivotFallbackLatched);
	View.SetStruct(TEXT("Acceleration"), FVector(2048.0, 0.0, 0.0));
	FSelection::RefreshPivotFallbackLatch(Linked, State);
	TestFalse(TEXT("Direction stabilization releases actual state"), State.bPivotFallbackLatched);
	View.SetStruct(TEXT("Acceleration"), FVector(-2048.0, 0.0, 0.0));
	State.bPivotFallbackLatched = true;
	View.SetEnumId(TEXT("Stance"), TEXT("NewEnumerator0"));
	TestTrue(TEXT("New standing posture can pivot before refresh"), FLocomotion::IsPivoting(Linked));
	FSelection::RefreshPivotFallbackLatch(Linked, State);
	TestFalse(TEXT("Crouch to Stand refresh releases actual state"), State.bPivotFallbackLatched);
	View.SetEnumId(TEXT("Stance"), TEXT("NewEnumerator1"));
	State.bPivotFallbackLatched = true;
	State.CapturedWeaponPool = static_cast<int32>(EVBWeaponType::Katana);
	TestTrue(TEXT("New weapon pool can pivot before reselection"), FLocomotion::IsPivoting(Linked));
	FSelection::RefreshPivotFallbackLatch(Linked, State);
	TestFalse(TEXT("Weapon pool change releases actual state"), State.bPivotFallbackLatched);
	State.CapturedWeaponPool = State.SelectedWeaponPool;
	State.bUsesWeaponLocomotion = false;
	State.bPivotFallbackLatched = true;
	TestTrue(TEXT("Pure Unarmed ignores old armed latch"), FLocomotion::IsPivoting(Linked));
	FSelection::RefreshPivotFallbackLatch(Linked, State);
	TestFalse(TEXT("Route change releases actual state"), State.bPivotFallbackLatched);
	State.bSelectedWeaponLocomotion = State.bUsesWeaponLocomotion = true;
	State.bSetupWasActive = false;
	State.bPivotFallbackLatched = true;
	TestTrue(TEXT("Disabled CMC cannot gate legacy pivots"), FLocomotion::IsPivoting(Linked));
	State.bSetupWasActive = true;
	State.bSelectionContractFailed = true;
	TestTrue(TEXT("Failed selection cannot gate pivots before settings refresh"), FLocomotion::IsPivoting(Linked));
	UVBGASPLocomotionUpdateLibrary::Update_CVarDrivenVariables(Linked);
	TestFalse(TEXT("Production settings update disables failed CMC"), State.bSetupWasActive);
	TestFalse(TEXT("Production settings update clears the latch"), State.bPivotFallbackLatched);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCPhaseTest, "Vowbound.Animation.CMC.ArmedTransitionRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCPhaseTest::RunTest(const FString& Parameters)
{
	using FSelection = UVBGASPLocomotionStateLibrary;
	using EPhase = EVBGASPCMCPhase;
	TestTrue(TEXT("Loop state is identified by authored name"), FSelection::GetCMCPhase(TEXT("Locomotion Loop")) == EPhase::Loop);
	TestTrue(TEXT("Idle loop also excludes stop clips"), FSelection::GetCMCPhase(TEXT("Idle Loop")) == EPhase::Loop);
	TestTrue(TEXT("Start is a transition"), FSelection::GetCMCPhase(TEXT("Transition to Locomotion Loop")) == EPhase::Transition);
	TestTrue(TEXT("Stop is a transition"), FSelection::GetCMCPhase(TEXT("Transition to Idle Loop")) == EPhase::Transition);
	TestTrue(TEXT("Air retains authored selection behavior"), FSelection::GetCMCPhase(TEXT("In Air Loop")) == EPhase::Any);
	TestTrue(TEXT("Falling air loop excludes jump start clips"), FSelection::GetCMCSelectionPhase(TEXT("In Air Loop"), -400.0f) == EPhase::Loop);
	TestTrue(TEXT("Rising air loop preserves jump clips"), FSelection::GetCMCSelectionPhase(TEXT("In Air Loop"), 400.0f) == EPhase::Any);
	TestTrue(TEXT("Original rise boundary retains jump selection"), FSelection::GetCMCSelectionPhase(TEXT("In Air Loop"), 10.0f) == EPhase::Any);
	TestTrue(TEXT("Below original rise boundary selects falling loop"), FSelection::GetCMCSelectionPhase(TEXT("In Air Loop"), 9.99f) == EPhase::Loop);
	TestTrue(TEXT("Above original rise boundary retains jump selection"), FSelection::GetCMCSelectionPhase(TEXT("In Air Loop"), 10.01f) == EPhase::Any);
	TestTrue(TEXT("Air entry retains authored transition selection"), FSelection::GetCMCPhase(TEXT("Transition to In Air Loop")) == EPhase::Any);
	TestTrue(TEXT("Descending air entry selects the falling loop"), FSelection::GetCMCSelectionPhase(TEXT("Transition to In Air Loop"), -400.0f) == EPhase::Loop);
	TestTrue(TEXT("Rising air entry retains jump selection"), FSelection::GetCMCSelectionPhase(TEXT("Transition to In Air Loop"), 400.0f) == EPhase::Any);
	TestTrue(TEXT("Nonfinite air speed does not manufacture a falling filter"), FSelection::GetCMCSelectionPhase(TEXT("In Air Loop"), std::numeric_limits<float>::quiet_NaN()) == EPhase::Any);
	TestFalse(TEXT("Healthy non-loop is not restarted on the periodic search interval"), FSelection::ShouldReleaseCMCTransition(EPhase::Transition, false, false, 0.7f, 0.2f));
	TestTrue(TEXT("Missing notify still exits at actual blend-out boundary"), FSelection::ShouldReleaseCMCTransition(EPhase::Transition, false, false, 0.2f, 0.2f));
	TestTrue(TEXT("Overshot end time still releases"), FSelection::ShouldReleaseCMCTransition(EPhase::Transition, false, false, -0.01f, 0.2f));
	TestTrue(TEXT("Authored notify owns early release"), FSelection::ShouldReleaseCMCTransition(EPhase::Transition, false, true, 0.7f, 0.2f));
	TestTrue(TEXT("Loop fallback cannot get stuck in Transition"), FSelection::ShouldReleaseCMCTransition(EPhase::Transition, true, false, MAX_flt, 0.2f));
	TestFalse(TEXT("Unbound stack does not manufacture completion"), FSelection::ShouldReleaseCMCTransition(EPhase::Transition, false, false, MAX_flt, 0.2f));
	TestFalse(TEXT("Air/slide release is unchanged"), FSelection::ShouldReleaseCMCTransition(EPhase::Any, false, true, 0.0f, 0.2f));
	FVBGASPCMCWeaponQuery Query;
	Query.MMState = EVBMMState::Moving;
	Query.IsPivoting = Query.IsStarting = Query.ShouldTurnInPlace = Query.ShouldSpinTransition = true;
	Query.JustTraversed = Query.JustLandedLight = Query.JustLandedHeavy = true;
	Query.MMWeaponType = EVBWeaponType::Fighter;
	Query.Gait = EVBGait::Run;
	FSelection::ApplyCMCWeaponPhase(Query, TEXT("Idle Loop"), EPhase::Loop);
	TestTrue(TEXT("CMC idle wins over stale main movement intent"), Query.MMState == EVBMMState::Idle);
	TestFalse(TEXT("Loop suppresses one-shot gates that can close its candidate pool"), Query.IsPivoting || Query.IsStarting || Query.ShouldTurnInPlace ||
		Query.ShouldSpinTransition || Query.JustTraversed || Query.JustLandedLight || Query.JustLandedHeavy);
	TestTrue(TEXT("Phase preserves weapon and gait authoring"), Query.MMWeaponType == EVBWeaponType::Fighter && Query.Gait == EVBGait::Run);
	Query.IsPivoting = true;
	FSelection::ApplyCMCWeaponPhase(Query, TEXT("Transition to Locomotion Loop"), EPhase::Transition);
	TestTrue(TEXT("Transition preserves current linked pivot intent"), Query.MMState == EVBMMState::Moving && Query.IsPivoting);
	const TArray<FName> Projected = FSelection::ProjectCMCPlaybackTags(
		{ TEXT("Start"), TEXT("Pivot"), TEXT("Stop"), TEXT("TurnInPlace") }, { TEXT("Preserved") });
	TestTrue(TEXT("Playback roles reach database-tag consumers without asset names"), Projected.Contains(TEXT("Starts")) &&
		Projected.Contains(TEXT("Pivots")) && Projected.Contains(TEXT("Stops")) && Projected.Contains(TEXT("TurnInPlace")) && Projected.Contains(TEXT("Preserved")));
	return true;
}

#if WITH_EDITORONLY_DATA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCAirPhaseConsumerTest, "Vowbound.Animation.CMC.AirPhaseConsumer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCAirPhaseConsumerTest::RunTest(const FString& Parameters)
{
	UClass* LinkedClass = LoadClass<UAnimInstance>(nullptr,
		TEXT("/Game/Vowbound/Blueprints/Characters/ABP_VB_GASPLocomotion.ABP_VB_GASPLocomotion_C"));
	UChooserTable* Chooser = LoadObject<UChooserTable>(nullptr,
		TEXT("/Game/Vowbound/Animations/GASP/CMC/CHT_VB_CMCWeaponAnimations.CHT_VB_CMCWeaponAnimations"));
	UAnimSequence* Start = LoadObject<UAnimSequence>(nullptr,
		TEXT("/Game/Vowbound/Animations/UEFN/Jump/M_Neutral_Jump_B_Start_Rfoot.M_Neutral_Jump_B_Start_Rfoot"));
	if (!TestNotNull(TEXT("Linked GASP class"), LinkedClass) || !TestNotNull(TEXT("Weapon chooser"), Chooser)
		|| !TestNotNull(TEXT("Existing jump start"), Start) || !TestEqual(TEXT("CMC chooser signature"), Chooser->GetContextData().Num(), 3)) return false;
	const FContextObjectTypeClass* ContextType = Chooser->GetContextData()[0].GetPtr<FContextObjectTypeClass>();
	if (!TestTrue(TEXT("Animation context class"), ContextType && ContextType->Class && ContextType->Class->IsChildOf(UAnimInstance::StaticClass()))) return false;
	if (!TestNotNull(TEXT("Fixture world context owner"), GEngine)) return false;
	const auto Initialization = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("Isolated animation world"), World)) return false;
	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); OwnedPackage->SetDirtyFlag(false); };
	UClass* CharacterClass = LoadClass<AVBCharacter>(nullptr, TEXT("/Game/Vowbound/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C"));
	if (!TestNotNull(TEXT("Production character class"), CharacterClass)) return false;
	AVBCharacter* Character = World->SpawnActor<AVBCharacter>(CharacterClass);
	if (!TestNotNull(TEXT("Production mesh owner"), Character)) return false;
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	UAnimInstance* Main = Mesh->GetAnimInstance();
	UAnimInstance* Linked = Main ? Main->GetLinkedAnimGraphInstanceByTag(TEXT("GASPLocomotion")) : nullptr;
	if (!TestTrue(TEXT("Initialized production animation context"), Main && Main->IsA(ContextType->Class))
		|| !TestTrue(TEXT("Initialized production linked graph"), Linked && Linked->IsA(LinkedClass))) return false;
	const VBGASP::FMutableStateView View(Linked);
	const auto SetAuthoredEnum = [&](FName PropertyName, FName AuthoredName)
	{
		const auto* Property = CastField<FByteProperty>(View.Find(PropertyName));
		if (!Property || !Property->Enum) return false;
		for (int32 Index = 0; Index < Property->Enum->NumEnums(); ++Index)
		{
			const int64 Value = Property->Enum->GetValueByIndex(Index);
			if (Property->Enum->GetAuthoredNameStringByValue(Value) == AuthoredName.ToString())
			{
				Property->SetPropertyValue_InContainer(Linked, static_cast<uint8>(Value));
				return true;
			}
		}
		return false;
	};
	if (!TestTrue(TEXT("Actual air-loop state"), SetAuthoredEnum(TEXT("StateMachineState"), TEXT("In Air Loop")))
		|| !TestTrue(TEXT("Actual airborne movement"), SetAuthoredEnum(TEXT("MovementMode"), TEXT("InAir")))) return false;
	const auto* StateProperty = CastField<FStructProperty>(View.Find(TEXT("CMCState")));
	const auto* InputsProperty = CastField<FStructProperty>(View.Find(TEXT("CMCBlendInputs")));
	if (!TestTrue(TEXT("Native state and playback contracts"), StateProperty && InputsProperty
		&& StateProperty->Struct == FVBGASPCMCState::StaticStruct() && InputsProperty->Struct == FVBGASPCMCBlendInputs::StaticStruct())) return false;
	auto& State = *StateProperty->ContainerPtrToValuePtr<FVBGASPCMCState>(Linked);
	auto& Inputs = *InputsProperty->ContainerPtrToValuePtr<FVBGASPCMCBlendInputs>(Linked);
	State.ActiveChooser = Chooser;
	State.SelectionContext = Main;
	State.bSetupWasActive = State.bHasSelection = true;
	State.bSelectedWeaponLocomotion = State.bUsesWeaponLocomotion = true;
	State.SelectedWeaponPool = State.CapturedWeaponPool = static_cast<int32>(EVBWeaponType::BigSword);
	State.WeaponQuery.MMWeaponType = EVBWeaponType::BigSword;
	State.bHistoryWasReady = UVBGASPLocomotionStateLibrary::IsCMCPoseHistoryReady(Linked);
	State.SelectionSerial = 1;
	// 이 시험은 «이미 수락했고 걸린 재선택 요청이 없는» 공중 선택에서 시작한다. 기본값 bNeedsReselect=true 를 남기면
	//   첫 Update 가 상승 구간이어도 그 요청 때문에 다시 고른다 - 상태 전이가 아니라 남은 요청을 시험하게 된다.
	State.bNeedsReselect = State.bWaitingForPhase = State.bSelectionContractFailed = false;
	Inputs.Anim = Start;
	Inputs.bLoop = false;
	if (!TestFalse(TEXT("Fixture starts from an accepted selection with no pending reselect"),
		!State.bHasSelection || State.bNeedsReselect || State.bWaitingForPhase || State.bSelectionContractFailed)) return false;
	View.SetNumber(TEXT("Speed2D"), 165.0);
	View.SetStruct(TEXT("Velocity"), FVector(165.0, 0.0, 100.0));
	UVBGASPLocomotionStateLibrary::UpdateCMCSelection(Linked);
	TestEqual(TEXT("Ascending non-loop is retained"), State.SelectionSerial, uint64(1));
	View.SetStruct(TEXT("Velocity"), FVector(165.0, 0.0, 10.0));
	UVBGASPLocomotionStateLibrary::UpdateCMCSelection(Linked);
	TestEqual(TEXT("Inclusive rising boundary is retained"), State.SelectionSerial, uint64(1));
	View.SetStruct(TEXT("Velocity"), FVector(165.0, 0.0, 9.99));
	UVBGASPLocomotionStateLibrary::UpdateCMCSelection(Linked);
	TestTrue(TEXT("Same-state descent dispatches a new accepted selection"), State.SelectionSerial > 1);
	TestTrue(TEXT("Descent accepts looping playback"), Inputs.Anim && Inputs.bLoop && State.LastSelection.Phase == EVBGASPCMCPhase::Loop);
	TestFalse(TEXT("Air phase change does not request ground transition release"), View.Bool(TEXT("NotifyTransition_ToLoop")));
	TestFalse(TEXT("Descent remains a valid selection contract"), State.bSelectionContractFailed || State.bWaitingForPhase);
	const uint64 DescendingSerial = State.SelectionSerial;
	UVBGASPLocomotionStateLibrary::UpdateCMCSelection(Linked);
	TestEqual(TEXT("Healthy looping descent is not force-reselected"), State.SelectionSerial, DescendingSerial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGASPCMCWeaponChooserActiveDBTest, "Vowbound.Animation.CMC.WeaponChooserActiveDatabaseRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBGASPCMCWeaponChooserActiveDBTest::RunTest(const FString& Parameters)
{
	auto* Chooser = LoadObject<UChooserTable>(nullptr,
		TEXT("/Game/Vowbound/Animations/GASP/CMC/CHT_VB_CMCWeaponAnimations.CHT_VB_CMCWeaponAnimations"));
	if (!TestNotNull(TEXT("Production weapon chooser loads"), Chooser)) return false;
	UChooserTable* Search = nullptr;
	for (UObject* Object : Chooser->NestedObjects)
		if (auto* Nested = Cast<UChooserTable>(Object); Nested && Nested->GetName().StartsWith(TEXT("ActiveWeaponPoseMatch"))) { Search = Nested; break; }
	if (!TestNotNull(TEXT("Actual weapon pose-match table is present"), Search)) return false;
	const FVBGASPActiveDatabaseColumn* Active = nullptr;
	for (const FInstancedStruct& Column : Search->ColumnsStructs)
		if (const auto* Value = Column.GetPtr<FVBGASPActiveDatabaseColumn>()) { Active = Value; break; }
	if (!TestNotNull(TEXT("Actual active-database column is present"), Active)) return false;
	if (!TestNotNull(TEXT("Actual native-query candidate chooser is present"), Active->CandidateChooser.Get())) return false;
	const int32 RowCount = Search->ResultsStructs.Num();
	if (!TestTrue(TEXT("Actual results and phase rows stay aligned"), RowCount > 0 &&
		Active->SourceDatabases.Num() == RowCount && Active->LoopingRows.Num() == RowCount)) return false;

	// 실제 애셋의 반복 설정이 바뀌면 컴파일된 단계 필터도 함께 갱신되어야 한다.
	TArray<FChooserIndexArray::FIndexData> InputStorage;
	InputStorage.Reserve(RowCount);
	for (int32 Row = 0; Row < RowCount; ++Row)
	{
		const auto* Result = Search->ResultsStructs[Row].GetPtr<FAssetChooser>();
		UAnimationAsset* Animation = Result ? Cast<UAnimationAsset>(Result->Asset.Get()) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("Animation result row %d"), Row), Animation)) return false;
		bool bLoop = false;
		UPoseSearchLibrary::IsAnimationAssetLooping(Animation, bLoop);
		TestEqual(*FString::Printf(TEXT("Authored looping flag matches actual asset at row %d"), Row), Active->LoopingRows[Row], bLoop);
		InputStorage.Emplace(static_cast<uint32>(Row), 0.0f);
	}
	TArray<FChooserIndexArray::FIndexData> OutputStorage = InputStorage;
	FChooserIndexArray Input(InputStorage.GetData(), RowCount);
	Input.SetNum(RowCount);
	FChooserIndexArray Output(OutputStorage.GetData(), RowCount);
	UPoseSearchDatabase* PreviousPool = nullptr;

	// 자세·단계를 받는다(2026-10-03 앉기 MM 회귀 - Codex 01 요청). 서기·Loop 가 종전 사례다.
	//   Loop 은 반복 행만 남아야 하고(앉기 진입 To_Crouch 같은 1회 행이 섞이면 루프 재선택이 서는 자세로 튄다),
	//   Transition 은 1회 행이 정상이라 «기대한 풀의 행이 하나는 남는가» 와 풀 격리만 본다.
	const auto CheckPoolAt = [&](EVBWeaponType Weapon, const TCHAR* Label, bool bMoving, const TCHAR* DatabasePath,
		EVBStance Stance, EVBGASPCMCPhase Phase, float MovingSpeed, EVBGait Gait)
	{
		auto* Expected = LoadObject<UPoseSearchDatabase>(nullptr, DatabasePath);
		if (!TestNotNull(*FString::Printf(TEXT("%s expected pool loads"), Label), Expected)) return;
		FVBGASPCMCWeaponQuery Query;
		Query.MMWeaponType = Weapon;
		Query.LocomotionState = EVBLocomotionState::ArmedCombat;
		Query.Stance = Stance;
		Query.MMState = bMoving ? EVBMMState::Moving : EVBMMState::Idle;
		// 실제 AnimInstance의 질주 스냅샷은 Gait=Run과 별도 속도 자격 값으로 전달된다.
		Query.Gait = Gait;
		Query.Speed2D = bMoving ? MovingSpeed : 0.0f;
		Query.MMSprintGateSpeed = Query.Speed2D;
		// 지상 행의 실제 TimeToLand 조건은 0이며, 쿼리 구조체 기본값 999와 다르다.
		Query.TimeToLand = 0.0f;
		// 실제 선택과 같은 단계 규칙을 탄다(Loop 이면 출발·피벗·제자리 회전 표지를 끈다).
		UVBGASPLocomotionStateLibrary::ApplyCMCWeaponPhase(Query, NAME_None, Phase);
		FVBGASPCMCChooserOutput Selection;
		Selection.Phase = Phase;
		Selection.bRequireCandidateChooser = true;
		if (PreviousPool) Selection.AllowedDatabases.Add(PreviousPool);
		FChooserEvaluationContext Context;
		Context.AddStructParam(Selection);
		Context.AddStructParam(Query);
		Output.SetNum(0);
		Active->Filter(Context, Input, Output);
		TestTrue(*FString::Printf(TEXT("%s evaluates the actual candidate chooser"), Label), Selection.bCandidateChooserEvaluated);
		TestTrue(*FString::Printf(TEXT("%s phase authoring remains valid"), Label), Selection.bPhaseDataValid);
		TestTrue(*FString::Printf(TEXT("%s admits the expected database"), Label), Selection.AllowedDatabases.Contains(Expected));
		if (PreviousPool && PreviousPool != Expected)
			TestFalse(*FString::Printf(TEXT("%s replaces the previous weapon pool"), Label), Selection.AllowedDatabases.Contains(PreviousPool));
		TestTrue(*FString::Printf(TEXT("%s has usable phase candidates"), Label), Selection.bHasPhaseCandidates && Output.Num() > 0);
		bool bExpectedSurvives = false;
		for (const auto& Row : Output)
		{
			if (!TestTrue(*FString::Printf(TEXT("%s output row is in bounds"), Label), Row.Index < static_cast<uint32>(RowCount))) continue;
			TestTrue(*FString::Printf(TEXT("%s excludes other pools"), Label), Selection.AllowedDatabases.Contains(Active->SourceDatabases[Row.Index]));
			if (Phase == EVBGASPCMCPhase::Loop)
			{
				TestTrue(*FString::Printf(TEXT("%s keeps only looping rows in Loop"), Label), Active->LoopingRows[Row.Index]);
			}
			// 앉은 채로는 어느 단계에서도 앉기 진입 클립(To_Crouch)을 고르지 않는다 - 그 클립은 선 자세에서 시작해,
			//   이미 앉은 정지 전이에서 뽑히면 서기부터 다시 시작한다(.sw/crouch-mm-completion/design.md «최소 수정»).
			if (Stance == EVBStance::Crouch)
			{
				const auto* Result = Search->ResultsStructs[Row.Index].GetPtr<FAssetChooser>();
				const UObject* RowAsset = Result ? Result->Asset.Get() : nullptr;
				TestFalse(*FString::Printf(TEXT("%s never offers a To_Crouch entry clip (row %u %s)"), Label, Row.Index, *GetNameSafe(RowAsset)),
					GetNameSafe(RowAsset).Contains(TEXT("To_Crouch")));
			}
			bExpectedSurvives |= Active->SourceDatabases[Row.Index] == Expected;
		}
		TestTrue(*FString::Printf(TEXT("%s keeps an actual expected-pool animation"), Label), bExpectedSurvives);
		PreviousPool = Expected;
	};
	const auto CheckPool = [&](EVBWeaponType Weapon, const TCHAR* Label, bool bMoving, const TCHAR* DatabasePath)
	{
		CheckPoolAt(Weapon, Label, bMoving, DatabasePath, EVBStance::Stand, EVBGASPCMCPhase::Loop, 680.0f, EVBGait::Run);
	};
	// 무기 소환 전후 대기 풀과 이후 고속 이동 풀을 저장된 Chooser로 검증한다.
	CheckPool(EVBWeaponType::None, TEXT("None idle"), false,
		TEXT("/Game/Vowbound/Animations/MotionMatching/Schemas/PSD_VB_Stand_Idles.PSD_VB_Stand_Idles"));
	CheckPool(EVBWeaponType::Katana, TEXT("Katana idle"), false,
		TEXT("/Game/Vowbound/Animations/MotionMatching/Katana/PSD_VB_Katana_Stand_Idles.PSD_VB_Katana_Stand_Idles"));
	CheckPool(EVBWeaponType::BigSword, TEXT("BigSword idle"), false,
		TEXT("/Game/Vowbound/Animations/MotionMatching/BigSword/PSD_VB_BigSword_Stand_Idles.PSD_VB_BigSword_Stand_Idles"));
	CheckPool(EVBWeaponType::Fighter, TEXT("Fighter idle"), false,
		TEXT("/Game/Vowbound/Animations/MotionMatching/Fighter/PSD_VB_Fighter_Stand_Idles.PSD_VB_Fighter_Stand_Idles"));
	CheckPool(EVBWeaponType::None, TEXT("None high-speed run"), true,
		TEXT("/Game/Vowbound/Animations/MotionMatching/Schemas/PSD_VB_Stand_Run_Loops.PSD_VB_Stand_Run_Loops"));
	CheckPool(EVBWeaponType::Katana, TEXT("Katana sprint"), true,
		TEXT("/Game/Vowbound/Animations/MotionMatching/Katana/PSD_VB_Katana_Stand_Sprint_Loops.PSD_VB_Katana_Stand_Sprint_Loops"));
	CheckPool(EVBWeaponType::BigSword, TEXT("BigSword sprint"), true,
		TEXT("/Game/Vowbound/Animations/MotionMatching/BigSword/PSD_VB_BigSword_Stand_Sprint_Loops.PSD_VB_BigSword_Stand_Sprint_Loops"));
	CheckPool(EVBWeaponType::Fighter, TEXT("Fighter sprint"), true,
		TEXT("/Game/Vowbound/Animations/MotionMatching/Fighter/PSD_VB_Fighter_Stand_Sprint_Loops.PSD_VB_Fighter_Stand_Sprint_Loops"));

	// 앉기 풀(2026-10-03 앉기 MM 보완 - 자산 = Codex 01, .sw/crouch-mm-completion/design.md): 네 무기 x 대기/이동 x Loop/Transition = 16.
	//   대검·맨손은 시작·정지 클립이 없어 Transition 도 반복 행으로 받는다(CMC 의 루프 대체 길). 앞 풀에서 넘어올 때 풀이 갈리는지(격리)도 이어서 본다.
	struct FCrouchPool { EVBWeaponType Weapon; const TCHAR* Name; const TCHAR* Path; };
	const FCrouchPool CrouchPools[] = {
		{EVBWeaponType::None, TEXT("None"), TEXT("/Game/Vowbound/Animations/MotionMatching/None/PSD_VB_None_Crouch.PSD_VB_None_Crouch")},
		{EVBWeaponType::Katana, TEXT("Katana"), TEXT("/Game/Vowbound/Animations/MotionMatching/Katana/PSD_VB_Katana_Crouch.PSD_VB_Katana_Crouch")},
		{EVBWeaponType::BigSword, TEXT("BigSword"), TEXT("/Game/Vowbound/Animations/MotionMatching/BigSword/PSD_VB_BigSword_Crouch.PSD_VB_BigSword_Crouch")},
		{EVBWeaponType::Fighter, TEXT("Fighter"), TEXT("/Game/Vowbound/Animations/MotionMatching/Fighter/PSD_VB_Fighter_Crouch.PSD_VB_Fighter_Crouch")},
	};
	// 앉은 이동 속도는 앉기 최대(200, VBGASPLocomotionLibrary 의 Crouch 범위) 안쪽 값이다.
	constexpr float CrouchMovingSpeed = 150.0f;
	for (const FCrouchPool& Pool : CrouchPools)
	{
		for (const EVBGASPCMCPhase Phase : {EVBGASPCMCPhase::Loop, EVBGASPCMCPhase::Transition})
		{
			for (const bool bMoving : {false, true})
			{
				const FString Label = FString::Printf(TEXT("%s crouch %s %s"), Pool.Name, bMoving ? TEXT("moving") : TEXT("idle"),
					Phase == EVBGASPCMCPhase::Loop ? TEXT("Loop") : TEXT("Transition"));
				CheckPoolAt(Pool.Weapon, *Label, bMoving, Pool.Path, EVBStance::Crouch, Phase, CrouchMovingSpeed, EVBGait::Walk);
			}
		}
	}

	auto* FarJumps = LoadObject<UPoseSearchDatabase>(nullptr,
		TEXT("/Game/Vowbound/Animations/MotionMatching/Schemas/PSD_VB_Jumps_Far.PSD_VB_Jumps_Far"));
	if (!TestNotNull(TEXT("Actual far-jump database loads"), FarJumps)) return false;
	const auto CheckAir = [&](EVBWeaponType Weapon, const TCHAR* Label, float HelperTime, float CollisionTime, bool bExpectFar)
	{
		FVBGASPCMCWeaponQuery Query;
		Query.MMWeaponType = Weapon;
		Query.LocomotionState = EVBLocomotionState::ArmedCombat;
		Query.MovementMode = EVBMovementMode::InAir;
		Query.MMState = EVBMMState::Moving;
		Query.TimeToLand = HelperTime;
		Query.TrajectoryTimeToLand = CollisionTime;
		FVBGASPCMCChooserOutput Selection;
		Selection.Phase = EVBGASPCMCPhase::Any;
		Selection.bRequireCandidateChooser = true;
		FChooserEvaluationContext Context;
		Context.AddStructParam(Selection);
		Context.AddStructParam(Query);
		Output.SetNum(0);
		Active->Filter(Context, Input, Output);
		TestTrue(*FString::Printf(TEXT("%s evaluates the actual air candidate chooser"), Label), Selection.bCandidateChooserEvaluated);
		TestTrue(*FString::Printf(TEXT("%s has valid air candidates"), Label),
			Selection.bPhaseDataValid && Selection.bHasPhaseCandidates && Output.Num() > 0);
		TestEqual(*FString::Printf(TEXT("%s uses its authored landing-time input"), Label), Selection.AllowedDatabases.Contains(FarJumps), bExpectFar);
		bool bFarSurvives = false;
		for (const auto& Row : Output)
		{
			if (!TestTrue(*FString::Printf(TEXT("%s output row is in bounds"), Label), Row.Index < static_cast<uint32>(RowCount))) continue;
			bFarSurvives |= Active->SourceDatabases[Row.Index] == FarJumps;
		}
		TestEqual(*FString::Printf(TEXT("%s preserves the actual far-jump filter result"), Label), bFarSurvives, bExpectFar);
	};
	// 비무장 표는 원시 충돌 예측을, 무장 표는 TimeToLand() 반환값을 읽는 원본 계약을 보존한다.
	CheckAir(EVBWeaponType::None, TEXT("None air reads short collision prediction"), 999.0f, 0.2f, false);
	CheckAir(EVBWeaponType::None, TEXT("None air reads long collision prediction"), 0.2f, 1.5f, true);
	CheckAir(EVBWeaponType::Katana, TEXT("Katana air reads short helper time"), 0.2f, 1.5f, false);
	CheckAir(EVBWeaponType::Katana, TEXT("Katana air reads long helper time"), 999.0f, 0.2f, true);
	return true;
}
#endif

#endif
