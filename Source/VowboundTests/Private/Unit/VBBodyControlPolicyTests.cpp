#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CharacterCommon/VBBodyControlPolicy.h"

// 몸 소유 판정을 정책으로 옮기기 전의 식과 정책의 결과를, 정책마다 입력 조합 전부에서 대조한다.
// 기준 함수는 각 소비처의 지금 식을 그대로 옮긴 독립 코드다. 정책·정책의 도우미를 부르지 않는다 -
//   기준과 정책이 같은 틀린 식을 나눠 쓰면 전수 대조도 통과하기 때문이다.
// 이 시험이 증명하는 것은 «같은 사실 -> 같은 결과» 뿐이다. 소비처가 원본을 맞는 칸에 담았는지(배선),
//   읽는 시점, 네트워크는 증명하지 않는다 - 설계 .sw/structure-refactor/phase2-1-body-control.md 6절.
// 기준을 옮긴 소스: 6d502b6 위 작업본(2026-10-03). VBCharacter.* 잠금이 풀린 revision 에서 식을 다시 대조하고
//   이 이름을 그 revision 으로 바꾼다.
namespace VBBodyControlPolicyTestBaseline
{
	static const TCHAR* Revision = TEXT("6d502b6+worktree-2026-10-03");

	static bool Bit(uint32 Mask, int32 Index) { return (Mask >> Index) & 1u; }

	// AVBCharacter::IsCapsuleRotationOwnedByFeature - 위에서부터 차례로 return true.
	static bool CapsuleRotation(uint32 M)
	{
		if (Bit(M, 0)) return true; // UVBFinisherComponent::IsActorInScene(this)
		if (Bit(M, 1)) return true; // PlayerRagdollComponent->IsRagdollActive()
		if (Bit(M, 2)) return true; // SmartObjectInteraction->OwnsCapsuleFacing()
		if (Bit(M, 3)) return true; // InteractorComponent->OwnsCapsuleFacing()
		if (Bit(M, 4)) return true; // IsWarpOwningRotation()
		if (Bit(M, 5)) return true; // TraversalComponent->IsDoingTraversal()
		return false;
	}

	// AVBCharacter::ApplyRotationMode 의 bFacingOwnedByFeature.
	static bool MovementFacing(uint32 M)
	{
		return Bit(M, 0)   // TraversalComponent->OwnsCapsuleFacing()
			|| Bit(M, 1)   // IsActorInScene(this)
			|| Bit(M, 2)   // IsRagdollActive()
			|| Bit(M, 3)   // SmartObjectInteraction->OwnsCapsuleFacing()
			|| Bit(M, 4);  // InteractorComponent->OwnsCapsuleFacing()
	}

	// UVBTargetLockComponent::CanUseCombatCamera 의 몸 조건(자격 조건은 빼고) - 하나라도 걸리면 false 를 돌려줬다.
	static bool CombatCamera(uint32 M)
	{
		if (Bit(M, 1)) return true; // IsActorInScene(OwnerCharacter) - 자격 조건 줄의 끝 항
		if (Bit(M, 0)) return true; // Traversal->OwnsCapsuleFacing()
		if (Bit(M, 2)) return true; // Ragdoll->IsRagdollActive()
		if (Bit(M, 3)) return true; // Smart->OwnsCapsuleFacing()
		if (Bit(M, 4)) return true; // Interactor->OwnsCapsuleFacing()
		return false;
	}

	// VBEnvironmentCameraSolver 의 bAllowComposition 에서 조준을 뺀 부정.
	static bool EnvironmentCamera(uint32 M)
	{
		const bool bRagdoll = Bit(M, 0);
		const bool bSpecialTraversal = Bit(M, 1);
		return !(!bRagdoll && !bSpecialTraversal);
	}

	// UVBAnimInstance 의 bPoseOwnedByMontage = bDeadNow || bInFinisherScene || IsTraversalOwningPose().
	static bool MontagePose(uint32 M)
	{
		return Bit(M, 0) || Bit(M, 1) || Bit(M, 2);
	}

	// UVBPlayerAttentionComponent::IsPoseSuppressed - 소유자 확인 뒤.
	static bool AttentionPose(uint32 M)
	{
		if (Bit(M, 0)) return true;              // IsActorDead(GetOwner())
		if (Bit(M, 1) || Bit(M, 2)) return true; // Traversal->IsDoingTraversal() || Traversal->OwnsCapsuleFacing()
		if (Bit(M, 3)) return true;              // Interactor->OwnsCapsuleFacing()
		return Bit(M, 4) || Bit(M, 5) || Bit(M, 6) || Bit(M, 7) || Bit(M, 8) || Bit(M, 9); // 태그 여섯
	}

	// UVBLookAtPOIComponent::ResolveTarget 의 차단(켜짐·캐릭터·메시 확인 뒤).
	static bool PlayerLookAt(uint32 M)
	{
		if (Bit(M, 0)) return true; // GetMesh()->IsAnySimulatingPhysics()
		if (Bit(M, 1)) return true; // ASC State_Dead
		if (Bit(M, 2)) return true; // Finisher->IsPlaying()
		if (Bit(M, 3)) return true; // Ragdoll->IsRagdollActive()
		if (Bit(M, 4)) return true; // SmartObject->IsInteracting()
		if (Bit(M, 5)) return true; // Attention->IsPoseSuppressed()
		return false;
	}

	// UVBEnemyLookAtComponent::ResolveTarget 의 적 전용 차단(대상 사망·Super 앞).
	static bool EnemyLookAt(uint32 M)
	{
		if (Bit(M, 0) || Bit(M, 1) || Bit(M, 2)) return true; // IsDead || IsRecoverableRagdollActive || IsActorInScene
		return Bit(M, 3) || Bit(M, 4) || Bit(M, 5) || Bit(M, 6); // Staggered·Downed·HitReacting·Attacking
	}

	// UVBTraversalComponent::TryTraversalAction 의 시작 차단.
	static bool TraversalStart(uint32 M)
	{
		return Bit(M, 0) || Bit(M, 1); // SmartObject->IsInteracting() || Ragdoll->IsRagdollActive()
	}
}

namespace
{
	using namespace VBBodyControlPolicy;
	using VBBodyControlPolicyTestBaseline::Bit;

	// 비트 번호는 위 기준 함수의 주석 순서와 같다.
	// 구조 분해로 채운다 - 사실 칸이 하나 늘면 묶는 이름 수가 안 맞아 여기서 컴파일이 깨진다. 칸을 더하고 정책에
	//   OR 로 넣어도 생성기가 그 칸을 안 채우면 전수 대조가 늘 false 인 칸으로 돌아 그대로 통과하기 때문이다.
	//   깨지면 생성기·기준 함수·비트 수를 같이 고친다. 이름·뜻이 바뀌거나 배선이 틀린 것은 이것이 못 잡는다.
	FCapsuleRotationFacts CapsuleFacts(uint32 M)
	{
		FCapsuleRotationFacts F;
		auto& [Finisher, Ragdoll, SmartFacing, EnvironmentFacing, Warp, TraversalAction] = F;
		Finisher = Bit(M, 0); Ragdoll = Bit(M, 1); SmartFacing = Bit(M, 2);
		EnvironmentFacing = Bit(M, 3); Warp = Bit(M, 4); TraversalAction = Bit(M, 5);
		return F;
	}
	FMovementFacingFacts FacingFacts(uint32 M)
	{
		FMovementFacingFacts F;
		auto& [TraversalFacing, Finisher, Ragdoll, SmartFacing, EnvironmentFacing] = F;
		TraversalFacing = Bit(M, 0); Finisher = Bit(M, 1); Ragdoll = Bit(M, 2);
		SmartFacing = Bit(M, 3); EnvironmentFacing = Bit(M, 4);
		return F;
	}
	FEnvironmentCameraFacts EnvironmentFacts(uint32 M)
	{
		FEnvironmentCameraFacts F;
		auto& [Ragdoll, TraversalFacing] = F;
		Ragdoll = Bit(M, 0); TraversalFacing = Bit(M, 1);
		return F;
	}
	FMontagePoseFacts PoseFacts(uint32 M)
	{
		FMontagePoseFacts F;
		auto& [Dead, Finisher, TraversalPose] = F;
		Dead = Bit(M, 0); Finisher = Bit(M, 1); TraversalPose = Bit(M, 2);
		return F;
	}
	FAttentionPoseFacts AttentionFacts(uint32 M)
	{
		FAttentionPoseFacts F;
		auto& [Dead, TraversalAction, TraversalFacing, EnvironmentFacing, Attacking, DodgeCommit, HitReacting, Staggered, Downed, Interacting] = F;
		Dead = Bit(M, 0); TraversalAction = Bit(M, 1); TraversalFacing = Bit(M, 2);
		EnvironmentFacing = Bit(M, 3); Attacking = Bit(M, 4); DodgeCommit = Bit(M, 5);
		HitReacting = Bit(M, 6); Staggered = Bit(M, 7); Downed = Bit(M, 8); Interacting = Bit(M, 9);
		return F;
	}
	FPlayerLookAtFacts PlayerLookFacts(uint32 M)
	{
		FPlayerLookAtFacts F;
		auto& [MeshPhysics, Dead, FinisherPlaying, Ragdoll, SmartInteracting, AttentionSuppressed] = F;
		MeshPhysics = Bit(M, 0); Dead = Bit(M, 1); FinisherPlaying = Bit(M, 2);
		Ragdoll = Bit(M, 3); SmartInteracting = Bit(M, 4); AttentionSuppressed = Bit(M, 5);
		return F;
	}
	FEnemyLookAtFacts EnemyLookFacts(uint32 M)
	{
		FEnemyLookAtFacts F;
		auto& [Dead, RecoverableRagdoll, Finisher, Staggered, Downed, HitReacting, Attacking] = F;
		Dead = Bit(M, 0); RecoverableRagdoll = Bit(M, 1); Finisher = Bit(M, 2);
		Staggered = Bit(M, 3); Downed = Bit(M, 4); HitReacting = Bit(M, 5); Attacking = Bit(M, 6);
		return F;
	}
	FTraversalStartFacts TraversalFacts(uint32 M)
	{
		FTraversalStartFacts F;
		auto& [SmartInteracting, Ragdoll] = F;
		SmartInteracting = Bit(M, 0); Ragdoll = Bit(M, 1);
		return F;
	}

	// 어긋난 첫 조합을 정책 이름·비트마스크·기준 값·새 값·기준 revision 으로 낸다. 어긋난 조합 수도 낸다.
	template <typename BaselineFn, typename PolicyFn>
	void CompareBodyControlPolicy(FAutomationTestBase& Test, const TCHAR* Policy, int32 Bits, BaselineFn Baseline, PolicyFn NewPolicy)
	{
		int32 Mismatches = 0;
		for (uint32 Mask = 0; Mask < (1u << Bits); ++Mask)
		{
			const bool bExpected = Baseline(Mask);
			const bool bActual = NewPolicy(Mask);
			if (bExpected != bActual && Mismatches++ == 0)
			{
				Test.AddError(FString::Printf(TEXT("%s mask=0x%X baseline=%d policy=%d baseline-revision=%s"),
					Policy, Mask, bExpected, bActual, VBBodyControlPolicyTestBaseline::Revision));
			}
		}
		if (Mismatches > 1)
		{
			Test.AddError(FString::Printf(TEXT("%s: %d of %d combinations differ"), Policy, Mismatches, 1 << Bits));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBodyControlPolicyMatchesBaselineTest, "Vowbound.Unit.BodyControl.MatchesBaseline",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBBodyControlPolicyMatchesBaselineTest::RunTest(const FString& Parameters)
{
	namespace B = VBBodyControlPolicyTestBaseline;
	CompareBodyControlPolicy(*this, TEXT("CapsuleRotation"), 6, B::CapsuleRotation, [](uint32 M) { return IsCapsuleRotationOwned(CapsuleFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("MovementFacing"), 5, B::MovementFacing, [](uint32 M) { return IsMovementFacingOwned(FacingFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("CombatCamera"), 5, B::CombatCamera, [](uint32 M) { return BlocksCombatCamera(FacingFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("EnvironmentCamera"), 2, B::EnvironmentCamera, [](uint32 M) { return BlocksEnvironmentCameraComposition(EnvironmentFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("MontagePose"), 3, B::MontagePose, [](uint32 M) { return IsPoseOwnedByMontage(PoseFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("AttentionPose"), 10, B::AttentionPose, [](uint32 M) { return IsAttentionPoseSuppressed(AttentionFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("PlayerLookAt"), 6, B::PlayerLookAt, [](uint32 M) { return BlocksPlayerLookAt(PlayerLookFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("EnemyLookAt"), 7, B::EnemyLookAt, [](uint32 M) { return BlocksEnemyLookAt(EnemyLookFacts(M)); });
	CompareBodyControlPolicy(*this, TEXT("TraversalStart"), 2, B::TraversalStart, [](uint32 M) { return BlocksTraversalStart(TraversalFacts(M)); });

	// 기본값 경로. 위 생성기는 모든 칸을 덮어써서 기본값을 안 지난다 - 누가 칸의 기본값을 true 로 바꾸면 여기서 잡힌다.
	TestFalse(TEXT("default capsule facts block nothing"), IsCapsuleRotationOwned({}));
	TestFalse(TEXT("default facing facts block nothing"), IsMovementFacingOwned({}));
	TestFalse(TEXT("default facing facts do not block the combat camera"), BlocksCombatCamera({}));
	TestFalse(TEXT("default environment camera facts block nothing"), BlocksEnvironmentCameraComposition({}));
	TestFalse(TEXT("default pose facts block nothing"), IsPoseOwnedByMontage({}));
	TestFalse(TEXT("default attention facts block nothing"), IsAttentionPoseSuppressed({}));
	TestFalse(TEXT("default player look-at facts block nothing"), BlocksPlayerLookAt({}));
	TestFalse(TEXT("default enemy look-at facts block nothing"), BlocksEnemyLookAt({}));
	TestFalse(TEXT("default traversal start facts block nothing"), BlocksTraversalStart({}));
	return true;
}

#endif
