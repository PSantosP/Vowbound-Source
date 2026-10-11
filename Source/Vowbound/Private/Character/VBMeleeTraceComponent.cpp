// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Character/VBMeleeTraceComponent.h"

#include "Character/VBWeaponStateComponent.h"
#include "Character/VBWeaponVisualComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBMeleeTraceShapeSet.h"
#include "Data/VBSwingTrailProfile.h" // 모양의 궤적 프로필을 약한 포인터로 옮긴다 - 완전 타입 필요
#include "Data/VBWeaponVisualConfig.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineGlobals.h"
#include "GameFramework/Character.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 판정 캡슐을 그려 본다. 0 = 끔, 1 = 그림. 저작·조정할 때만 켠다.
	TAutoConsoleVariable<int32> CVarDrawMeleeTrace(
		TEXT("vb.Combat.DrawMeleeTrace"), 0,
		TEXT("무기 궤적 판정 캡슐을 그린다. 0 = 끔 / 1 = 그림"),
		ECVF_Default);

	// 날이 한 프레임 동안 멈춰 있으면(히트스톱) 시작·끝이 같아 스윕이 무효가 된다. 아주 작게 밀어 유효한 스윕으로 만든다.
	constexpr float StationarySweepNudge = 0.01f;

	// 모양이 대체 모양을 가리키고 그게 또 가리키는 고리를 끊는다.
	constexpr int32 MaxFallbackDepth = 2;
}

UVBMeleeTraceComponent::UVBMeleeTraceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// 애니 평가가 끝난 뒤의 뼈를 읽는다. 창이 열린 동안만 켠다.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UVBMeleeTraceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (USkeletalMeshComponent* Mesh = GetMesh())
	{
		AddTickPrerequisiteComponent(Mesh);
	}
	// ShapeSet 이 비어도 여기서는 경고하지 않는다. 컴포넌트는 플레이어·적 기반 클래스가 모두 달지만 판정 창을 쓰는 몽타주는 캐릭터별로 옮겨 간다 -
	//   아직 옛 노티만 쓰는 적마다 울리면 잡음이다. 창을 열려는 순간 BeginWindow 가 경고하고 GA 가 옛 판정으로 대신한다(그때가 문제가 되는 때다).
}

void UVBMeleeTraceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndAllWindows();
	Super::EndPlay(EndPlayReason);
}

USkeletalMeshComponent* UVBMeleeTraceComponent::GetMesh() const
{
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		return Character->GetMesh();
	}
	return GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

bool UVBMeleeTraceComponent::ResolveShape(FGameplayTag ShapeTag, FVBResolvedMeleeShape& OutShape) const
{
	// 지금 든 무기와 그 외형 데이터를 모아 날 풀기의 홈(ResolveShapeFromData)에 넘긴다.
	const AActor* Owner = GetOwner();
	const UVBWeaponStateComponent* WeaponState = Owner ? Owner->FindComponentByClass<UVBWeaponStateComponent>() : nullptr;
	const UVBWeaponVisualComponent* Visual = Owner ? Owner->FindComponentByClass<UVBWeaponVisualComponent>() : nullptr;
	return ResolveShapeFromData(ShapeSet, ShapeTag, Visual ? Visual->VisualConfig.Get() : nullptr,
	                            WeaponState ? WeaponState->GetCurrentWeaponType() : EVBWeaponType::None, OutShape);
}

bool UVBMeleeTraceComponent::ResolveShapeFromData(const UVBMeleeTraceShapeSet* InShapeSet, FGameplayTag ShapeTag,
                                                  const UVBWeaponVisualConfig* VisualConfig, EVBWeaponType WeaponType, FVBResolvedMeleeShape& OutShape)
{
	const FVBMeleeTraceShapeDef* Def = InShapeSet ? InShapeSet->Shapes.Find(ShapeTag) : nullptr;
	return Def && ResolveShapeDef(InShapeSet, *Def, VisualConfig, WeaponType, OutShape, 0);
}

bool UVBMeleeTraceComponent::ResolveShapeForTools(const UVBMeleeTraceShapeSet* InShapeSet, FGameplayTag ShapeTag, const UVBWeaponVisualConfig* VisualConfig,
                                                  EVBWeaponType WeaponType, FName& OutBone, FVector& OutBaseLocal, FVector& OutTipLocal, float& OutRadius)
{
	FVBResolvedMeleeShape Shape;
	const bool bResolved = ResolveShapeFromData(InShapeSet, ShapeTag, VisualConfig, WeaponType, Shape);
	OutBone = Shape.Bone;
	OutBaseLocal = Shape.BaseLocal;
	OutTipLocal = Shape.TipLocal;
	OutRadius = Shape.Radius;
	return bResolved;
}

bool UVBMeleeTraceComponent::ResolveShapeDef(const UVBMeleeTraceShapeSet* InShapeSet, const FVBMeleeTraceShapeDef& Def, const UVBWeaponVisualConfig* VisualConfig,
                                             EVBWeaponType WeaponType, FVBResolvedMeleeShape& OutShape, int32 Depth)
{
	OutShape = FVBResolvedMeleeShape();
	OutShape.Radius = Def.Radius;
	// 무기 외형에서 푸는 날은 든 무기의 궤적(표에 있으면), 아니면 모양의 기본. 대체 모양으로 떨어지면 아래 재귀가 그 모양의 것으로 덮는다.
	const TObjectPtr<UVBSwingTrailProfile>* ByWeapon =
		Def.Source == EVBMeleeShapeSource::WeaponVisual ? Def.WeaponTrailProfiles.Find(WeaponType) : nullptr;
	OutShape.TrailProfile = (ByWeapon && *ByWeapon) ? ByWeapon->Get() : Def.TrailProfile.Get();

	if (Def.Source == EVBMeleeShapeSource::Bone)
	{
		OutShape.Bone = Def.Bone;
		OutShape.BaseLocal = Def.BaseLocal;
		OutShape.TipLocal = Def.TipLocal;
		return OutShape.IsValid();
	}

	// WeaponVisual: 지금 든 무기의 외형 파트에서 날을 계산한다. 칼 좌표의 홈은 외형 데이터 하나다(VBMeleeTraceShapeSet.h 주석).
	const FVBWeaponVisualStyle* Style = VisualConfig ? VisualConfig->Styles.Find(WeaponType) : nullptr;
	const FVBWeaponVisualPart* Part = (Style && Style->Parts.IsValidIndex(Def.VisualPartIndex)) ? &Style->Parts[Def.VisualPartIndex] : nullptr;

	if (!Part || !Part->Mesh)
	{
		// 맨손 무기(Fighter)처럼 메시가 없으면 대체 모양으로 간다.
		const FVBMeleeTraceShapeDef* Fallback = (InShapeSet && Def.FallbackShape.IsValid()) ? InShapeSet->Shapes.Find(Def.FallbackShape) : nullptr;
		if (Fallback && Depth < MaxFallbackDepth)
		{
			return ResolveShapeDef(InShapeSet, *Fallback, VisualConfig, WeaponType, OutShape, Depth + 1);
		}
		return false;
	}

	// 메시 로컬에서 손잡이→칼끝 축으로 투영한 범위. 연출(UVBSummonedWeapon)이 날을 드러낼 때 쓰는 것과 같은 계산이다.
	const FVector Axis = Part->RevealAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const FBox Bounds = Part->Mesh->GetBoundingBox();
	const FVector Center = Bounds.GetCenter();
	float Lo = TNumericLimits<float>::Max();
	float Hi = TNumericLimits<float>::Lowest();
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector P(
			(Corner & 1) ? Bounds.Max.X : Bounds.Min.X,
			(Corner & 2) ? Bounds.Max.Y : Bounds.Min.Y,
			(Corner & 4) ? Bounds.Max.Z : Bounds.Min.Z);
		const float D = FVector::DotProduct(P, Axis);
		Lo = FMath::Min(Lo, D);
		Hi = FMath::Max(Hi, D);
	}
	const float CenterD = FVector::DotProduct(Center, Axis);
	const float BaseD = FMath::Lerp(Lo, Hi, Def.HandleRatio);
	const FVector BaseMesh = Center + Axis * (BaseD - CenterD);
	const FVector TipMesh = Center + Axis * (Hi - CenterD);

	// 외형 액터는 소켓에 붙은 뒤 AttachmentTransform 을 상대 변환으로 받는다(UVBWeaponVisualComponent::CreateVisuals).
	OutShape.Bone = Part->Socket;
	OutShape.BaseLocal = Part->AttachmentTransform.TransformPosition(BaseMesh);
	OutShape.TipLocal = Part->AttachmentTransform.TransformPosition(TipMesh);
	return OutShape.IsValid();
}

int32 UVBMeleeTraceComponent::BeginWindow(FGameplayTag ShapeTag, float RehitIntervalSeconds, FVBMeleeTraceHits OnHits)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		// 피해는 서버만 낸다(FIND-085). 클라가 판정 창을 열 길은 두지 않는다.
		VB_LOG(Warning, "%s: 권위 없는 쪽에서 판정 창을 열려 했다 - 거절", *GetNameSafe(Owner));
		return INDEX_NONE;
	}

	FWindow Window;
	if (!ResolveShape(ShapeTag, Window.Shape))
	{
		VB_LOG(Warning, "%s: 판정 모양 %s 를 못 풀었다 - ShapeSet 에 없거나 무기 메시·대체 모양이 없다. 이 창은 판정하지 않는다",
		       *GetNameSafe(Owner), *ShapeTag.ToString());
		return INDEX_NONE;
	}
	if (!GetMesh() || (GetMesh()->GetBoneIndex(Window.Shape.Bone) == INDEX_NONE && !GetMesh()->DoesSocketExist(Window.Shape.Bone)))
	{
		VB_LOG(Warning, "%s: 판정 모양 %s 의 뼈·소켓 %s 가 메시에 없다", *GetNameSafe(Owner), *ShapeTag.ToString(), *Window.Shape.Bone.ToString());
		return INDEX_NONE;
	}

	Window.Id = NextWindowId++;
	Window.RehitInterval = FMath::Max(0.0f, RehitIntervalSeconds);
	Window.OnHits = MoveTemp(OnHits);
	// 큐 노티의 시작 자세를 저장한다. 첫 틱이 다음 프레임에 와도 그 사이 이동을 버리지 않는다.
	// 여기서는 콜백을 부르지 않는다 - 호출한 GA 가 반환된 창 번호와 피해 문맥을 먼저 저장해야 한다.
	Window.PrevBone = GetMesh()->GetSocketTransform(Window.Shape.Bone, RTS_World);
	Window.bHasPrev = true;
	Window.LastSampleFrame = GFrameCounter;
	Windows.Add(MoveTemp(Window));

	AcquireBoneRefresh();
	SetComponentTickEnabled(true);
	return Windows.Last().Id;
}

void UVBMeleeTraceComponent::EndWindow(int32 WindowId, bool bSweepFinalPose)
{
	// 큐 노티는 뼈 평가 뒤에 온다. 정상 종료의 이번 프레임도 검사하되, 취소 때는 마지막 피해를 추가하지 않는다.
	TArray<FHitResult> FinalHits;
	FVBMeleeTraceHits Callback;
	if (bSweepFinalPose)
	{
		FWindow* Window = Windows.FindByPredicate([WindowId](const FWindow& W) { return W.Id == WindowId; });
		USkeletalMeshComponent* Mesh = GetMesh();
		if (Window && Mesh)
		{
			const float DeltaTime = GetWorld() ? GetWorld()->GetDeltaSeconds() * GetOwner()->CustomTimeDilation : 0.0f;
			GatherWindowHits(*Window, Mesh->GetSocketTransform(Window->Shape.Bone, RTS_World), DeltaTime, FinalHits);
			Callback = Window->OnHits;
		}
	}
	const int32 Removed = Windows.RemoveAll([WindowId](const FWindow& W) { return W.Id == WindowId; });
	for (int32 Index = 0; Index < Removed; ++Index)
	{
		ReleaseBoneRefresh();
	}
	if (Windows.Num() == 0)
	{
		SetComponentTickEnabled(false);
	}
	// 패링이 GA 를 끝내거나 새 창을 열 수 있다. 먼저 창·틱·뼈 갱신 소유권을 정리하고 사본으로 호출한다.
	if (FinalHits.Num() > 0)
	{
		Callback.ExecuteIfBound(FinalHits);
	}
}

void UVBMeleeTraceComponent::EndAllWindows()
{
	const int32 Count = Windows.Num();
	Windows.Reset();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		ReleaseBoneRefresh();
	}
	SetComponentTickEnabled(false);
}

void UVBMeleeTraceComponent::AcquireBoneRefresh()
{
	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh)
	{
		return;
	}
	if (BoneRefreshCount == 0)
	{
		// 화면 밖 캐릭터의 뼈는 렌더될 때만 갱신된다(ACharacter 생성자가 AlwaysTickPose, USkinnedMeshComponent::ShouldUpdateTransform).
		//   창 동안만 강제로 갱신해야 카메라 밖 적의 칼이 허공을 긋지 않는다(설계 8절).
		SavedTickOption = Mesh->VisibilityBasedAnimTickOption;
		bSavedUpdateRateOptimizations = Mesh->bEnableUpdateRateOptimizations;
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Mesh->bEnableUpdateRateOptimizations = false;
		RefreshedMesh = Mesh;
	}
	++BoneRefreshCount;
}

void UVBMeleeTraceComponent::ReleaseBoneRefresh()
{
	if (BoneRefreshCount <= 0)
	{
		return;
	}
	--BoneRefreshCount;
	if (BoneRefreshCount == 0)
	{
		if (USkeletalMeshComponent* Mesh = RefreshedMesh.Get())
		{
			Mesh->VisibilityBasedAnimTickOption = SavedTickOption;
			Mesh->bEnableUpdateRateOptimizations = bSavedUpdateRateOptimizations;
		}
		RefreshedMesh.Reset();
	}
}

void UVBMeleeTraceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	USkeletalMeshComponent* Mesh = GetMesh();
	if (!Mesh || Windows.Num() == 0)
	{
		return;
	}

	// 넘기는 사이에 GA 가 창을 닫을 수 있으니 번호로 다시 찾는다.
	TArray<int32> Ids;
	for (const FWindow& W : Windows)
	{
		Ids.Add(W.Id);
	}

	for (const int32 Id : Ids)
	{
		FWindow* Window = Windows.FindByPredicate([Id](const FWindow& W) { return W.Id == Id; });
		if (!Window)
		{
			continue;
		}

		TArray<FHitResult> NewHits;
		GatherWindowHits(*Window, Mesh->GetSocketTransform(Window->Shape.Bone, RTS_World), DeltaTime, NewHits);
		if (NewHits.Num() > 0)
		{
			// 사본으로 넘긴다 - 받는 쪽이 창을 닫아 Window 가 무효가 될 수 있다.
			const FVBMeleeTraceHits Callback = Window->OnHits;
			Callback.ExecuteIfBound(NewHits);
		}
	}
}

int32 UVBMeleeTraceComponent::CalculateSweepSteps(const FVBResolvedMeleeShape& Shape, const FTransform& A, const FTransform& B, int32 StepLimit)
{
	// 직선보다 호가 길다. 위치·회전이 서로 상쇄해 끝점이 덜 움직여도 검사 밀도가 줄지 않게 각 이동량을 더한다.
	const float LocalReach = FMath::Max(Shape.BaseLocal.Size(), Shape.TipLocal.Size());
	const float Scale = FMath::Max(A.GetScale3D().GetAbsMax(), B.GetScale3D().GetAbsMax());
	const float RotationTravel = A.GetRotation().AngularDistance(B.GetRotation()) * LocalReach * Scale;
	const float ScaleTravel = FVector::Dist(A.GetScale3D(), B.GetScale3D()) * LocalReach;
	const float Travel = FVector::Dist(A.GetLocation(), B.GetLocation()) + RotationTravel + ScaleTravel;
	return FMath::Clamp(FMath::CeilToInt(Travel / Shape.Radius), 1, FMath::Max(1, StepLimit));
}

void UVBMeleeTraceComponent::GatherWindowHits(FWindow& Window, const FTransform& Current, float DeltaTime, TArray<FHitResult>& OutHits) const
{
	if (!Window.bHasPrev)
	{
		// 모르는 준비 동작을 쓸지는 않는다. 첫 자세와 같은 자리의 스윕으로 이미 닿은 표적을 검사한다.
		Window.PrevBone = Current;
		Window.bHasPrev = true;
	}
	else if (Window.LastSampleFrame != GFrameCounter)
	{
		Window.Elapsed += DeltaTime;
	}
	Window.LastSampleFrame = GFrameCounter;

	const int32 Steps = CalculateSweepSteps(Window.Shape, Window.PrevBone, Current, MaxSubsteps);
	TMap<AActor*, TPair<int32, FHitResult>> BestPerActor;
	// 회전 스윕은 중간 방향의 캡슐이다. 실제 시작/끝 자세까지 덮는다는 보장은 없으므로 별도 검사한다.
	const bool bChangedPose = !Window.PrevBone.Equals(Current);
	if (!Window.bQueriedInitialPose && bChangedPose)
	{
		SweepSegment(Window.Shape, Window.PrevBone, Window.PrevBone, -1, BestPerActor);
	}
	Window.bQueriedInitialPose = true;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		FTransform A;
		FTransform B;
		A.Blend(Window.PrevBone, Current, static_cast<float>(Step) / Steps);
		B.Blend(Window.PrevBone, Current, static_cast<float>(Step + 1) / Steps);
		SweepSegment(Window.Shape, A, B, Step, BestPerActor);
	}
	if (bChangedPose) SweepSegment(Window.Shape, Current, Current, Steps, BestPerActor);
	Window.PrevBone = Current;

	for (TPair<AActor*, TPair<int32, FHitResult>>& Pair : BestPerActor)
	{
		AActor* Target = Pair.Key;
		if (const float* Last = Window.LastHitTime.Find(Target))
		{
			if (Window.RehitInterval <= 0.0f || Window.Elapsed - *Last < Window.RehitInterval)
			{
				continue;
			}
		}
		FHitResult Hit = Pair.Value.Value;
		if (!FinalizeContact(Hit))
		{
			continue;
		}
		Window.LastHitTime.Add(Target, Window.Elapsed);
		OutHits.Add(MoveTemp(Hit));
	}
}

void UVBMeleeTraceComponent::SweepSegment(const FVBResolvedMeleeShape& Shape, const FTransform& A, const FTransform& B, int32 StepIndex,
                                          TMap<AActor*, TPair<int32, FHitResult>>& BestPerActor) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 캡슐은 날을 따라 눕히고, 방향은 이 단계 가운데 자세의 것을 쓴다. 단계 안의 회전은 단계를 잘게 나눠 흡수한다(오차 < 반경).
	FTransform Mid;
	Mid.Blend(A, B, 0.5f);
	const FVector MidBase = Mid.TransformPosition(Shape.BaseLocal);
	const FVector MidTip = Mid.TransformPosition(Shape.TipLocal);
	const FVector Along = MidTip - MidBase;
	const float HalfLength = Along.Size() * 0.5f;
	const FQuat Orientation = FRotationMatrix::MakeFromZ(Along.GetSafeNormal()).ToQuat();

	const FVector StartCenter = (A.TransformPosition(Shape.BaseLocal) + A.TransformPosition(Shape.TipLocal)) * 0.5f;
	FVector EndCenter = (B.TransformPosition(Shape.BaseLocal) + B.TransformPosition(Shape.TipLocal)) * 0.5f;
	if (StartCenter.Equals(EndCenter, StationarySweepNudge))
	{
		EndCenter = StartCenter + Along.GetSafeNormal() * StationarySweepNudge;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBMeleeTrace), /*bTraceComplex=*/false, GetOwner());
	Params.bReturnPhysicalMaterial = false;
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Shape.Radius, HalfLength + Shape.Radius);

	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(Hits, StartCenter, EndCenter, Orientation, TraceChannel, Capsule, Params);

	if (CVarDrawMeleeTrace.GetValueOnGameThread() > 0)
	{
		DrawDebugCapsule(World, EndCenter, HalfLength + Shape.Radius, Shape.Radius, Orientation,
		                 Hits.Num() > 0 ? FColor::Red : FColor::Cyan, false, 1.0f);
	}

	for (FHitResult& Hit : Hits)
	{
		AActor* Target = Hit.GetActor();
		if (!Target || Target == GetOwner())
		{
			continue;
		}
		TPair<int32, FHitResult>* Best = BestPerActor.Find(Target);
		if (!Best || StepIndex < Best->Key || (StepIndex == Best->Key && Hit.Time < Best->Value.Time))
		{
			BestPerActor.Add(Target, TPair<int32, FHitResult>(StepIndex, Hit));
		}
	}
}

bool UVBMeleeTraceComponent::FinalizeContact(FHitResult& Hit) const
{
	// 접점: 스윕 시작부터 겹쳐 있던 히트는 ImpactPoint 가 비어 올 수 있다. 맞은 뼈 → 맞은 컴포넌트 순으로 채운다.
	//   타격 큐(GCN_DamagePhysical)가 이 점에 불꽃을 낸다 - 여기가 «칼이 닿은 자리» 여야 한다.
	if (Hit.ImpactPoint.IsNearlyZero())
	{
		const USkinnedMeshComponent* HitMesh = Cast<USkinnedMeshComponent>(Hit.GetComponent());
		if (HitMesh && !Hit.BoneName.IsNone())
		{
			Hit.ImpactPoint = HitMesh->GetSocketLocation(Hit.BoneName);
		}
		else if (Hit.GetComponent())
		{
			Hit.ImpactPoint = Hit.GetComponent()->GetComponentLocation();
		}
		Hit.Location = Hit.ImpactPoint;
	}

	if (bRejectHitsBehindWalls && GetOwner())
	{
		const FVector From = GetOwner()->GetActorLocation() + FVector(0.0f, 0.0f, WallCheckHeight);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VBMeleeWallCheck), false, GetOwner());
		Params.AddIgnoredActor(Hit.GetActor());
		FHitResult Wall;
		if (GetWorld() && GetWorld()->LineTraceSingleByChannel(Wall, From, Hit.ImpactPoint, ECC_Visibility, Params))
		{
			return false;
		}
	}

	// 관문을 통과한 «진짜 접촉» 이다. 공격 GA 의 blocking 관문(겹침과 관통을 가르던 것)은 이 표시로 통과한다(설계 16절).
	Hit.bBlockingHit = true;
	return true;
}
