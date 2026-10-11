#include "Camera/VBEnvironmentCameraSolver.h"
#include "Data/VBEnvironmentCameraConfig.h"
#include "Character/VBCharacter.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "Traversal/VBTraversalComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"
#include "SceneView.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBEnvironmentCamera, Log, All);
static TAutoConsoleVariable<int32> CVarVBEnvironmentCameraDebug(TEXT("vb.Camera.Environment.Debug"), 0,
	TEXT("1: draw desired/final camera and safe anchor; log collision state changes."));

namespace
{
	double CameraProbeRadius(FMinimalViewInfo View, FIntPoint ViewSize, const APlayerController* PC,
		const UVBEnvironmentCameraConfig& Config)
	{
		ViewSize.X = FMath::Max(1, ViewSize.X);
		ViewSize.Y = FMath::Max(1, ViewSize.Y);
		FIntRect Rect(0, 0, ViewSize.X, ViewSize.Y);
		FIntRect Constrained = Rect;
		if (View.bConstrainAspectRatio && View.AspectRatio > 0)
		{
			const int32 Width = FMath::Min(ViewSize.X, FMath::RoundToInt(ViewSize.Y * View.AspectRatio));
			const int32 Height = FMath::Min(ViewSize.Y, FMath::RoundToInt(ViewSize.X / View.AspectRatio));
			Constrained = FIntRect(0, 0, FMath::Max(1, Width), FMath::Max(1, Height));
		}
		FSceneViewProjectionData Projection;
		Projection.SetViewRectangle(Rect);
		const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		const auto Axis = View.AspectRatioAxisConstraint.Get(LocalPlayer ?
			LocalPlayer->AspectRatioAxisConstraint.GetValue() : AspectRatio_MaintainXFOV);
		FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, Axis, Constrained, Projection);
		const FMatrix Inverse = Projection.ProjectionMatrix.Inverse();
		double Radius = FMath::Max(1.f, Config.MinimumProbeRadius);
		// Reversed-Z projection: z=1 is the near plane. A sphere contains all four corners.
		for (double X : {-1.0, 1.0}) for (double Y : {-1.0, 1.0})
		{
			const FVector4 P = Inverse.TransformFVector4(FVector4(X, Y, 1, 1));
			if (FMath::IsFinite(P.W) && FMath::Abs(P.W) > UE_SMALL_NUMBER)
			{
				const double Length = FVector(P.X / P.W, P.Y / P.W, P.Z / P.W).Length();
				if (FMath::IsFinite(Length)) Radius = FMath::Max(Radius, Length);
			}
		}
		return Radius + FMath::Max(0.f, Config.SurfaceMargin);
	}
}

void FVBEnvironmentCameraSolver::Reset()
{
	Recovery.Reset();
	LocalAdjustment = FVector::ZeroVector;
	LastCandidate = INDEX_NONE;
	bHasHistory = false;
}

void FVBEnvironmentCameraSolver::Update(ACharacter* Pawn, const UVBEnvironmentCameraConfig* Settings,
	float DeltaTime, bool bCameraCut, FMinimalViewInfo& Pose)
{
	using namespace VBEnvironmentCameraPolicy;
	APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!Pawn || !Settings || !PC || !PC->IsLocalController() || PC->GetViewTarget() != Pawn ||
		Pawn->GetNetMode() == NM_DedicatedServer)
	{
		Reset();
		return;
	}
	UWorld* World = Pawn->GetWorld();
	const UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
	if (!World || !Capsule) return;
	const FVector Desired = Pose.Location;
	if (Desired.ContainsNaN()) { Reset(); return; }
	const FVector Up = -Pawn->GetCharacterMovement()->GetGravityDirection();
	const FVector Right = FVector::VectorPlaneProject(Pose.Rotation.RotateVector(FVector::RightVector), Up).GetSafeNormal();
	FIntPoint ViewSize;
	PC->GetViewportSize(ViewSize.X, ViewSize.Y);
	const double Radius = CameraProbeRadius(Pose, ViewSize, PC, *Settings);
	const double Inset = FMath::Max(0.0, Capsule->GetScaledCapsuleHalfHeight() - Radius - Settings->SurfaceMargin);
	FVector Center = Capsule->GetComponentLocation();
	FVector Anchor = Center + Up * Inset * FMath::Clamp(Settings->AnchorHeightFraction, 0.f, 1.f);
	auto* VB = Cast<AVBCharacter>(Pawn);
	const bool bRagdoll = VB && VB->GetPlayerRagdollComponent() && VB->GetPlayerRagdollComponent()->IsRagdollActive();
	const bool bSpecialTraversal = VB && VB->GetTraversalComponent() && VB->GetTraversalComponent()->OwnsCapsuleFacing();
	// 구도를 막는 몸 상태는 몸 소유 정책 한 곳이 정한다(구조 정리 2단계 1항) - 래그돌·매달림 둘뿐이고 전투 카메라
	//   목록을 쓰지 않는다(쓰면 처형·스마트 오브젝트·환경 상호작용 중 구도가 새로 막힌다). 조준은 이 소비처의 조건이다.
	VBBodyControlPolicy::FEnvironmentCameraFacts BodyFacts;
	BodyFacts.bPlayerRagdoll = bRagdoll;
	BodyFacts.bTraversalFacing = bSpecialTraversal;
	const bool bAllowComposition = !(VB && VB->WantsToAim()) && !VBBodyControlPolicy::BlocksEnvironmentCameraComposition(BodyFacts);
	if (bRagdoll && Pawn->GetMesh() && Pawn->GetMesh()->DoesSocketExist(TEXT("pelvis")))
		Anchor = Center = Pawn->GetMesh()->GetSocketLocation(TEXT("pelvis"));

	FCollisionQueryParams Query(SCENE_QUERY_STAT(VBEnvironmentCamera), false, Pawn);
	TArray<AActor*> Attached;
	Pawn->GetAttachedActors(Attached, true, true);
	for (AActor* Actor : Attached) if (Actor && Actor->IsOwnedBy(Pawn)) Query.AddIgnoredActor(Actor);
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	const ECollisionChannel Channel = Settings->TraceChannel;
	auto IsSafe = [&](const FVector& P)
	{
		return !P.ContainsNaN() && !World->OverlapBlockingTestByChannel(P, FQuat::Identity, Channel, Shape, Query);
	};
	auto Clip = [&](const FVector& Start, const FVector& End, FVector& Out)
	{
		FHitResult Hit;
		Out = End;
		if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Channel, Shape, Query))
		{
			if (Hit.bStartPenetrating) return false;
			const double Distance = FVector::Distance(Start, End);
			const double T = Distance > UE_SMALL_NUMBER ? FMath::Max(0.0, double(Hit.Time) - Settings->SurfaceMargin / Distance) : 0;
			Out = FMath::Lerp(Start, End, T);
		}
		return IsSafe(Out);
	};
	if (!IsSafe(Anchor))
	{
		bool bFound = false;
		for (double Height : {0.0, -0.5 * Inset, -Inset})
		{
			const FVector Alternative = Center + Up * Height;
			if (IsSafe(Alternative)) { Anchor = Alternative; bFound = true; break; }
		}
		if (!bFound)
		{
			if (!bWasUnresolved) UE_LOG(LogVBEnvironmentCamera, Warning, TEXT("No safe camera anchor for %s; camera geometry unresolved"), *Pawn->GetName());
			bWasUnresolved = true;
			if (bHasHistory && IsSafe(LastPosition)) Pose.Location = LastPosition;
			Reset();
			return;
		}
	}
	if (bWasUnresolved) UE_LOG(LogVBEnvironmentCamera, Log, TEXT("Camera anchor recovered for %s"), *Pawn->GetName());
	bWasUnresolved = false;
	if (bCameraCut ||
		(bHasHistory && FVector::DistSquared(Anchor, LastAnchor) > FMath::Square(Settings->TeleportResetDistance))) Reset();

	const double DesiredLength = FVector::Distance(Anchor, Desired);
	TArray<FCandidate, TInlineAllocator<4>> Candidates;
	FVector Targets[4] = {Desired, Desired, Desired, Desired};
	const double ShoulderOffset = FVector::DotProduct(Desired - Anchor, Right);
	const FVector Inward = -Right * FMath::Clamp(ShoulderOffset, -double(Settings->MaxLateralAdjustment), double(Settings->MaxLateralAdjustment));
	Targets[1] += Inward;
	Targets[2] -= Up * Settings->MaxHeightAdjustment;
	Targets[3] += Inward - Up * Settings->MaxHeightAdjustment;
	FVector BaseSafe;
	const bool bBaseSafe = Clip(Anchor, Desired, BaseSafe);
	const bool bObstructed = !bBaseSafe || !BaseSafe.Equals(Desired, 0.1);
	const int32 Count = bObstructed && bAllowComposition ? 4 : 1;
	for (int32 I = 0; I < Count; ++I)
	{
		FCandidate& C = Candidates.AddDefaulted_GetRef();
		C.bSafe = I == 0 ? bBaseSafe : Clip(Anchor, Targets[I], C.Position);
		if (I == 0) C.Position = BaseSafe;
		if (!C.bSafe) continue;
		C.Score = FVector::Distance(C.Position, Desired);
		if (bObstructed)
		{
			for (double Height : {0.0, Inset})
			{
				FHitResult Visibility;
				if (World->LineTraceSingleByChannel(Visibility, C.Position, Center + Up * Height, Channel, Query))
					C.Score += Settings->VisibilityPenalty;
			}
		}
	}
	const int32 Selected = SelectCandidate(Candidates, LastCandidate, Settings->CandidateSwitchMargin);
	if (Selected == INDEX_NONE)
	{
		Pose.Location = Anchor;
		Reset();
		return;
	}
	const bool bWasConstrained = Recovery.Fraction < 0.9999;
	const FVector TargetOffset = Targets[Selected] - Desired;
	const FVector WantedAdjustment(0, FVector::DotProduct(TargetOffset, Right), FVector::DotProduct(TargetOffset, Up));
	const double Alpha = RecoveryAlpha(DeltaTime, Settings->RecoveryHalfLife);
	// Clear-side hold also retains the chosen local offset until distance recovery can begin.
	if (!bHasHistory || bObstructed) LocalAdjustment = FMath::Lerp(LocalAdjustment, WantedAdjustment, Alpha);
	else if (Recovery.ClearTime >= Settings->ClearHoldSeconds) LocalAdjustment = FMath::Lerp(LocalAdjustment, FVector::ZeroVector, Alpha);
	if (!bAllowComposition) LocalAdjustment = FVector::ZeroVector;
	const FVector AdjustedTarget = Desired + Right * LocalAdjustment.Y + Up * LocalAdjustment.Z;
	FVector Safe;
	if (!Clip(Anchor, AdjustedTarget, Safe)) Safe = Anchor;
	const double AdjustedLength = FVector::Distance(Anchor, AdjustedTarget);
	const double SafeLength = FVector::Distance(Anchor, Safe);
	const double SafeFraction = AdjustedLength > UE_SMALL_NUMBER ? SafeLength / AdjustedLength : 1;
	const bool bHasClearance = !bObstructed || SafeLength - Recovery.Fraction * AdjustedLength >= Settings->ClearDistanceMargin;
	const double Fraction = Recovery.Update(SafeFraction, DeltaTime, Settings->RecoveryHalfLife,
		Settings->ClearHoldSeconds, bHasClearance);
	const FVector Final = FMath::Lerp(Anchor, AdjustedTarget, Fraction);
	// 직전 위치 -> 이번 위치 재스윕은 두지 않는다(2026-10-07 제거). 둘 다 기준점에서 보이는데 그 사이 직선이 기둥·문틀
	//   모서리를 지나면 카메라를 직전 자리에 세웠고, 다음 프레임도 그 자리에서 시작해서 길이 열릴 때까지 월드에 박혀 있었다
	//   (user 보고 «캐릭터는 가는데 카메라가 서 있다», 시험 CornerDoesNotPin). 화면에 나가는 위치의 안전은 아래 Anchor -> Final
	//   스윕이 매 프레임 따로 보장한다. 대가: 빠르게 모서리를 돌 때 한 프레임에 모서리 너머로 건너뛸 수 있다.
	FVector FinalSafe;
	if (!Clip(Anchor, Final, FinalSafe)) FinalSafe = Anchor;
	Pose.Location = FinalSafe;
	LastAnchor = Anchor;
	LastPosition = FinalSafe;
	LastCandidate = Selected;
	bHasHistory = true;
	if (CVarVBEnvironmentCameraDebug.GetValueOnGameThread())
	{
		const bool bConstrained = Recovery.Fraction < 0.9999;
		if (bWasConstrained != bConstrained) UE_LOG(LogVBEnvironmentCamera, Log,
			TEXT("%s crouch=%d candidate=%d desired=%.1f actual=%.1f"), bConstrained ? TEXT("Constrained") : TEXT("Clear"),
			Pawn->bIsCrouched, Selected, DesiredLength, FVector::Distance(Anchor, FinalSafe));
		DrawDebugLine(World, Anchor, Desired, FColor::Red);
		DrawDebugLine(World, Anchor, FinalSafe, FColor::Green);
		DrawDebugSphere(World, FinalSafe, Radius, 12, FColor::Green);
	}
}
