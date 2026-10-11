// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "CharacterCommon/VBBodyControlPolicy.h"

namespace VBBodyControlPolicy
{
	bool IsCapsuleRotationOwned(const FCapsuleRotationFacts& Facts)
	{
		// 처형·래그돌·스마트 오브젝트·환경 상호작용은 정렬을 끝까지 쥔다. 공격 워프는 SkewWarp 의 Facing 이
		//   적 쪽으로, PhysicsRotation 은 카메라 쪽으로 돌려 주도권이 갈린다(2026-08-03 user FEEL).
		//   트래버설 동작은 워프 창이 아니라 몽타주 전 구간을 쥔다(FIND-083).
		return Facts.bFinisherScene || Facts.bPlayerRagdoll || Facts.bSmartObjectFacing
			|| Facts.bEnvironmentMotionFacing || Facts.bAttackWarp || Facts.bTraversalAction;
	}

	bool IsMovementFacingOwned(const FMovementFacingFacts& Facts)
	{
		// 루트모션 회전 플래그는 애님 루트모션이 도는 틱에만 효과가 있다(엔진 게이트가
		//   `bAllowPhysicsRotationDuringAnimRootMotion || !HasAnimRootMotion()` 다 - 이동 모드는 안 본다).
		//   매달림·등반·사다리처럼 루트모션 없이 캡슐을 쥐는 동안에는 이 두 방향 플래그를 꺼야 PhysicsRotation 이
		//   멈춘다(CharacterMovementComponent.cpp::PhysicsRotation 첫 줄). 2026-09-20 에 사다리가 이 목록에서 빠져
		//   캡슐이 카메라를 따라 돌았다.
		return Facts.bTraversalFacing || Facts.bFinisherScene || Facts.bPlayerRagdoll
			|| Facts.bSmartObjectFacing || Facts.bEnvironmentMotionFacing;
	}

	bool BlocksCombatCamera(const FMovementFacingFacts& Facts)
	{
		// 지금은 이동 방향 목록과 같다. 의도로 맞춘 것인지는 기록이 없다(8d3d83d 에 함께 들어왔다).
		//   목록을 한 벌로 둬서 새 소유자가 둘에 같이 들어가게 하고, 갈라야 할 때는 이 함수에서 가른다.
		return IsMovementFacingOwned(Facts);
	}

	bool BlocksEnvironmentCameraComposition(const FEnvironmentCameraFacts& Facts)
	{
		// 처형·스마트 오브젝트·환경 상호작용 중에도 구도는 바꾼다. 전투 카메라 목록을 쓰면 그 셋이 새로 막힌다.
		return Facts.bPlayerRagdoll || Facts.bTraversalFacing;
	}

	bool IsPoseOwnedByMontage(const FMontagePoseFacts& Facts)
	{
		// 사망·처형 몽타주가 TIP 에 끊겨 시체가 일어서고 처형이 안 나온 선례 둘(2026-09-13). 매달림 포즈도 덮지 않는다.
		return Facts.bDead || Facts.bFinisherScene || Facts.bTraversalPose;
	}

	bool IsAttentionPoseSuppressed(const FAttentionPoseFacts& Facts)
	{
		return Facts.bDead || Facts.bTraversalAction || Facts.bTraversalFacing || Facts.bEnvironmentMotionFacing
			|| Facts.bAttacking || Facts.bDodgeCommit || Facts.bHitReacting || Facts.bStaggered || Facts.bDowned
			|| Facts.bInteracting;
	}

	bool BlocksPlayerLookAt(const FPlayerLookAtFacts& Facts)
	{
		return Facts.bMeshSimulatingPhysics || Facts.bDead || Facts.bFinisherPlaying || Facts.bPlayerRagdoll
			|| Facts.bSmartObjectInteracting || Facts.bAttentionSuppressed;
	}

	bool BlocksEnemyLookAt(const FEnemyLookAtFacts& Facts)
	{
		return Facts.bDead || Facts.bRecoverableRagdoll || Facts.bFinisherScene
			|| Facts.bStaggered || Facts.bDowned || Facts.bHitReacting || Facts.bAttacking;
	}

	bool BlocksTraversalStart(const FTraversalStartFacts& Facts)
	{
		return Facts.bSmartObjectInteracting || Facts.bPlayerRagdoll;
	}
}
