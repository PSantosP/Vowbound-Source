// Copyright Vowbound. All Rights Reserved.

#include "Traversal/VBLadder.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AVBLadder::AVBLadder()
{
	PrimaryActorTick.bCanEverTick = false;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	SetRootComponent(Pivot);

	Parts = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Parts"));
	Parts->SetupAttachment(Pivot);
	// 타는 사람은 트래버설 컴포넌트가 배치하므로 형체가 막을 필요가 없다.
	//   막게 두면 사다리에 붙는 순간 캡슐이 형체와 싸운다.
	Parts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Parts->SetGenerateOverlapEvents(false);
	// 기본 메시는 엔진 큐브다. 진짜 사다리 메시가 생기면 PartMesh 를 갈아끼운다.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		PartMesh = CubeFinder.Object;
	}

	Marker = CreateDefaultSubobject<UBoxComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Pivot);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetGenerateOverlapEvents(false);
	Marker->SetBoxExtent(FVector(5.0f, 30.0f, 100.0f));
	Marker->SetHiddenInGame(true);
}

void AVBLadder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildVisual();
}

void AVBLadder::RebuildVisual()
{
	if (!Parts)
	{
		return;
	}
	Parts->SetStaticMesh(PartMesh);
	Parts->ClearInstances();
	if (!PartMesh || LadderHeight <= 0.0f)
	{
		return;
	}
	// 엔진 기본 도형은 한 변(지름·높이) 100 이라 «원하는 치수 / 100» 이 배율이다.
	//   이 100 은 그 메시의 성질이지 튜닝값이 아니다(Cube·Cylinder 둘 다 반범위 50 실측).
	const float CubeSide = 100.0f;
	const float T = FMath::Max(PartThickness, 1.0f) / CubeSide;
	const float HalfW = LadderWidth * 0.5f;

	// 세로대 둘. 액터 앞(+X)이 바깥 법선이므로 대는 x=0 평면에 둔다 - 타는 면이 곧 축면이다.
	// 메시는 원통을 쓴다(축이 Z). 각진 큐브는 사다리로 안 보이고, 무엇보다 손이 감쌀 모양이 아니다
	//   (2026-09-19 user 「손 모양이 이 사진이랑 알맞게 안되잖아」).
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		FTransform Rail;
		Rail.SetLocation(FVector(0.0f, HalfW * Side, LadderHeight * 0.5f));
		Rail.SetScale3D(FVector(T, T, LadderHeight / CubeSide));
		Parts->AddInstance(Rail);
	}

	// 가로대. 바닥에서 RungSpacing 마다 꼭대기까지.
	// 세로대 면에서 뒤로(-X) RungDepth 만큼 물러나 있다. 클립이 손과 발을 26cm 떨어진 두 면에
	//   두기 때문이다 - 손은 세로대(x=0)를 잡고 발은 그보다 뒤에 있는 가로대를 밟는다.
	//   평평한 사다리로 만들면 둘 중 하나가 26cm 어긋나는데 IK 상한 6cm 로는 못 메운다.
	// 가로대를 «그 깊이를 통째로 채우는 판» 으로 만들었다가 되돌렸다(2026-09-19 user 「이거 왜 튀어나옴」).
	//   접촉은 맞았지만 31cm 두께 판이 여덟 개 붙으니 사다리가 아니라 계단으로 보였다.
	//   지금은 얇은 봉이고, 세로대와는 짧은 브래킷 둘로 잇는다 - 벽에서 띄워 단 사다리의 모양이다.
	//   접촉면은 그대로다: 손은 x=0, 발은 x=-RungDepth.
	// 디딤 간격을 정수로 쪼갠 것이 «보이는 간격» 이다. 쪼개도 디딤은 여전히 가로대 위에 떨어진다.
	const float Spacing = FMath::Max(RungSpacing, 5.0f) / FMath::Max(RungSubdivision, 1);
	const float Depth = FMath::Max(RungDepth, 0.0f);
	const int32 Count = FMath::Max(1, FMath::FloorToInt(LadderHeight / Spacing));
	// 격자를 통째로 내린다. 내린 만큼 맨 아래 것이 바닥 밑으로 가므로 그것만 건너뛴다.
	const float Drop = FMath::Clamp(RungDropOffset, 0.0f, Spacing);
	for (int32 i = 0; i <= Count + 1; ++i)
	{
		const float Z = i * Spacing - Drop;
		if (Z < 0.0f || Z > LadderHeight)
		{
			continue;
		}
		FTransform Rung;
		Rung.SetLocation(FVector(-Depth, 0.0f, Z));
		// 원통의 축은 Z 다. 가로대는 좌우(Y)로 누워야 하므로 X 축으로 90도 돌린다.
		//   배율은 회전 «전» 의 지역 공간에 걸리므로, 길이는 여기서도 Z 성분이다.
		Rung.SetRotation(FQuat(FVector::ForwardVector, HALF_PI));
		Rung.SetScale3D(FVector(T, T, LadderWidth / CubeSide));
		Parts->AddInstance(Rung);

		// 브래킷 둘. 세로대(x=0)에서 가로대(x=-Depth)까지 잇는다. 깊이가 0 이면 만들지 않는다.
		if (Depth > KINDA_SMALL_NUMBER)
		{
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				FTransform Arm;
				Arm.SetLocation(FVector(-Depth * 0.5f, HalfW * Side, Z));
				// 브래킷은 앞뒤(X)로 눕는다 - 원통 축 Z 를 Y 축으로 90도 돌리면 X 를 향한다.
				Arm.SetRotation(FQuat(FVector::RightVector, HALF_PI));
				Arm.SetScale3D(FVector(T, T, Depth / CubeSide));
				Parts->AddInstance(Arm);
			}
		}
	}
}

FVector AVBLadder::GetAxisPoint(float Height) const
{
	return GetActorLocation() + GetActorUpVector() * FMath::Clamp(Height, 0.0f, LadderHeight);
}

FVector AVBLadder::GetFacingNormal() const
{
	return GetActorForwardVector();
}

float AVBLadder::ProjectToHeight(const FVector& WorldPoint) const
{
	const float Raw = static_cast<float>(FVector::DotProduct(WorldPoint - GetActorLocation(), GetActorUpVector()));
	return FMath::Clamp(Raw, 0.0f, LadderHeight);
}
