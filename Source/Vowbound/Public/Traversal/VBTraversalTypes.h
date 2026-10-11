// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h" // EVBGait
#include "VBTraversalTypes.generated.h"

class UPrimitiveComponent;
class USplineComponent;

/**
 * EVBTraversalActionType
 * 트래버설 동작 종류. GASP E_TraversalActionType 그대로 (None=0/Vault=1/Hurdle=2/Mantle=3).
 * Mantle 네스티드 chooser는 ObstacleHeight>150 일 때 Climb 자산을 반환(별도 Climb 타입 없음).
 * Hang 은 Vowbound 확장(2026-09-18) - 몽타주 한 번이 아니라 «매달려 있는 상태» 다. chooser 를 안 지난다.
 */
UENUM(BlueprintType)
enum class EVBTraversalActionType : uint8
{
	None   = 0 UMETA(DisplayName="None"),
	Vault  = 1 UMETA(DisplayName="Vault"),
	Hurdle = 2 UMETA(DisplayName="Hurdle"),
	Mantle = 3 UMETA(DisplayName="Mantle"),
	Hang   = 4 UMETA(DisplayName="Hang"),
	Climb  = 5 UMETA(DisplayName="Climb"),
};

/**
 * FVBTraversalCheckResult
 * 트래버설 트레이스 결과. GASP S_TraversalCheckResult(13필드) 그대로.
 * TryTraversalAction(=UVBTraversalComponent)이 단계별 트레이스로 채운다.
 */
USTRUCT(BlueprintType)
struct FVBTraversalCheckResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	EVBTraversalActionType ActionType = EVBTraversalActionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	bool bHasFrontLedge = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	FVector FrontLedgeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	FVector FrontLedgeNormal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	bool bHasBackLedge = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	FVector BackLedgeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	FVector BackLedgeNormal = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	bool bHasBackFloor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	FVector BackFloorLocation = FVector::ZeroVector;

	// actor↔frontledge ΔZ
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	float ObstacleHeight = 0.0f;

	// frontledge↔backledge(또는 top impact) 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	float ObstacleDepth = 0.0f;

	// backledge↔backfloor 거리
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	float BackLedgeHeight = 0.0f;

	// 트레이스가 맞은 LevelBlock_Traversable 프리미티브
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	TObjectPtr<UPrimitiveComponent> HitComponent = nullptr;

	// front 렛지 스플라인 - 매달림(Hang)이 옆걸음의 좌표축으로 쓴다. 다른 동작은 안 읽는다.
	// 레벨 액터의 컴포넌트라 이름이 안정적이어서 RPC 인자로 그대로 건너간다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	TObjectPtr<USplineComponent> FrontLedgeSpline = nullptr;
};

/**
 * FVBHangIKTargets
 * 매달림 중 네 말단이 가야 할 월드 위치. 트래버설 컴포넌트가 매 틱 채우고 ABP 가 읽어 TwoBoneIK 에 넣는다.
 * 주의: 손(HandL/HandR)은 월드 좌표가 아니라 손뼈 공간의 오프셋 벡터다(2026-09-18 개정).
 *   클립 자세를 유지한 채 밀어내기만 하려면 목표가 «지금 손에서 얼마나» 여야 하고, 그 «지금» 은 애니만 안다.
 * 발(FootL/FootR)도 2026-09-19 부터 뼈 공간 오프셋이다 - ABP 의 네 노드를 모두 BCS_BoneSpace 로
 *   통일했다(월드 공간이면 노드가 말단을 «놓아» 버려 클립 정보가 사라지고 얼어붙었다).
 *   2026-09-21 정정: 이 주석이 그 뒤로도 「발은 월드 좌표」라고 말하고 있었다. 공급자 셋(매달림·등반·
 *   사다리)이 전부 뼈 공간을 넣는데 계약 문서만 옛말이었다. 좌표 계약은 이 구조체가 아니라
 *   ABP 노드의 공간 설정이 정하므로, 공급자를 늘릴 때 그 설정을 먼저 리드백할 것.
 * Weight 는 그 말단을 실제로 IK 로 끌지 여부(0 이면 애니 그대로). 벽을 못 찾은 발은 0 이 된다.
 */
USTRUCT(BlueprintType)
struct FVBHangIKTargets
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	FVector HandL = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	FVector HandR = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	FVector FootL = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	FVector FootR = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	float HandWeightL = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	float HandWeightR = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	float FootLWeight = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Traversal")
	float FootRWeight = 0.0f;
};

/**
 * FVBTraversalChooserParams
 * CHT_VB_TraversalAnims 평가 컨텍스트. GASP S_TraversalChooserParams(5필드) 그대로.
 * 주의: 멤버명은 chooser 컬럼 바인딩명(ActionType/ObstacleHeight/ObstacleDepth/Speed)과 반드시 일치해야 한다.
 */
USTRUCT(BlueprintType)
struct FVBTraversalChooserParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	EVBTraversalActionType ActionType = EVBTraversalActionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	EVBGait Gait = EVBGait::Walk;

	// |Velocity.XY|
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	float Speed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	float ObstacleHeight = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Vowbound|Traversal")
	float ObstacleDepth = 0.0f;
};
