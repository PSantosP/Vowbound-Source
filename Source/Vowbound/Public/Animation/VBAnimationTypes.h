// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBAnimationTypes.generated.h"

UENUM(BlueprintType)
enum class EVBTurnInPlaceType : uint8
{
	None UMETA(DisplayName = "None"),
	TurnLeft90 UMETA(DisplayName = "Turn Left 90"),
	TurnRight90 UMETA(DisplayName = "Turn Right 90"),
	Turn180 UMETA(DisplayName = "Turn 180"),
};


UENUM(BlueprintType)
enum class EVBRootYawOffsetMode : uint8
{
	Accelerate UMETA(DisplayName = "Accelerate"),
	BlendOut UMETA(DisplayName = "Blend Out"),
	Hold UMETA(DisplayName = "Hold"),
};

UENUM(BlueprintType)
enum class EVBMMState : uint8
{
	// GASP align (2026-05-16): IsMoving = (Velocity!=0) AND (FutureVelocity!=0).
	// Stopping/Starting 별 분기는 cpp 에서 제거, nested chooser 의 IsStarting()/JustLanded() bool 이 처리.
	Idle UMETA(DisplayName = "Idle"),
	Moving UMETA(DisplayName = "Moving"),
};

// Gait는 입력 기반으로 결정된다 (GASP 방식, Step 13-2 Phase 2-0a).
// - Walk:   Walk 토글 입력 활성 (CapsLock 등)
// - Run:    기본 (Sprint/Walk 모두 비활성)
// - Sprint: Sprint 버튼 활성 (Walk보다 우선순위 높음)
// 과거의 속도 기반(GaitThreshold)은 삭제됨 — 속도 임계값 대신 입력이 직접 Gait를 결정.
UENUM(BlueprintType)
enum class EVBGait : uint8
{
	Walk UMETA(DisplayName = "Walk"),
	Run UMETA(DisplayName = "Run"),
	Sprint UMETA(DisplayName = "Sprint"),
};

// GASP E_MovementMode 대응. IsFalling bool을 enum화해 ABP의 switch 패턴(Get_OffsetRootTranslationMode,
// Get_MMBlendTime 등)을 깨끗하게 표현하기 위함.
UENUM(BlueprintType)
enum class EVBMovementMode : uint8
{
	Grounded UMETA(DisplayName = "Grounded"),
	InAir    UMETA(DisplayName = "In Air"),
};

// GASP E_RotationMode 대응. WantsToStrafe / LockedOn / ArmedCombat 결합 결과를 하나의 enum으로 노출.
// ABP/Chooser에서 분기에 사용.
UENUM(BlueprintType)
enum class EVBRotationMode : uint8
{
	OrientToMovement  UMETA(DisplayName = "Orient To Movement"), // Free: 이동 방향으로 회전
	Strafe            UMETA(DisplayName = "Strafe"),             // Strafe: 카메라 방향 유지
	OrientToTarget    UMETA(DisplayName = "Orient To Target"),   // LockOn: 타겟 방향 유지
};

// GASP E_Stance 대응. bIsCrouched bool을 enum화 (추후 Slide/Prone 등 확장 여지).
UENUM(BlueprintType)
enum class EVBStance : uint8
{
	Stand  UMETA(DisplayName = "Stand"),
	Crouch UMETA(DisplayName = "Crouch"),
};