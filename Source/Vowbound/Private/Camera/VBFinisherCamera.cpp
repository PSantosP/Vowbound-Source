// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Camera/VBFinisherCamera.h"

#include "Camera/CameraComponent.h"
#include "Character/VBFinisherComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Vowbound/Vowbound.h"

AVBFinisherCamera::AVBFinisherCamera()
{
	PrimaryActorTick.bCanEverTick = true;

	// 카메라 매니저보다 먼저 돌아야 한다. 늦으면 이번 프레임의 뷰가 지난 프레임 자리로 잡힌다.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
	SetRootComponent(CameraComponent);

	// 보는 사람의 것이라 복제 대상이 아니다. 각 기계가 자기 것을 만든다.
	bReplicates = false;

	// 처형은 슬로우가 걸린 채로 돈다. 이 액터가 함께 느려지면 키 보간이 늦게 따라가
	// 「카메라가 밀린다」로 보인다. 시간축은 씬 재생 시간이므로 틱은 실시간이어야 한다.
	SetActorTickInterval(0.0f);
	CustomTimeDilation = 1.0f;
}

void AVBFinisherCamera::Setup(const UVBFinisherConfig* InConfig, AActor* InAnchor, AActor* InTimeSource)
{
	Config     = InConfig;
	Anchor     = InAnchor;
	TimeSource = InTimeSource ? InTimeSource : InAnchor;

	// 첫 프레임을 기다리지 않고 한 번 세운다. 스폰 직후 뷰타깃 블렌드가 시작되는데
	// 그때 이 액터가 원점에 있으면 블렌드가 세계 원점에서 출발한다.
	Tick(0.0f);
}

float AVBFinisherCamera::GetSceneTime() const
{
	const AActor* Source = TimeSource.Get();
	if (!Source)
	{
		return -1.0f;
	}

	const UVBFinisherComponent* Finisher = Source->FindComponentByClass<UVBFinisherComponent>();
	return Finisher ? Finisher->GetPlaybackTime() : -1.0f;
}

void AVBFinisherCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const AActor* AnchorActor = Anchor.Get();
	const float   SceneTime   = GetSceneTime();

	// 앵커가 사라졌거나(대상 사망 후 파괴) 시간을 못 읽으면 마지막 자리를 유지한다.
	// 원점으로 떨어뜨리면 복귀 블렌드가 세계 원점에서 출발해 화면이 통째로 날아간다.
	if (!AnchorActor || SceneTime < 0.0f)
	{
		if (bHasLastGood)
		{
			SetActorTransform(LastGoodWorld);
		}
		return;
	}

	// 판정은 자산이 한다. 저작 도구와 런타임이 같은 함수를 불러야 미리보기가 거짓말을 안 한다.
	FVector RelLocation, RelLookAt;
	float   FOV = 90.0f;
	if (!Config || !Config->EvaluateCameraAt(SceneTime, RelLocation, RelLookAt, FOV))
	{
		return;
	}

	// 앵커의 회전까지 따라간다. 처형 중 앵커가 도는 클립이면 카메라도 같이 돌아야
	// 저작한 화각이 유지된다. 스케일은 쓰지 않는다 - 카메라에 스케일은 뜻이 없다.
	const FTransform AnchorWorld(AnchorActor->GetActorQuat(), AnchorActor->GetActorLocation());
	const FVector    WorldLocation = AnchorWorld.TransformPosition(RelLocation);
	const FVector    WorldLookAt   = AnchorWorld.TransformPosition(RelLookAt);

	// 회전은 여기서 딱 한 번 계산된다. 자산에는 회전이 아예 없다(헤더 LookAtOffset 주석).
	// 두 점이 겹치면 방향이 0 벡터라 회전이 튄다 - 그때는 앞 프레임 회전을 유지한다.
	const FVector ToTarget = WorldLookAt - WorldLocation;
	const FRotator LookRotation = ToTarget.IsNearlyZero()
		                              ? GetActorRotation()
		                              : ToTarget.Rotation();

	const FTransform World(LookRotation, WorldLocation);
	SetActorLocationAndRotation(WorldLocation, LookRotation);
	if (CameraComponent)
	{
		CameraComponent->SetFieldOfView(FOV);
	}

	LastGoodWorld = World;
	bHasLastGood  = true;
}

FRotator AVBFinisherCamera::GetViewRotation() const
{
	return CameraComponent ? CameraComponent->GetComponentRotation() : GetActorRotation();
}
