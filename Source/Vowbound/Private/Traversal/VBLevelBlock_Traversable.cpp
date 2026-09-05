// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBLevelBlock_Traversable.h"

#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"

AVBLevelBlock_Traversable::AVBLevelBlock_Traversable()
{
	PrimaryActorTick.bCanEverTick = false;

	// 장애물 메시를 루트로 — 전방 트레이스가 이 메시를 맞춤(Visibility 채널).
	ObstacleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObstacleMesh"));
	SetRootComponent(ObstacleMesh);
}

void AVBLevelBlock_Traversable::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 1. 이전 자동생성 옆면 렛지: OppositeLedges 항목 정리 + 컴포넌트 제거(재구성 누적 방지).
	for (const TObjectPtr<USplineComponent>& Old : DerivedSideLedges)
	{
		OppositeLedges.Remove(Old);
		for (TMap<TObjectPtr<USplineComponent>, TObjectPtr<USplineComponent>>::TIterator It(OppositeLedges); It; ++It)
		{
			if (It.Value() == Old)
			{
				It.RemoveCurrent();
			}
		}
		if (IsValid(Old))
		{
			Old->DestroyComponent();
		}
	}
	DerivedSideLedges.Reset();

	// 2. 디자이너가 둔 스플라인 수집(파생분은 위에서 제거됨).
	TArray<USplineComponent*> Designer;
	GetComponents<USplineComponent>(Designer);

	// 3. 디자이너 스플라인이 정확히 2개(앞/뒤 두 모서리)면 좌/우 옆면 렛지 자동 생성 → 4면 큐브 맨틀.
	//    Mantle은 front 렛지만 쓰므로 위치만 맞으면 옆에서도 발동(back/Opposite는 Vault/Hurdle용, 옆면은 미설정).
	if (Designer.Num() == 2
		&& Designer[0]->GetNumberOfSplinePoints() >= 2
		&& Designer[1]->GetNumberOfSplinePoints() >= 2)
	{
		USplineComponent* E1 = Designer[0];
		USplineComponent* E2 = Designer[1];
		const int32 E1Last = E1->GetNumberOfSplinePoints() - 1;
		const int32 E2Last = E2->GetNumberOfSplinePoints() - 1;
		const FVector P0 = E1->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		const FVector P1 = E1->GetLocationAtSplinePoint(E1Last, ESplineCoordinateSpace::World);
		const FVector Q0 = E2->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		const FVector Q1 = E2->GetLocationAtSplinePoint(E2Last, ESplineCoordinateSpace::World);

		// P0 에 가까운 E2 끝점과 페어 → 같은 옆면(코너 연결).
		const bool bQ0NearP0 = FVector::DistSquared(P0, Q0) <= FVector::DistSquared(P0, Q1);
		const FVector Center = GetActorLocation();
		USplineComponent* SideA = CreateSideLedge(P0, bQ0NearP0 ? Q0 : Q1, Center);
		USplineComponent* SideB = CreateSideLedge(P1, bQ0NearP0 ? Q1 : Q0, Center);
		if (SideA) { DerivedSideLedges.Add(SideA); }
		if (SideB) { DerivedSideLedges.Add(SideB); }
		// 좌/우 옆면을 서로 opposite 로 연결 → 상단 sweep 으로 깊이(≈장애물 너비) 산출 → Mantle 분류(D>=59).
		// 없으면 D=0 → Vault 오분류 → Vault 몽타주 없음 → 후보 0개(이 버그).
		if (SideA && SideB)
		{
			OppositeLedges.Add(SideA, SideB);
			OppositeLedges.Add(SideB, SideA);
		}
	}

	// 4. 전체(디자이너 + 파생) 스플라인을 렛지로 수집.
	TArray<USplineComponent*> All;
	GetComponents<USplineComponent>(All);
	Ledges.Empty(All.Num());
	for (USplineComponent* Spline : All)
	{
		Ledges.Add(Spline);
	}
}

USplineComponent* AVBLevelBlock_Traversable::CreateSideLedge(const FVector& A, const FVector& B, const FVector& Center)
{
	USplineComponent* Spline = NewObject<USplineComponent>(this);
	if (!Spline)
	{
		return nullptr;
	}
	Spline->SetupAttachment(RootComponent);
	Spline->RegisterComponent();
	Spline->SetMobility(EComponentMobility::Movable);
	Spline->SetClosedLoop(false, false);
	Spline->ClearSplinePoints(false);
	Spline->AddSplinePoint(A, ESplineCoordinateSpace::World, false);
	Spline->AddSplinePoint(B, ESplineCoordinateSpace::World, false);

	// up 벡터 = 큐브 중심에서 모서리 중점으로의 수평 방향(바깥쪽) — Front/Back의 up 규약과 동일.
	// 트래버설 워프 방향(MakeFromX(-FrontLedgeNormal))이 모서리에서 안쪽으로 향하게 함.
	const FVector Mid = (A + B) * 0.5f;
	FVector Outward = Mid - Center;
	Outward.Z = 0.0f;
	Outward = Outward.GetSafeNormal();
	if (!Outward.IsNearlyZero())
	{
		// 스플라인 up = 바깥 수평(Front/Back 의 up=±X 규약과 동일). SetDefaultUpVector 가
		// GetUpVectorAtSplinePoint 에 신뢰성 있게 반영됨(SetRotationAtSplinePoint 는 UpdateSpline 에서
		// 기본 Z로 되돌아가 무시됨). 워프 회전 MakeFromX(-FrontLedgeNormal) 이 모서리→안쪽을 향하게 함.
		Spline->SetDefaultUpVector(Outward, ESplineCoordinateSpace::World);
	}
	Spline->UpdateSpline();
	return Spline;
}

USplineComponent* AVBLevelBlock_Traversable::FindLedgeClosestToActor(const FVector& ActorLocation) const
{
	USplineComponent* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const TObjectPtr<USplineComponent>& Ledge : Ledges)
	{
		if (!IsValid(Ledge))
		{
			continue;
		}
		// 각 렛지에서 캐릭터에 가장 가까운 점까지의 거리로 비교.
		const FVector ClosestPoint = Ledge->FindLocationClosestToWorldLocation(ActorLocation, ESplineCoordinateSpace::World);
		const float DistSq = FVector::DistSquared(ClosestPoint, ActorLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Ledge;
		}
	}
	return Best;
}

void AVBLevelBlock_Traversable::GetLedgeTransforms(const FVector& HitLocation, const FVector& ActorLocation, FVBTraversalCheckResult& Result) const
{
	// 1. 캐릭터 위치에 가장 가까운 렛지.
	USplineComponent* ClosestLedge = FindLedgeClosestToActor(ActorLocation);
	if (!IsValid(ClosestLedge))
	{
		Result.bHasFrontLedge = false;
		return;
	}

	// 2. 렛지가 충분히 넓은가. 아니면 front 렛지 무효.
	const float SplineLength = ClosestLedge->GetSplineLength();
	if (SplineLength < MinLedgeWidth)
	{
		Result.bHasFrontLedge = false;
		return;
	}

	// 3. 트레이스 적중점에서 렛지 최근접 지점 → 거리 → 모서리에서 MinLedgeWidth/2 만큼 클램프
	//    (코너 근처 트래버설 시 캐릭터가 떠버리는 것 방지: 항상 모서리에서 절반폭 이상 안쪽).
	const FVector ClosestPoint = ClosestLedge->FindLocationClosestToWorldLocation(HitLocation, ESplineCoordinateSpace::World);
	const float DistAtLocation = ClosestLedge->GetDistanceAlongSplineAtLocation(ClosestPoint, ESplineCoordinateSpace::World);
	const float HalfWidth = MinLedgeWidth * 0.5f;
	const float ClampedDist = FMath::Clamp(DistAtLocation, HalfWidth, SplineLength - HalfWidth);
	const FTransform FrontXform = ClosestLedge->GetTransformAtDistanceAlongSpline(ClampedDist, ESplineCoordinateSpace::World);

	Result.bHasFrontLedge = true;
	Result.FrontLedgeLocation = FrontXform.GetLocation();
	Result.FrontLedgeNormal = FrontXform.GetRotation().GetUpVector();

	// 4. 맵으로 반대편(back) 렛지 조회.
	const TObjectPtr<USplineComponent>* FoundOpposite = OppositeLedges.Find(ClosestLedge);
	USplineComponent* OppositeLedge = FoundOpposite ? FoundOpposite->Get() : nullptr;

	// 5. front 렛지 위치 기준으로 back 렛지 최근접 transform.
	if (IsValid(OppositeLedge))
	{
		const FTransform BackXform = OppositeLedge->FindTransformClosestToWorldLocation(Result.FrontLedgeLocation, ESplineCoordinateSpace::World);
		Result.bHasBackLedge = true;
		Result.BackLedgeLocation = BackXform.GetLocation();
		Result.BackLedgeNormal = BackXform.GetRotation().GetUpVector();
	}
	else
	{
		Result.bHasBackLedge = false;
	}
}
