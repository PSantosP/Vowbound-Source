// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h" // EVBGait
#include "VBTraversalTypes.generated.h"

class UPrimitiveComponent;

/**
 * EVBTraversalActionType
 * 트래버설 동작 종류. GASP E_TraversalActionType 그대로 (None=0/Vault=1/Hurdle=2/Mantle=3).
 * Mantle 네스티드 chooser는 ObstacleHeight>150 일 때 Climb 자산을 반환(별도 Climb 타입 없음).
 */
UENUM(BlueprintType)
enum class EVBTraversalActionType : uint8
{
	None   = 0 UMETA(DisplayName="None"),
	Vault  = 1 UMETA(DisplayName="Vault"),
	Hurdle = 2 UMETA(DisplayName="Hurdle"),
	Mantle = 3 UMETA(DisplayName="Mantle"),
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
