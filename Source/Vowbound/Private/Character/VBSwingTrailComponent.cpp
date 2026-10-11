// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Character/VBSwingTrailComponent.h"

#include "ProceduralMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBSwingTrailProfile.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 한 프레임을 이보다 잘게 쪼개지 않는다. 히트스톱이 풀린 첫 프레임처럼 긴 프레임에서도 조각 수가 폭주하지 않게.
	constexpr int32 MaxSubstepsPerFrame = 32;
	// 끝 신호가 안 와도 궤적을 스스로 닫는 한계 = 노티 길이 x 배수 + 여유. 재생 속도가 느리거나 히트스톱이 길어도 정상 궤적은 안 걸리게 넉넉히 둔다.
	constexpr float MaxOpenDurationScale = 3.0f;
	constexpr float MaxOpenSlackSeconds = 0.5f;
}

UVBSwingTrailComponent::UVBSwingTrailComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	// 애니 평가가 끝난 뒤의 뼈를 읽는다(판정 컴포넌트와 같다). 궤적이 열린 동안만 켠다.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

UVBSwingTrailComponent* UVBSwingTrailComponent::FindOrAdd(AActor* Owner)
{
	if (!Owner || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}
	if (UVBSwingTrailComponent* Existing = Owner->FindComponentByClass<UVBSwingTrailComponent>())
	{
		return Existing;
	}
	UVBSwingTrailComponent* Created = NewObject<UVBSwingTrailComponent>(Owner, TEXT("SwingTrail"), RF_Transient);
	Created->Trace = Owner->FindComponentByClass<UVBMeleeTraceComponent>();
	Created->RegisterComponent();
	if (USkeletalMeshComponent* Mesh = Created->GetOwnerMesh())
	{
		Created->AddTickPrerequisiteComponent(Mesh);
	}
	return Created;
}

USkeletalMeshComponent* UVBSwingTrailComponent::GetOwnerMesh() const
{
	return Trace.IsValid() ? Trace->GetMesh() : nullptr;
}

void UVBSwingTrailComponent::BeginTrail(const void* Key, FGameplayTag ShapeTag, const UVBSwingTrailProfile* OverrideProfile, float ExpectedDuration)
{
	// 같은 노티가 끝 없이 다시 열리면(몽타주를 처음부터 다시 틀 때) 옛 궤적은 닫고 새로 연다.
	EndTrail(Key);

	FTrail Trail;
	Trail.Key = Key;
	Trail.MaxOpenTime = FMath::Max(0.0f, ExpectedDuration) * MaxOpenDurationScale + MaxOpenSlackSeconds;
	if (!Trace.IsValid() || !Trace->ResolveShape(ShapeTag, Trail.Shape))
	{
		VB_LOG(Warning, "%s: 궤적 모양 %s 를 못 풀었다 - 판정 컴포넌트와 모양 자산을 확인(궤적 없이 진행)",
		       *GetNameSafe(GetOwner()), *ShapeTag.ToString());
		return;
	}
	const UVBSwingTrailProfile* Profile = OverrideProfile ? OverrideProfile : Trail.Shape.TrailProfile.Get();
	if (!Profile)
	{
		VB_LOG(Warning, "%s: 궤적 프로필이 없다 - 노티에도 모양 %s 에도 없다(궤적 없이 진행)",
		       *GetNameSafe(GetOwner()), *ShapeTag.ToString());
		return;
	}
	Trail.Profile = Profile;

	// 휘두름 소리는 궤적마다 한 번, 날 가운데에서 낸다. 무작위·음높이 변주는 사운드 큐가 한다.
	USkeletalMeshComponent* OwnerMesh = GetOwnerMesh();
	if (Profile->WhooshSound && OwnerMesh)
	{
		const FTransform Bone = OwnerMesh->GetSocketTransform(Trail.Shape.Bone, RTS_World);
		UGameplayStatics::PlaySoundAtLocation(this, Profile->WhooshSound, Bone.TransformPosition((Trail.Shape.BaseLocal + Trail.Shape.TipLocal) * 0.5f));
	}

	if (Profile->TrailMaterial)
	{
		Trail.Mesh = AcquireTrailMesh(*Profile);
	}

	Trails.Add(MoveTemp(Trail));
	SetComponentTickEnabled(true);
}

UProceduralMeshComponent* UVBSwingTrailComponent::AcquireTrailMesh(const UVBSwingTrailProfile& Profile)
{
	UProceduralMeshComponent* TrailMesh = nullptr;
	while (!TrailMesh && FreeMeshes.Num() > 0)
	{
		TrailMesh = FreeMeshes.Pop(EAllowShrinking::No).Get();
	}
	if (!TrailMesh)
	{
		// 띠는 월드 좌표로 짓는다 - 붙이지 않고 항등 변환에 둔다. 캐릭터가 움직여도 이미 지나간 자리는 그 자리에 남아야 궤적이다.
		TrailMesh = NewObject<UProceduralMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		TrailMesh->SetUsingAbsoluteLocation(true);
		TrailMesh->SetUsingAbsoluteRotation(true);
		TrailMesh->SetUsingAbsoluteScale(true);
		TrailMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TrailMesh->SetCastShadow(false);
		TrailMesh->RegisterComponent();
		TrailMesh->SetWorldTransform(FTransform::Identity);
	}
	// 머티리얼은 무기별 인스턴스를 그대로 꽂는다 - 스윙마다 동적 인스턴스를 만들지 않는다(색은 인스턴스가 홈).
	TrailMesh->SetMaterial(0, Profile.TrailMaterial);
	TrailMesh->SetMaterial(1, Profile.DistortionMaterial);
	TrailMesh->SetVisibility(true);
	return TrailMesh;
}

void UVBSwingTrailComponent::ReleaseTrailMesh(FTrail& Trail)
{
	if (UProceduralMeshComponent* TrailMesh = Trail.Mesh.Get())
	{
		TrailMesh->ClearAllMeshSections();
		TrailMesh->SetVisibility(false);
		FreeMeshes.Add(TrailMesh);
	}
	Trail.Mesh.Reset();
}

void UVBSwingTrailComponent::EndTrail(const void* Key)
{
	for (FTrail& Trail : Trails)
	{
		if (Trail.Key == Key && !Trail.bEnding)
		{
			Trail.bEnding = true;
		}
	}
}

void UVBSwingTrailComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// DeltaTime 에는 소유 액터의 시간 배율이 이미 곱해져 있다 - 히트스톱이 공격자를 늦추면 궤적도 그 자리에 멈춘다.
	USkeletalMeshComponent* OwnerMesh = GetOwnerMesh();
	for (int32 Index = Trails.Num() - 1; Index >= 0; --Index)
	{
		FTrail& Trail = Trails[Index];
		const UVBSwingTrailProfile* Profile = Trail.Profile.Get();
		const float Lifetime = Profile ? Profile->GetMaxSegmentLifetime() : 0.0f;

		for (FSegment& Segment : Trail.Segments)
		{
			Segment.Age += DeltaTime;
		}
		if (!Trail.bEnding)
		{
			Trail.OpenTime += DeltaTime;
			if (Trail.OpenTime > Trail.MaxOpenTime)
			{
				// 정상이면 노티 끝이 먼저 닫는다. 여기 닿았다면 끝 신호를 잃은 것이다 - 조용히 붙어 있지 않게 닫고 알린다.
				VB_LOG(Warning, "%s: 휘두름 궤적이 %.2f초 동안 끝 신호를 못 받아 스스로 닫는다(노티 끝 짝 찾기를 확인)",
				       *GetNameSafe(GetOwner()), Trail.OpenTime);
				Trail.bEnding = true;
			}
		}
		if (!Trail.bEnding && OwnerMesh && Profile)
		{
			AppendSegments(Trail, OwnerMesh->GetSocketTransform(Trail.Shape.Bone, RTS_World), DeltaTime);
		}
		Trail.Segments.RemoveAll([Lifetime](const FSegment& Segment) { return Segment.Age >= Lifetime; });

		if (!Profile || (Trail.bEnding && Trail.Segments.Num() == 0))
		{
			ReleaseTrailMesh(Trail);
			Trails.RemoveAt(Index);
			continue;
		}
		RebuildMesh(Trail);
	}

	if (Trails.Num() == 0)
	{
		SetComponentTickEnabled(false);
	}
}

void UVBSwingTrailComponent::AppendSegments(FTrail& Trail, const FTransform& Current, float DeltaTime) const
{
	const FVBResolvedMeleeShape& Shape = Trail.Shape;
	if (!Trail.bHasPrev)
	{
		Trail.PrevBone = Current;
		Trail.bHasPrev = true;
		Trail.Segments.Add({Current.TransformPosition(Shape.BaseLocal), Current.TransformPosition(Shape.TipLocal), 0.0f});
		return;
	}

	// 판정 컴포넌트와 같은 보간 - 두 자세 사이를 회전까지 섞어 호를 따른다. 날 끝이 SubstepSpacing 만큼 갈 때마다 한 조각.
	const float TipTravel = FVector::Dist(Trail.PrevBone.TransformPosition(Shape.TipLocal), Current.TransformPosition(Shape.TipLocal));
	const float BaseTravel = FVector::Dist(Trail.PrevBone.TransformPosition(Shape.BaseLocal), Current.TransformPosition(Shape.BaseLocal));
	const float Spacing = Trail.Profile.IsValid() ? Trail.Profile->SubstepSpacing : 8.0f;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(FMath::Max(TipTravel, BaseTravel) / Spacing), 1, MaxSubstepsPerFrame);
	for (int32 Step = 1; Step <= Steps; ++Step)
	{
		const float Alpha = static_cast<float>(Step) / Steps;
		FTransform Blended;
		Blended.Blend(Trail.PrevBone, Current, Alpha);
		// 프레임 안에서 먼저 지나간 자리일수록 조금 더 늙었다 - 띠 꼬리가 프레임 단위 계단 없이 사라진다.
		Trail.Segments.Add({Blended.TransformPosition(Shape.BaseLocal), Blended.TransformPosition(Shape.TipLocal), DeltaTime * (1.0f - Alpha)});
	}
	Trail.PrevBone = Current;
}

void UVBSwingTrailComponent::RebuildMesh(FTrail& Trail)
{
	UProceduralMeshComponent* TrailMesh = Trail.Mesh.Get();
	const UVBSwingTrailProfile* Profile = Trail.Profile.Get();
	if (!TrailMesh || !Profile)
	{
		return;
	}
	// 구역마다 자기 수명으로 짓는다 - 띠는 날에 바짝 붙어 짧게, 공기 왜곡은 띠가 걷힌 뒤에도 그 자리에 남는다(설계 20절).
	BuildSection(*TrailMesh, Trail, 0, Profile->SegmentLifetime);
	if (Profile->DistortionMaterial)
	{
		BuildSection(*TrailMesh, Trail, 1, Profile->DistortionLifetime);
	}
}

void UVBSwingTrailComponent::BuildSection(UProceduralMeshComponent& TrailMesh, const FTrail& Trail, int32 SectionIndex, float Lifetime)
{
	// 조각은 배열 뒤로 갈수록 새것이다(나이가 줄어든다) - 이 수명 안의 조각은 배열 꼬리의 한 덩어리다.
	const TArray<FSegment>& Segments = Trail.Segments;
	int32 First = 0;
	while (First < Segments.Num() && Segments[First].Age >= Lifetime)
	{
		++First;
	}
	const int32 Count = Segments.Num() - First;
	if (Count < 2)
	{
		// 이미 빈 구역을 매 프레임 다시 비우지 않는다(비울 때마다 렌더 상태를 새로 만든다).
		const FProcMeshSection* Section = TrailMesh.GetProcMeshSection(SectionIndex);
		if (Section && Section->ProcVertexBuffer.Num() > 0)
		{
			TrailMesh.ClearMeshSection(SectionIndex);
		}
		return;
	}

	// 멤버 버퍼를 비우기만 하고 용량은 둔다 - 매 프레임 새로 할당하지 않는다.
	TArray<FVector>& Vertices = ScratchVertices;
	TArray<int32>& Triangles = ScratchTriangles;
	TArray<FVector>& Normals = ScratchNormals;
	TArray<FVector2D>& UV0 = ScratchUV0;
	TArray<FLinearColor>& Colors = ScratchColors;
	const TArray<FProcMeshTangent> Tangents;
	Vertices.Reset(Count * 2);
	Normals.Reset(Count * 2);
	UV0.Reset(Count * 2);
	Colors.Reset(Count * 2);
	Triangles.Reset((Count - 1) * 6);

	// 정점 규약은 UVBSwingTrailProfile::TrailMaterial 주석에 있다(알파 = 이 구역 수명 안의 남은 비율, UV.X = 나이 비율, UV.Y = 날 시작 0 / 끝 1).
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FSegment& Segment = Segments[First + Index];
		const float AgeRatio = FMath::Clamp(Segment.Age / Lifetime, 0.0f, 1.0f);
		const FVector Along = Segment.Tip - Segment.Base;
		const FVector Motion = (Index + 1 < Count) ? (Segments[First + Index + 1].Tip - Segment.Tip) : (Segment.Tip - Segments[First + Index - 1].Tip);
		const FVector Normal = FVector::CrossProduct(Along, Motion).GetSafeNormal();
		const FLinearColor Color(1.0f, 1.0f, 1.0f, 1.0f - AgeRatio);

		Vertices.Add(Segment.Base);
		Vertices.Add(Segment.Tip);
		Normals.Add(Normal);
		Normals.Add(Normal);
		UV0.Add(FVector2D(AgeRatio, 0.0f));
		UV0.Add(FVector2D(AgeRatio, 1.0f));
		Colors.Add(Color);
		Colors.Add(Color);
	}
	for (int32 Index = 0; Index + 1 < Count; ++Index)
	{
		const int32 A = Index * 2;
		Triangles.Append({A, A + 1, A + 2, A + 1, A + 3, A + 2});
	}

	TrailMesh.CreateMeshSection_LinearColor(SectionIndex, Vertices, Triangles, Normals, UV0, Colors, Tangents, /*bCreateCollision=*/false);
}

void UVBSwingTrailComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 쓰던 띠와 풀의 띠를 모두 부순다 - 이 컴포넌트가 만든 것들이다.
	for (FTrail& Trail : Trails)
	{
		ReleaseTrailMesh(Trail);
	}
	Trails.Reset();
	for (const TWeakObjectPtr<UProceduralMeshComponent>& Pooled : FreeMeshes)
	{
		if (UProceduralMeshComponent* TrailMesh = Pooled.Get())
		{
			TrailMesh->DestroyComponent();
		}
	}
	FreeMeshes.Reset();
	Super::EndPlay(EndPlayReason);
}
