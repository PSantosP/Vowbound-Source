// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"   // EVisibilityBasedAnimTickOption
#include "Engine/EngineTypes.h"                 // ECollisionChannel
#include "Engine/HitResult.h"
#include "GameplayTagContainer.h"
#include "Types/VBWeaponStateTypes.h"      // EVBWeaponType - 날 풀기 창구가 값으로 받는다
#include "VBMeleeTraceComponent.generated.h"

namespace VBCollision
{
	// 근접 판정 채널(DefaultEngine.ini 의 VBMeleeTrace). 판정 쪽 기본값과 «맞는 쪽» 이 같이 읽는다 -
	//   쓰러진 몸의 래그돌 메시가 이 채널에 응답을 켠다(AVBEnemyBase::ApplyRecoverableRagdoll, 설계 .sw/downed-hit).
	inline constexpr ECollisionChannel MeleeTrace = ECC_GameTraceChannel3;
}

class UVBMeleeTraceShapeSet;
class UVBSwingTrailProfile;
class UVBWeaponVisualConfig;
class USkeletalMeshComponent;
struct FVBMeleeTraceShapeDef;

/** 풀어 낸 날 하나 - 어느 뼈 공간의 어느 선분인가. */
struct FVBResolvedMeleeShape
{
	FName Bone;
	FVector BaseLocal = FVector::ZeroVector;
	FVector TipLocal = FVector::ZeroVector;
	float Radius = 0.0f;
	// 실제로 풀린 모양의 기본 궤적(대체 모양으로 떨어졌으면 그 모양의 것). 판정은 안 쓴다.
	TWeakObjectPtr<const UVBSwingTrailProfile> TrailProfile;

	bool IsValid() const { return !Bone.IsNone() && Radius > 0.0f && !BaseLocal.Equals(TipLocal); }
};

// 이번 프레임에 «새로» 맞은 표적들. 표적당 가장 이른 접촉 하나씩이고, 벽·중복 검사를 통과한 것만 온다.
DECLARE_DELEGATE_OneParam(FVBMeleeTraceHits, const TArray<FHitResult>& /*NewHits*/);

/**
 * 무기 궤적 적중 판정. 설계 .sw/weapon-trace 2·4·6·8절.
 *
 * 공격 GA 가 판정 창을 열면(BeginWindow), 창이 닫힐 때까지 매 프레임 «날» 이 지난 프레임에서 이번 프레임까지 지나간 자리를
 *   캡슐로 쓸어 새로 맞은 표적을 GA 에 넘긴다. 맞은 뒤의 처리(피해·큐·히트스톱)는 GA 의 일이다 - 이 컴포넌트는 «어디에 닿았나» 만 안다.
 *
 * 왜 GA 가 아니라 컴포넌트인가:
 *   - 뼈 위치는 애니 평가가 끝난 뒤에 읽어야 한다. 이 컴포넌트는 TG_PostUpdateWork 에 메시 뒤로 틱한다.
 *   - 창이 열린 동안 공격자 메시의 뼈 갱신을 강제로 켠다. 켜고 끄는 주인이 한 곳이어야 GA 가 끊겨도 복원된다.
 *   - 휘두름 궤적(UVBSwingTrailComponent)이 같은 날(ResolveShape)과 같은 메시(GetMesh)를 읽는다.
 *
 * 권위: 창은 서버에서만 연다(피해·GE 는 서버만 낸다 - FIND-085 계약). 클라에서 부르면 거절하고 경고한다.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBMeleeTraceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBMeleeTraceComponent();

	// 창을 연다. RehitIntervalSeconds 가 0 이면 창 안에서 표적당 한 번, 0 보다 크면 그 간격(애니 시간)마다 다시 맞힌다.
	//   날을 못 풀면(모양 없음·무기 메시 없음) INDEX_NONE 을 돌려주고 경고한다.
	int32 BeginWindow(FGameplayTag ShapeTag, float RehitIntervalSeconds, FVBMeleeTraceHits OnHits);
	// 정상 노티 종료만 마지막 자세까지 검사한다. 취소·섹션 점프는 즉시 닫는다.
	void EndWindow(int32 WindowId, bool bSweepFinalPose = false);
	void EndAllWindows();
	bool HasOpenWindows() const { return Windows.Num() > 0; }

	// 날을 푼다. 휘두름 이펙트도 이 함수로 같은 날을 따른다(판정과 궤적이 같은 칼).
	bool ResolveShape(FGameplayTag ShapeTag, FVBResolvedMeleeShape& OutShape) const;

	// 날 풀기의 홈. 위 함수(지금 든 무기)와 저작 도구가 같은 계산을 쓴다 - 측정 스크립트에 칼 치수를 다시 적지 않게(설계 19절 6).
	static bool ResolveShapeFromData(const UVBMeleeTraceShapeSet* InShapeSet, FGameplayTag ShapeTag, const UVBWeaponVisualConfig* VisualConfig,
	                                 EVBWeaponType WeaponType, FVBResolvedMeleeShape& OutShape);

	// 저작 도구(Content/Python/weapon_trace_tools.py)가 부르는 창구. 게임 코드는 쓰지 않는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Combat")
	static bool ResolveShapeForTools(const UVBMeleeTraceShapeSet* InShapeSet, FGameplayTag ShapeTag, const UVBWeaponVisualConfig* VisualConfig,
	                                 EVBWeaponType WeaponType, FName& OutBone, FVector& OutBaseLocal, FVector& OutTipLocal, float& OutRadius);
	// 모양의 뼈가 사는 메시. 휘두름 궤적도 이 메시를 읽는다 - 따로 찾으면 판정과 궤적이 다른 메시를 볼 수 있다.
	USkeletalMeshComponent* GetMesh() const;
	// 끝점 사이 거리뿐 아니라 회전·배율 변화가 만드는 이동량으로 보간 밀도를 정한다.
	static int32 CalculateSweepSteps(const FVBResolvedMeleeShape& Shape, const FTransform& A, const FTransform& B, int32 StepLimit);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 이 캐릭터가 휘두를 수 있는 날들. 캐릭터 BP 마다 꽂는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBMeleeTraceShapeSet> ShapeSet;

	// 판정 채널. 캐릭터 몸(CharacterMesh 프로필)만 Overlap 으로 답하고 나머지는 무시한다(DefaultEngine.ini 의 VBMeleeTrace).
	//   캡슐이 아니라 몸에 닿아야 맞는다 - 캡슐은 몸보다 커서 «칼이 안 닿았는데 맞는» 원인이다.
	//   맞는 쪽(쓰러진 몸의 래그돌)은 VBCollision::MeleeTrace 를 읽으므로 이 값을 캐릭터마다 바꾸면 그 몸에 안 닿는다.
	//   2026-10-01 기준 이 값을 덮어쓴 BP 는 없다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TEnumAsByte<ECollisionChannel> TraceChannel = VBCollision::MeleeTrace;

	// 한 프레임의 이동·회전 거리 / 반경 만큼 쪼갠다. 끝점 간 직선 거리만 재면 긴 칼의 회전을 적게 나눈다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxSubsteps = 16;

	// 벽 너머 표적을 버린다: 공격자 가슴 높이에서 접점까지 Visibility 선이 월드에 막히면 안 맞은 것으로 친다.
	//   채널에서 월드를 Block 으로 두지 않는 이유는 설계 15절(Trigger 류까지 스윕을 멈춘다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	bool bRejectHitsBehindWalls = true;

	// 벽 검사 선의 시작 높이(액터 위치 기준, cm).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(EditCondition="bRejectHitsBehindWalls"))
	float WallCheckHeight = 50.0f;

private:
	friend class FVBMeleeTraceWorldCollisionTest;
	struct FWindow
	{
		int32 Id = INDEX_NONE;
		FVBResolvedMeleeShape Shape;
		float RehitInterval = 0.0f;
		FVBMeleeTraceHits OnHits;
		FTransform PrevBone;
		bool bHasPrev = false;
		bool bQueriedInitialPose = false;
		// 창이 열린 뒤 흐른 시간. 공격자의 시간 배율(히트스톱)이 곱해진 틱 시간이라 애니 시간과 같이 간다.
		float Elapsed = 0.0f;
		// 정상 종료 검사와 컴포넌트 틱이 같은 프레임에 와도 다단 히트 시간을 두 번 더하지 않는다.
		uint64 LastSampleFrame = MAX_uint64;
		TMap<TWeakObjectPtr<AActor>, float> LastHitTime;
	};
	TArray<FWindow> Windows;
	int32 NextWindowId = 1;

	static bool ResolveShapeDef(const UVBMeleeTraceShapeSet* InShapeSet, const FVBMeleeTraceShapeDef& Def, const UVBWeaponVisualConfig* VisualConfig,
	                            EVBWeaponType WeaponType, FVBResolvedMeleeShape& OutShape, int32 Depth);

	// 날 하나를 A 자세에서 B 자세까지 캡슐로 쓴다. 표적마다 가장 이른 접촉을 BestPerActor 에 모은다(단계 번호가 이른 쪽).
	void SweepSegment(const FVBResolvedMeleeShape& Shape, const FTransform& A, const FTransform& B, int32 StepIndex,
	                  TMap<AActor*, TPair<int32, FHitResult>>& BestPerActor) const;
	// 첫 자세의 겹침·프레임 사이 이동·정상 종료의 마지막 구간이 같은 판정과 중복 제거를 쓴다.
	void GatherWindowHits(FWindow& Window, const FTransform& Current, float DeltaTime, TArray<FHitResult>& OutHits) const;
	// 표적 접점을 채우고 벽 너머면 거짓.
	bool FinalizeContact(FHitResult& Hit) const;

	// 뼈 갱신 강제. 창 수를 세서 마지막 창이 닫힐 때만 원래 값으로 되돌린다.
	void AcquireBoneRefresh();
	void ReleaseBoneRefresh();
	int32 BoneRefreshCount = 0;
	EVisibilityBasedAnimTickOption SavedTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPose;
	bool bSavedUpdateRateOptimizations = false;
	TWeakObjectPtr<USkeletalMeshComponent> RefreshedMesh;
};
