// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 몸(캡슐 회전·방향·애님 반응·카메라 구도·시선)을 지금 어느 기능이 쥐고 있나 - 소비처별 판정의 홈.
//   소비처가 기능들에게서 사실을 모아 넘기고, 여기서는 «그 소비처를 막는가» 만 정한다. 상태가 없다.
// 왜 소비처마다 따로인가: 목록이 소비처마다 다르고, 그중 일부는 의도된 차이다 - 캡슐 회전은 트래버설 «동작» 을,
//   이동 방향은 매달림·등반·사다리를 보고, 환경 카메라는 처형을 안 본다. 하나의 bool 로 합치면 빠진 줄을 메우는
//   순간 다른 소비처의 동작까지 바뀐다.
// 진입점 하나 = 정책 하나다. 같은 정책을 여러 소비처가 나눠 쓸 수 있고(캡슐 회전 = 루트모션 회전·이동 회전 속도),
//   그때도 사실을 모으는 자리와 시점은 소비처마다 따로다.
// 사실을 «어디서 언제» 읽는지는 소비처가 정한다. 같은 사실도 소비처마다 읽는 시점이 다르다
//   (워프 대상: 이동 회전 속도는 워프 정리 앞에서, 루트모션 회전은 정리 뒤에서 읽는다).
// 소비처·원본 대응표와 의도적으로 뺀 자리 = .sw/structure-refactor/phase2-1-body-control.md 2.2·9절
//   (구조 정리 2단계 1항, 2026-10-03). 새 소유자를 더할 때는 그 표의 정책을 전부 훑는다.
namespace VBBodyControlPolicy
{
	/** 캡슐 회전 소유 - 루트모션 중 PhysicsRotation 과 이동 회전 속도가 쓴다(AVBCharacter::IsCapsuleRotationOwnedByFeature). */
	struct FCapsuleRotationFacts
	{
		bool bFinisherScene = false;           // UVBFinisherComponent::IsActorInScene
		bool bPlayerRagdoll = false;           // UVBPlayerRagdollComponent::IsRagdollActive - 일어나는 중 포함
		bool bSmartObjectFacing = false;       // UVBGASPSmartObjectComponent::OwnsCapsuleFacing
		bool bEnvironmentMotionFacing = false; // UVBInteractorComponent::OwnsCapsuleFacing
		bool bAttackWarp = false;              // AVBCharacter::IsWarpOwningRotation
		bool bTraversalAction = false;         // UVBTraversalComponent::IsDoingTraversal
	};
	VOWBOUND_API bool IsCapsuleRotationOwned(const FCapsuleRotationFacts& Facts);

	/** 이동 방향 소유 - CMC 의 OrientToMovement·ControllerDesired 를 끈다(AVBCharacter::ApplyRotationMode). */
	struct FMovementFacingFacts
	{
		bool bTraversalFacing = false;         // UVBTraversalComponent::OwnsCapsuleFacing - 매달림·등반·사다리
		bool bFinisherScene = false;
		bool bPlayerRagdoll = false;
		bool bSmartObjectFacing = false;
		bool bEnvironmentMotionFacing = false;
	};
	VOWBOUND_API bool IsMovementFacingOwned(const FMovementFacingFacts& Facts);

	/** 전투 카메라의 몸 조건. 카메라 자격(잠금 가능·로컬 조종·조준·뷰타깃)은 소비처에 남는다. */
	VOWBOUND_API bool BlocksCombatCamera(const FMovementFacingFacts& Facts);

	/** 환경 카메라 구도 변경. 조준은 소비처 조건이다. */
	struct FEnvironmentCameraFacts
	{
		bool bPlayerRagdoll = false;
		bool bTraversalFacing = false;
	};
	VOWBOUND_API bool BlocksEnvironmentCameraComposition(const FEnvironmentCameraFacts& Facts);

	/** 저작된 포즈가 몸을 쥐고 있나 - 제자리 회전(TIP) 몽타주를 막는다(UVBAnimInstance::bPoseOwnedByMontage). */
	struct FMontagePoseFacts
	{
		bool bDead = false;                    // UVBAbilitySystemStatics::IsActorDead
		bool bFinisherScene = false;
		bool bTraversalPose = false;           // UVBAnimInstance::IsTraversalOwningPose - 애님이 미러한 값
	};
	VOWBOUND_API bool IsPoseOwnedByMontage(const FMontagePoseFacts& Facts);

	/** 플레이어 주시 자세 억제(UVBPlayerAttentionComponent::IsPoseSuppressed). 소유자가 없을 때의 처리는 소비처가 한다. */
	struct FAttentionPoseFacts
	{
		bool bDead = false;                    // UVBAbilitySystemStatics::IsActorDead
		bool bTraversalAction = false;
		bool bTraversalFacing = false;
		bool bEnvironmentMotionFacing = false;
		bool bAttacking = false;               // State.Combat.Attacking - 아래 여섯은 ASC 가 없으면 false
		bool bDodgeCommit = false;             // State.Combat.DodgeCommit
		bool bHitReacting = false;             // State.Combat.HitReacting
		bool bStaggered = false;               // State.Combat.Staggered
		bool bDowned = false;                  // State.Combat.Downed
		bool bInteracting = false;             // State.Interacting
	};
	VOWBOUND_API bool IsAttentionPoseSuppressed(const FAttentionPoseFacts& Facts);

	/** 플레이어 시선 POI(UVBLookAtPOIComponent::ResolveTarget). 켜짐·캐릭터·메시 확인은 소비처가 먼저 한다. */
	struct FPlayerLookAtFacts
	{
		bool bMeshSimulatingPhysics = false;   // USkeletalMeshComponent::IsAnySimulatingPhysics
		bool bDead = false;                    // ASC 의 State.Dead 태그 - 주시 쪽과 출처가 다르다
		bool bFinisherPlaying = false;         // UVBFinisherComponent::IsPlaying - 컴포넌트가 없으면 false
		bool bPlayerRagdoll = false;
		bool bSmartObjectInteracting = false;  // UVBGASPSmartObjectComponent::IsInteracting - Facing 과 다르다
		bool bAttentionSuppressed = false;     // UVBPlayerAttentionComponent::IsPoseSuppressed 의 결과(소유자 없음 처리까지 거친 값)
	};
	VOWBOUND_API bool BlocksPlayerLookAt(const FPlayerLookAtFacts& Facts);

	/** 적 시선의 적 전용 차단. 대상 사망 검사와 부모 POI 의 차단은 소비처에서 그대로 이어진다. */
	struct FEnemyLookAtFacts
	{
		bool bDead = false;                    // AVBEnemyBase::IsDead
		bool bRecoverableRagdoll = false;      // AVBEnemyBase::IsRecoverableRagdollActive
		bool bFinisherScene = false;
		bool bStaggered = false;
		bool bDowned = false;
		bool bHitReacting = false;
		bool bAttacking = false;
	};
	VOWBOUND_API bool BlocksEnemyLookAt(const FEnemyLookAtFacts& Facts);

	/** 트래버설 시작(UVBTraversalComponent::TryTraversalAction). 이미 동작 중인지는 소비처가 본다. */
	struct FTraversalStartFacts
	{
		bool bSmartObjectInteracting = false;
		bool bPlayerRagdoll = false;
	};
	VOWBOUND_API bool BlocksTraversalStart(const FTraversalStartFacts& Facts);
}
