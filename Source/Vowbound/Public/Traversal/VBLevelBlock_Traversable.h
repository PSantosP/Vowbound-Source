// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Traversal/VBTraversalTypes.h"
#include "VBLevelBlock_Traversable.generated.h"

class USplineComponent;
class UStaticMeshComponent;
class UVBPushableComponent;

/**
 * AVBLevelBlock_Traversable
 *
 * GASP LevelBlock_Traversable 그대로. 디자이너가 장애물 위치에 배치하고, 스플라인 컴포넌트로
 * 렛지(올라설 모서리)를 정의한다. 트래버설 전방 트레이스가 이 액터를 맞추면 GetLedgeTransforms로
 * front/back 렛지 transform을 질의한다.
 *
 * 의도:
 *  - 스플라인 = 렛지 모서리. 캐릭터 위치 기준 최근접 렛지를 front 로, OppositeLedges 맵으로 back 을 찾음.
 *  - Vault/Hurdle 은 back 렛지/바닥 필요, Mantle 은 front 렛지만 필요(맨틀 MVP는 front 스플라인만으로 동작).
 */
UCLASS()
class VOWBOUND_API AVBLevelBlock_Traversable : public AActor
{
	GENERATED_BODY()

public:
	AVBLevelBlock_Traversable();

	/**
	 * GetLedgeTransforms — GASP 그대로.
	 * @param HitLocation   전방 트레이스 적중 위치(어느 렛지 지점을 노렸는지)
	 * @param ActorLocation 캐릭터 위치(최근접 렛지 선택용)
	 * @param Result        in/out — front/back 렛지 필드를 채움
	 */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Traversal")
	void GetLedgeTransforms(const FVector& HitLocation, const FVector& ActorLocation, UPARAM(ref) FVBTraversalCheckResult& Result) const;

	FORCEINLINE bool ShouldAlwaysHang() const { return bAlwaysHang; }
	FORCEINLINE float GetMinLedgeWidth() const { return MinLedgeWidth; }

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

	// 장애물 메시(트레이스 대상). 디자이너가 메시 지정.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Traversal")
	TObjectPtr<UStaticMeshComponent> ObstacleMesh;
	// 설정을 준 침대/상자만 활성. 기존 렛지와 부착물을 그대로 같이 이동한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UVBPushableComponent> Pushable;

	// 렛지 스플라인들 — 액터에 추가된 USplineComponent 자동 수집(OnConstruction).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Traversal")
	TArray<TObjectPtr<USplineComponent>> Ledges;

	// front 렛지 → 반대편(back) 렛지 매핑. GASP OppositeLedges. (Vault/Hurdle용 — Mantle은 불필요)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	TMap<TObjectPtr<USplineComponent>, TObjectPtr<USplineComponent>> OppositeLedges;

	// 렛지 최소 폭. 이보다 좁으면 front 무효 + 모서리에서 절반값만큼 클램프(코너 부유 방지). GASP MinLedgeWidth.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	float MinLedgeWidth = 60.0f;

	// 이 장애물의 렛지는 높이와 무관하게 «매달림» 으로만 잡는다(디자이너 강제).
	// 기본(false)은 벽 실높이가 정한다 - 공중에서 잡은 렛지 아래 MantleHeightMax 안에 바닥이 없으면 매달림,
	//   있으면 종전처럼 바로 올라간다(UVBTraversalComponent::ShouldHangInsteadOfMantle).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	bool bAlwaysHang = false;

	// 캐릭터 위치에 가장 가까운 렛지 스플라인 반환(없으면 nullptr).
	USplineComponent* FindLedgeClosestToActor(const FVector& ActorLocation) const;

	// OnConstruction에서 자동 생성한 옆면 렛지 추적(재구성 시 정리). 디자이너는 Front/Back만 두면 됨.
	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineComponent>> DerivedSideLedges;

	// Front/Back 두 모서리의 끝점을 이어 옆면 렛지 스플라인 생성(up=큐브중심→모서리 바깥수평).
	// 4면 큐브 맨틀용 — 디자이너가 앞/뒤 2개만 둬도 좌/우가 자동으로 생긴다.
	USplineComponent* CreateSideLedge(const FVector& A, const FVector& B, const FVector& Center);
};
