// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Character/VBMeleeTraceComponent.h" // FVBResolvedMeleeShape - 궤적은 판정과 같은 날을 따른다
#include "VBSwingTrailComponent.generated.h"

class UProceduralMeshComponent;
class UVBSwingTrailProfile;

/**
 * 휘두름 궤적 띠. 설계 .sw/weapon-trace 18절.
 *
 * 궤적 노티(UVBANS_WeaponTrail)가 열고 닫는다. 열린 동안 매 프레임 «날» 이 지난 프레임에서 이번 프레임까지 지나간 자리를
 *   변환 보간으로 잘게 채워 띠 조각을 쌓고, 조각은 수명이 다하면 사라진다. 날은 UVBMeleeTraceComponent 의 모양을 그대로 쓴다 -
 *   보이는 궤적과 판정이 쓴 칼이 같은 데이터에서 나온다.
 *
 * 왜 노티가 아니라 컴포넌트가 상태를 드나: 노티 인스턴스는 몽타주 자산 하나에 하나라 그 몽타주를 트는 모든 액터가 나눠 쓴다.
 * 왜 생성자가 아니라 처음 쓸 때 붙이나: 치장이다. 없어도 게임 규칙은 돈다 - 판정 컴포넌트와 달리 빠뜨려서 조용히 틀어지는 것이 없다.
 * 전용 서버에서는 궤적을 만들지 않는다(그릴 화면이 없다).
 */
UCLASS(ClassGroup=(Vowbound))
class VOWBOUND_API UVBSwingTrailComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBSwingTrailComponent();

	// 액터에 없으면 만들어 붙인다. 전용 서버면 nullptr.
	static UVBSwingTrailComponent* FindOrAdd(AActor* Owner);

	// Key = 궤적을 연 노티 객체(같은 노티의 시작·끝이 같은 궤적을 가리키게). OverrideProfile 이 비면 모양의 기본 궤적.
	// ExpectedDuration = 노티 길이(애니 시간). 끝 신호가 끝내 안 오면 이 길이의 몇 배가 지나 스스로 닫는다.
	void BeginTrail(const void* Key, FGameplayTag ShapeTag, const UVBSwingTrailProfile* OverrideProfile, float ExpectedDuration);
	// 새 조각을 더 쌓지 않는다. 남은 조각이 다 사라지면 띠를 치운다.
	void EndTrail(const void* Key);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FSegment
	{
		FVector Base;
		FVector Tip;
		float Age = 0.0f;
	};

	struct FTrail
	{
		const void* Key = nullptr;
		FVBResolvedMeleeShape Shape;
		TWeakObjectPtr<const UVBSwingTrailProfile> Profile;
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;
		FTransform PrevBone;
		bool bHasPrev = false;
		bool bEnding = false;
		// 열린 뒤 흐른 시간(공격자 시간)과 스스로 닫는 한계. 끝 신호를 잃은 궤적이 영원히 붙어 있지 않게 한다.
		float OpenTime = 0.0f;
		float MaxOpenTime = 0.0f;
		TArray<FSegment> Segments;
	};
	TArray<FTrail> Trails;

	// 모양과 메시의 주인. 궤적은 판정 컴포넌트가 보는 메시의 뼈를 읽는다(모양의 뼈가 그 메시 기준이다).
	TWeakObjectPtr<const UVBMeleeTraceComponent> Trace;

	// 다 쓴 띠를 부수지 않고 숨겨 두었다가 다음 스윙에 다시 쓴다 - 스윙마다 컴포넌트를 만들고 부수지 않게(설계 19절 4).
	//   액터가 소유하므로(Outer = 액터) 가비지 수거에서 안전하다. 약한 포인터는 액터가 먼저 사라지는 경우를 위한 것이다.
	TArray<TWeakObjectPtr<UProceduralMeshComponent>> FreeMeshes;

	// 띠를 다시 지을 때 쓰는 정점 버퍼. 매 프레임 새로 할당하지 않게 멤버로 두고 용량을 유지한다.
	TArray<FVector> ScratchVertices;
	TArray<int32> ScratchTriangles;
	TArray<FVector> ScratchNormals;
	TArray<FVector2D> ScratchUV0;
	TArray<FLinearColor> ScratchColors;

	USkeletalMeshComponent* GetOwnerMesh() const;
	UProceduralMeshComponent* AcquireTrailMesh(const UVBSwingTrailProfile& Profile);
	void ReleaseTrailMesh(FTrail& Trail);
	void AppendSegments(FTrail& Trail, const FTransform& Current, float DeltaTime) const;
	void RebuildMesh(FTrail& Trail);
	// 한 구역을 그 구역의 수명 안에 있는 조각만으로 짓는다(띠 = SegmentLifetime, 왜곡 = DistortionLifetime).
	void BuildSection(UProceduralMeshComponent& TrailMesh, const FTrail& Trail, int32 SectionIndex, float Lifetime);
};
