// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AI/VBCombatCoordinatorSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Data/VBCombatSettings.h"
#include "Data/VBEnemyConfig.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 전쌍 정렬 그리디 배정의 후보 한 쌍. 정렬 키가 셋인 이유는 결정론 때문이다 -
	// 거리 동률에서 순서가 흔들리면 같은 상황에서 매번 다른 배정이 나와 적들이 눈에 띄게 떤다.
	struct FVBSlotPairing
	{
		double Distance    = 0.0;
		int32  MemberIndex = INDEX_NONE;
		int32  SlotIndex   = INDEX_NONE;
	};
}

UVBCombatCoordinatorSubsystem* UVBCombatCoordinatorSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}
	// 조율은 per-world → world 서브시스템을 그 world 에서 직접 얻는다(GI 아님).
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
	}
	return nullptr;
}

void UVBCombatCoordinatorSubsystem::Deinitialize()
{
	// 월드 소멸 시 남은 타이머를 전부 해제한다. 타이머 매니저가 월드와 함께 사라지긴 하지만,
	// 해제를 명시해 두면 "그룹이 사라지면 타이머도 사라진다"는 계약이 코드에 남는다.
	if (UWorld* World = GetWorld())
	{
		for (TPair<TWeakObjectPtr<AActor>, FVBCombatGroup>& Pair : Groups)
		{
			World->GetTimerManager().ClearTimer(Pair.Value.RecomputeTimer);
		}
	}
	Groups.Empty();
	Super::Deinitialize();
}

bool UVBCombatCoordinatorSubsystem::IsServer() const
{
	const UWorld* World = GetWorld();
	// 월드가 없으면 결정을 내릴 수 없으므로 권위가 아닌 것으로 본다(변이 API 전체가 조기 반환).
	return World && World->GetNetMode() != NM_Client;
}

void UVBCombatCoordinatorSubsystem::RegisterCombatant(UVBEnemyCombatComponent* Member, AActor* Target)
{
	if (!IsServer() || !Member || !Target)
	{
		return;
	}

	FVBCombatGroup& Group = Groups.FindOrAdd(Target);
	Group.Target = Target;
	// 이미 있으면 기존 결정을 그대로 둔다 - 멱등성이 이 함수의 계약이다.
	Group.Members.FindOrAdd(Member);
	EnsureRecomputeTimer(Group, Target);
}

void UVBCombatCoordinatorSubsystem::UnregisterCombatant(UVBEnemyCombatComponent* Member)
{
	if (!IsServer() || !Member)
	{
		return;
	}

	// 멤버 기록이 통째로 사라진다. 그 멤버의 HeldTokenCost 항이 없어지므로 가용량이 자동 회복된다 -
	// FR8(사망 시 토큰 회수)이 반납 코드 없이 성립하는 지점이다.
	const TWeakObjectPtr<AActor> TargetKey = Member->CombatTarget;
	FVBCombatGroup* Group = Groups.Find(TargetKey);
	if (!Group)
	{
		return;
	}

	Group->Members.Remove(Member);
	if (Group->Members.Num() == 0)
	{
		DisbandGroup(TargetKey);
	}
}

bool UVBCombatCoordinatorSubsystem::TryAcquireAttackToken(UVBEnemyCombatComponent* Member, int32 Cost)
{
	if (!IsServer() || !Member)
	{
		return false;
	}

	FVBCombatGroup* Group = FindGroupForMemberMutable(Member);
	if (!Group)
	{
		return false;
	}

	FVBCombatMemberState* State = Group->Members.Find(Member);
	if (!State || State->HeldTokenCost > 0)
	{
		// 이미 쥐고 있으면 중복 취득하지 않는다 - 중복이 허용되면 합이 실제 점유를 넘어선다.
		return false;
	}

	// 0 이하 코스트는 토큰 시스템을 무의미하게 만든다(가용량 검사가 항상 통과해 전원이 공격자가 된다).
	// 자산의 ClampMin 은 에디터 입력만 막으므로 런타임 바닥을 여기서 잡는다. 튜닝값이 아니라 구조 불변식이다.
	const int32 EffectiveCost = FMath::Max(1, Cost);
	if (ComputeAvailableTokens(*Group) < EffectiveCost)
	{
		return false;
	}

	// 새 임대의 시작이므로 지난 임대의 흔적을 먼저 지운다. 빠뜨리면 직전 공격의 시작 시각이 남아
	// 이번 임대가 '이미 공격 중'으로 오판되고 접근 구간 상한이 통째로 안 돈다.
	// 지울 목록을 여기 적지 않는 이유: 그 목록의 홈은 FVBCombatMemberState::ClearLease 하나다.
	State->ClearLease();

	State->HeldTokenCost    = EffectiveCost;
	State->TokenGrantedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// 회수 규칙(2.6)이 쓸 기준점. 부여 시점보다 멀어졌을 때만 뺏게 하기 위한 것이다.
	// 타겟이나 소유자가 없으면 기준을 세울 수 없으므로 -1 로 두고, 그때는 회수가 위치로 판단하지 않는다.
	const AActor* GrantTarget = Group->Target.Get();
	const AActor* GrantOwner  = Member->GetOwner();
	State->DistanceAtGrant = (GrantTarget && GrantOwner)
		? FVector::Dist2D(GrantOwner->GetActorLocation(), GrantTarget->GetActorLocation())
		: -1.0;

	VB_LOG(Log, "토큰 부여: %s (코스트 %d, 잔여 %d)",
	       *GetNameSafe(Member->GetOwner()), EffectiveCost, ComputeAvailableTokens(*Group));
	return true;
}

void UVBCombatCoordinatorSubsystem::NotifyAttackStarted(UVBEnemyCombatComponent* Member)
{
	if (!IsServer() || !Member)
	{
		return;
	}

	FVBCombatGroup*       Group = FindGroupForMemberMutable(Member);
	FVBCombatMemberState* State = Group ? Group->Members.Find(Member) : nullptr;
	if (!State)
	{
		return;
	}

	// 토큰 없이 공격이 시작되는 경로는 결함이다. BT 는 CombatRole==Attacker 분기에서만 공격 태스크에
	// 들어가고 그 역할은 토큰 보유가 전제다. 조용히 넘기면 동시 공격 상한 붕괴가 화면에서만 관측된다 -
	// 2026-08-20 실측이 정확히 그 상태였고 로그는 한 줄도 없었다. 이 한 줄이 그 재발 탐지기다.
	VB_CLOG(State->HeldTokenCost <= 0, Warning,
	        "토큰 없이 공격이 시작됐다: %s - 상한 계산이 이미 어긋나 있다",
	        *GetNameSafe(Member->GetOwner()));

	State->AttackStartedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UVBCombatCoordinatorSubsystem::ReleaseAttackToken(UVBEnemyCombatComponent* Member)
{
	if (!IsServer() || !Member)
	{
		return;
	}

	FVBCombatGroup*       Group = FindGroupForMemberMutable(Member);
	FVBCombatMemberState* State = Group ? Group->Members.Find(Member) : nullptr;
	if (!State)
	{
		return;
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	VB_LOG(Log, "토큰 반납: %s (공격 %.2fs)", *GetNameSafe(Member->GetOwner()),
	       State->AttackStartedTime >= 0.0 ? Now - State->AttackStartedTime : 0.0);

	State->ClearLease();
	// 방금 때린 멤버를 뒤로 보내는 일은 순번 규칙이 위치로 한다(3단계) - 아직 사거리 안이라
	// 자동으로 후순위가 된다. 시간 필드를 따로 들지 않는 이유다.

	// 그 자리에서 재배분한다. 타이머를 기다리면 다음 적이 붙기까지 최대 한 주기가 통째로 빈다
	//   (2026-08-21 실측 0.14~0.65초 - 한 사이클 공백의 절반이 이것이었다).
	// 재귀가 없는 이유: RecomputeGroup 은 토큰을 회수할 때 이 함수를 부르지 않고 State 를 직접 만진다.
	//   그 사실이 깨지면 여기가 무한 재귀가 되므로, 회수 경로를 바꿀 때는 이 주석을 먼저 볼 것.
	RecomputeGroup(*Group);
}

FVector UVBCombatCoordinatorSubsystem::GetAssignedSlotLocation(const UVBEnemyCombatComponent* Member) const
{
	if (const FVBCombatMemberState* State = FindMemberState(Member))
	{
		return State->SlotLocation;
	}
	// 미등록/미배정이면 자기 위치를 돌려준다 - 가짜 슬롯을 지어내지 않는다(부재는 값이 아니다).
	// 호출부(BT MoveTo)는 이 값으로 제자리 대기가 된다.
	const AActor* Owner = Member ? Member->GetOwner() : nullptr;
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

EVBCombatRole UVBCombatCoordinatorSubsystem::GetAssignedRole(const UVBEnemyCombatComponent* Member) const
{
	if (const FVBCombatMemberState* State = FindMemberState(Member))
	{
		return State->Role;
	}
	return EVBCombatRole::Idle;
}

int32 UVBCombatCoordinatorSubsystem::GetHeldTokenCost(const UVBEnemyCombatComponent* Member) const
{
	const FVBCombatMemberState* State = FindMemberState(Member);
	return State ? State->HeldTokenCost : 0;
}

int32 UVBCombatCoordinatorSubsystem::GetAvailableTokens(const AActor* Target) const
{
	// 맵 키 타입이 TWeakObjectPtr<AActor> 라 const 를 벗겨야 한다. 읽기 전용 조회이므로 안전하다.
	const TWeakObjectPtr<AActor> TargetKey(const_cast<AActor*>(Target));
	const FVBCombatGroup* Group = Groups.Find(TargetKey);
	if (!Group)
	{
		// 그룹이 없으면 아무도 안 쥐고 있다 = 총량 전부가 가용이다.
		return GetDefault<UVBCombatSettings>()->MaxAttackTokens;
	}
	return ComputeAvailableTokens(*Group);
}

int32 UVBCombatCoordinatorSubsystem::ComputeAvailableTokens(const FVBCombatGroup& Group) const
{
	int32 Available = GetDefault<UVBCombatSettings>()->MaxAttackTokens;
	for (const TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		Available -= Pair.Value.HeldTokenCost;
	}
	return Available;
}

const FVBCombatGroup* UVBCombatCoordinatorSubsystem::FindGroupForMember(const UVBEnemyCombatComponent* Member) const
{
	if (!Member)
	{
		return nullptr;
	}
	// 멤버의 약참조 원본을 키로 쓴다. getter(CombatTarget.Get())를 쓰면 타겟이 파괴된 순간 null 이 되어
	// 자기 그룹을 못 찾고, 등록 해제가 조용히 실패해 죽은 그룹에 멤버가 남는다.
	return Groups.Find(Member->CombatTarget);
}

FVBCombatGroup* UVBCombatCoordinatorSubsystem::FindGroupForMemberMutable(const UVBEnemyCombatComponent* Member)
{
	return const_cast<FVBCombatGroup*>(FindGroupForMember(Member));
}

const FVBCombatMemberState* UVBCombatCoordinatorSubsystem::FindMemberState(const UVBEnemyCombatComponent* Member) const
{
	const FVBCombatGroup* Group = FindGroupForMember(Member);
	if (!Group)
	{
		return nullptr;
	}
	// 맵 키 타입이 TWeakObjectPtr<UVBEnemyCombatComponent> 라 const 를 벗겨야 한다. 읽기 전용 조회다.
	const TWeakObjectPtr<UVBEnemyCombatComponent> MemberKey(const_cast<UVBEnemyCombatComponent*>(Member));
	return Group->Members.Find(MemberKey);
}

void UVBCombatCoordinatorSubsystem::EnsureRecomputeTimer(FVBCombatGroup& Group, AActor* Target)
{
	if (Group.RecomputeTimer.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return;
	}

	// 0 이하 주기는 SetTimer 가 타이머를 지우는 것으로 해석해 조율이 영영 안 돈다.
	// 자산 ClampMin(0.05)이 에디터 입력을 막지만 ini 수기 편집은 막지 못하므로 여기서 바닥을 잡는다.
	const float Interval = FMath::Max(GetDefault<UVBCombatSettings>()->SlotRecomputeInterval, KINDA_SMALL_NUMBER);

	const TWeakObjectPtr<AActor> TargetKey(Target);
	World->GetTimerManager().SetTimer(
		Group.RecomputeTimer,
		FTimerDelegate::CreateUObject(this, &UVBCombatCoordinatorSubsystem::OnRecomputeTimer, TargetKey),
		Interval,
		/*bLoop=*/true);
}

void UVBCombatCoordinatorSubsystem::DisbandGroup(const TWeakObjectPtr<AActor>& TargetKey)
{
	if (FVBCombatGroup* Group = Groups.Find(TargetKey))
	{
		// 반드시 제거보다 먼저. Remove 가 구조체를 파괴하면 핸들이 사라져 타이머가 고아가 된다.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(Group->RecomputeTimer);
		}
		Groups.Remove(TargetKey);
	}
}

void UVBCombatCoordinatorSubsystem::OnRecomputeTimer(TWeakObjectPtr<AActor> TargetKey)
{
	if (!IsServer())
	{
		return;
	}

	FVBCombatGroup* Group = Groups.Find(TargetKey);
	if (!Group)
	{
		return;
	}

	// 타겟이 사라졌거나 죽었으면 그룹 자체가 의미를 잃는다. 멤버들은 다음 BTService 주기에
	// 새 타겟(또는 null)을 넘겨 스스로 재등록한다 - 등록이 멱등이라 복구 경로가 따로 필요 없다.
	AActor* Target = Group->Target.Get();
	if (!Target || UVBAbilitySystemStatics::IsActorDead(Target))
	{
		DisbandGroup(TargetKey);
		return;
	}

	RecomputeGroup(*Group);

	// 정리 단계에서 멤버가 전부 빠졌으면 빈 그룹의 타이머가 영원히 도는 것을 막는다.
	// RecomputeGroup 은 Groups 를 건드리지 않으므로 위 포인터는 여기서도 유효하다.
	if (Group->Members.Num() == 0)
	{
		DisbandGroup(TargetKey);
	}
}

void UVBCombatCoordinatorSubsystem::RecomputeGroup(FVBCombatGroup& Group)
{
	const UVBCombatSettings* Settings = GetDefault<UVBCombatSettings>();
	AActor* Target = Group.Target.Get();
	if (!Target)
	{
		return;
	}

	const UWorld* World = GetWorld();
	const double  Now   = World ? World->GetTimeSeconds() : 0.0;

	// 1) 정리 - 무효/사망 멤버 제거 + 토큰 임대 만료 회수
	for (auto It = Group.Members.CreateIterator(); It; ++It)
	{
		const UVBEnemyCombatComponent* Member = It.Key().Get();
		AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner || UVBAbilitySystemStatics::IsActorDead(MemberOwner))
		{
			It.RemoveCurrent();
			continue;
		}

		FVBCombatMemberState& State = It.Value();

		// 여기서는 공격이 시작된 임대만 본다. 접근 구간 초과는 넘겨받을 후보가 있을 때만 의미가 있어
		// 후보 수집 뒤(2.5 단계)로 미룬다 - 그러지 않으면 회수한 토큰을 같은 멤버가 도로 집는 헛돌기가 된다.
		if (State.HeldTokenCost <= 0 || State.AttackStartedTime < 0.0)
		{
			continue;
		}
		if ((Now - State.AttackStartedTime) <= Settings->MaxTokenHoldSeconds)
		{
			continue;
		}

		// 갈림길이 여기다. '아직 공격 중인가'를 우리 기록이 아니라 ASC 태그에 직접 묻는다.
		// 이 안전망이 잡으려는 결함이 바로 그 기록을 갱신하는 구독의 실패이므로,
		// 자기 기록으로 자기 오류를 검사하면 두 경우가 구분되지 않는다.
		if (Member->IsAttacking())
		{
			// 공격 GA 가 안 끝났다. 임대를 회수하지 '않는' 것이 이 분기의 요점이다.
			// 회수하면 ComputeAvailableTokens 가 스윙 중인 멤버의 슬롯을 빈 것으로 계산하고
			// 대기 중인 적이 그것을 집는다 - 2026-08-20 실측에서 동시 공격 3기가 나온 기전 그대로다.
			VB_CLOG(!State.bReportedHoldOverrun, Warning,
			        "공격이 %.1fs 째 끝나지 않는다: %s - 임대를 유지해 동시 공격 상한을 지킨다",
			        Now - State.AttackStartedTime, *MemberOwner->GetName());
			State.bReportedHoldOverrun = true;
			continue;
		}

		// 태그는 내려갔는데 반납이 안 됐다 = 원래 이 안전망이 잡으려던 그 결함이다.
		// 이제야 이 경고문이 참이 된다.
		VB_LOG(Warning, "토큰 임대 만료: %s (공격 %.1fs) - 정상 반납 경로가 실패했다",
		       *MemberOwner->GetName(), Now - State.AttackStartedTime);
		State.ClearLease();
	}

	if (Group.Members.Num() == 0)
	{
		return;
	}

	const FVector TargetLocation = Target->GetActorLocation();

	// 2) 후보 수집 - 토큰 미보유 && 지금 실제로 공격을 켤 수 있는 멤버만.
	//    IsReadyToAttack 이 여기 있는 것이 요점이다: 쿨다운/코스트/차단태그를 엔진 정본
	//    UGameplayAbility::CanActivateAbility 하나로 물으므로, "때릴 수 없는 적에게 토큰을 줘
	//    교착시키는" 결함이 구조적으로 없다.
	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>> Candidates;
	Candidates.Reserve(Group.Members.Num());
	for (const TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		const UVBEnemyCombatComponent* Member = Pair.Key.Get();
		if (Member && Pair.Value.HeldTokenCost <= 0 && Member->IsReadyToAttack())
		{
			Candidates.Add(Pair.Key);
		}
	}

	// 이번 주기에 토큰을 회수당한 멤버들. 정렬 뒤에 붙여 "다른 후보가 다 받고도 남으면 준다"를
	// 코드 모양으로 표현한다. 앞에 섞으면 위치 규칙이 그들을 다시 1순위로 올려 회수가 무효가 된다.
	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>> Deprioritized;

	// 2.5) 접근 구간 초과 회수. 결함이 아니라 정상 재배분이라 Warning 이 아니라 Log 다.
	//      후보가 없으면 회수해도 넘겨줄 상대가 없어 같은 멤버가 그대로 다시 받는다 - 그 헛돌기와
	//      그때 쌓이는 로그 노이즈를 막으려고 후보 수집 뒤에 둔다.
	if (Candidates.Num() > 0)
	{
		for (TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
		{
			UVBEnemyCombatComponent* Member = Pair.Key.Get();
			FVBCombatMemberState&    State  = Pair.Value;
			if (!Member || State.HeldTokenCost <= 0 || State.AttackStartedTime >= 0.0)
			{
				// 미보유이거나 이미 공격에 들어간 임대는 이 규칙의 대상이 아니다.
				continue;
			}
			if ((Now - State.TokenGrantedTime) <= Member->GetCombatConfig()->MaxApproachHoldSeconds)
			{
				continue;
			}

			VB_LOG(Log, "토큰 재배분: %s - %.1fs 동안 사거리에 못 붙어 대기열 뒤로 보낸다",
			       *GetNameSafe(Member->GetOwner()), Now - State.TokenGrantedTime);

			State.ClearLease();

			// 회수당한 멤버도 남는 토큰이 있으면 도로 받는 것이 옳다. 다만 이번 배분에서는
			// 다른 후보들 뒤에 세운다 - 앞에 두면 정렬이 그를 다시 1순위로 올려 회수가 무효가 된다.
			// 정렬 뒤에 붙이므로 "남으면 준다"가 코드 모양에 그대로 드러난다.
			if (Member->IsReadyToAttack())
			{
				Deprioritized.Add(Pair.Key);
			}
		}
	}

	// 2.6) 위치 기준 회수. 아직 공격을 시작하지 않은 보유자가 멀리 있는데 이미 사거리 안에 들어온
	//      후보가 있으면 토큰을 옮긴다.
	//
	// 참고작이 "권한 탈취"라 부르는 것이 이것이다(Game AI Pro 28장):
	//   "If the player suddenly moves towards the second soldier who is currently without permission
	//    to attack, it would be best if the stage manager could move permission from the first
	//    soldier to the second to take advantage of the second soldier's new position."
	// 우리에겐 슬롯 재배정(AssignRingSlots 의 매 주기 전쌍 그리디)은 이미 있었지만
	// 토큰 배분에는 위치 항이 어디에도 없었고, 그 공백이 이 규칙이다.
	//
	// 새 값 0: 경계는 이미 있는 히스테리시스 쌍을 그대로 쓴다.
	//   회수는 AttackReleaseDistance 밖에서만 / 부여는 AttackCommitDistance 안 우선
	//   -> 두 값의 간격(200-180)이 그대로 완충이 되어 경계에서 발진하지 않는다.
	//
	// AttackStartedTime >= 0 가드가 이 규칙의 안전선이다. 스윙 중인 적의 토큰은 뺏지 않는다.
	//   뺏으면 가용량이 스윙 중인 멤버의 슬롯을 빈 것으로 계산해 동시 공격 상한이 붕괴한다
	//   (2026-08-20 실증된 그 결함). 기존 2.5단계가 같은 가드를 갖고 있어 형태를 그대로 복제했다.
	if (Candidates.Num() > 0)
	{
		// 사거리 안에 이미 들어와 있는 후보가 있을 때만 의미가 있다. 없으면 뺏어 봐야 놀리는 것이다.
		bool bAnyCandidateInRange = false;
		for (const TWeakObjectPtr<UVBEnemyCombatComponent>& Candidate : Candidates)
		{
			const UVBEnemyCombatComponent* Member = Candidate.Get();
			const AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
			if (MemberOwner &&
			    FVector::Dist2D(MemberOwner->GetActorLocation(), TargetLocation) <= Member->GetCombatConfig()->AttackCommitDistance)
			{
				bAnyCandidateInRange = true;
				break;
			}
		}

		if (bAnyCandidateInRange)
		{
			for (TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
			{
				UVBEnemyCombatComponent* Member      = Pair.Key.Get();
				FVBCombatMemberState&    State       = Pair.Value;
				const AActor*            MemberOwner = Member ? Member->GetOwner() : nullptr;
				if (!MemberOwner || State.HeldTokenCost <= 0 || State.AttackStartedTime >= 0.0)
				{
					continue;
				}
				const double Distance = FVector::Dist2D(MemberOwner->GetActorLocation(), TargetLocation);
				if (Distance <= Member->GetCombatConfig()->AttackReleaseDistance)
				{
					continue;
				}

				// 부여 시점보다 가까워졌으면 뺏지 않는다 - 걸어오는 중이다.
				//
				// 이 조건이 없으면 규칙 3과 정면으로 싸운다. 3은 가장 멀리 물러난 자를 먼저 뽑는데,
				//   그렇게 뽑힌 자는 정의상 사거리 밖이라 다음 주기에 여기서 곧바로 회수당한다
				//   (2026-08-21 실측: 부여 6.08s -> 회수 6.58s, 걸어보지도 못했다).
				// 참고작의 문장은 '멀다'가 아니라 '플레이어가 그쪽에서 멀어졌다'이다 -
				//   "If the player suddenly moves towards the second soldier". 부여 시점을 기준으로
				//   삼아야 그 문장이 그대로 코드가 된다. 절대 거리만 보면 표현할 수 없는 판단이다.
				if (State.DistanceAtGrant >= 0.0 && Distance <= State.DistanceAtGrant)
				{
					continue;
				}

				VB_LOG(Log, "토큰 이동: %s 가 %.0f 로 멀어졌다(부여 시점 %.0f) - 사거리 안 후보에게 넘긴다",
				       *MemberOwner->GetName(), Distance, State.DistanceAtGrant);

				State.ClearLease();

				// 뺏긴 멤버도 남으면 도로 받는다. 다른 후보 뒤에 세우는 이유는 2.5 와 같다 -
				// 앞에 두면 그가 사거리 밖이라 정렬 1순위가 되어 곧바로 되찾는다.
				if (Member->IsReadyToAttack())
				{
					Deprioritized.Add(Pair.Key);
				}
			}
		}
	}

	// 3) 순번 규칙 - "먼저 물러난 자에게 우선권".
	//
	// 종전 규칙은 "가장 오래 기다린 멤버 우선"이었다. 공평하지만 완전한 FIFO 라 같은 적이 두 번
	// 연속 때리는 일이 구조적으로 불가능했고, 적 셋이 기계적으로 돌아가며 때렸다
	// (2026-08-20 실측: 521 -> 519 -> 520 -> 521 -> 519). 반납 시각이 실수라 동률이 안 나서
	// 뒤에 있던 거리 비교가 한 번도 실행되지 않았다 - 그 시각 필드는 이제 없앴다.
	//
	// 참고작의 규칙은 순번제가 아니다(Game AI Pro 28장, Kingdoms of Amalur: Reckoning):
	//   "ensured no single creature could monopolize attack opportunities"
	//   "any creature that gave up its position after making an attack could immediately be
	//    assigned a slot again" (적이 적을 때)
	// 즉 독점만 막고 나머지는 위치가 정한다.
	//
	// 2026-08-20 재수정. 초판(가장 최근에 놓은 자만 배제 + 거리 우선)이 새 결함을 만들었다.
	//   실측: 링(250)까지 제대로 물러난 적이 슬롯 배정 25회 동안 한 번도 못 때렸고,
	//   덜 물러난 적 둘이 3타씩 가져갔다. 토큰이 비는 순간 '가장 가까운' 적은 언제나
	//   '아직 안 물러난' 적이라, 규칙이 물러나는 행동을 벌주고 있었다.
	//   참고작의 "best position to attack" 을 '가장 가까운'으로 읽은 것이 오독이었다 -
	//   그쪽은 대기자가 전부 approach circle 에 있어 거리가 동률인 전제다.
	//
	// 그래서 판정을 거리 하나가 아니라 두 단계로 둔다:
	//   1차: 사거리 밖으로 물러났는가 (물러난 쪽이 앞)
	//   2차: 물러난 자들끼리는 타겟에 가까운 쪽이 앞
	//
	// 이것이 참고작의 문장을 그대로 옮긴 것이다 -
	//   "any creature that was within the distance defined by the outer circle but without
	//    permission to attack had to leave the circle as quickly as possible"
	//   물러나는 것이 다음 차례의 조건이지 손해가 아니어야 한다.
	//
	// 이 규칙이 종전의 '직전에 놓은 자 배제'를 흡수한다: 방금 때린 적은 아직 사거리 안이라
	//   자동으로 뒤로 간다. 시간이 아니라 위치로 같은 일을 하므로 규칙이 하나로 준다.
	//
	// 연속 공격은 그대로 가능하다. 후보가 혼자면 뒤에 서 있어도 결국 그가 받는다.
	//   적이 여럿일 때만 "먼저 물러난 자에게 양보"가 발화한다.
	//
	// 새 값 0: 경계는 AttackReleaseDistance(아키타입 자산)를 그대로 쓴다. 역할 판정의
	//   히스테리시스가 쓰는 그 값이라, "물러났다"의 기준이 두 곳에서 갈리지 않는다.
	Candidates.Sort([&Group, &TargetLocation](const TWeakObjectPtr<UVBEnemyCombatComponent>& A,
	                                          const TWeakObjectPtr<UVBEnemyCombatComponent>& B)
	{
		const UVBEnemyCombatComponent* MemberA = A.Get();
		const UVBEnemyCombatComponent* MemberB = B.Get();
		const AActor* OwnerA = MemberA ? MemberA->GetOwner() : nullptr;
		const AActor* OwnerB = MemberB ? MemberB->GetOwner() : nullptr;
		if (!OwnerA || !OwnerB)
		{
			// 순서를 뒤집지 않고 '같음'으로 답해 약한 순서 공리를 지킨다.
			return false;
		}

		const double DistA = FVector::Dist2D(OwnerA->GetActorLocation(), TargetLocation);
		const double DistB = FVector::Dist2D(OwnerB->GetActorLocation(), TargetLocation);

		const bool bARetreated = DistA > MemberA->GetCombatConfig()->AttackReleaseDistance;
		const bool bBRetreated = DistB > MemberB->GetCombatConfig()->AttackReleaseDistance;
		if (bARetreated != bBRetreated)
		{
			return bARetreated;
		}
		return DistA < DistB;
	});

	Candidates.Append(Deprioritized);

	// 3.5) 예고 게이트. 그룹 안에 예고 중인 멤버가 있으면 이번 주기에는 토큰을 주지 않는다.
	//
	// 왜 필요한가: 플레이어가 힘든 것은 맞는 횟수가 아니라 동시에 읽어야 하는 개수다. 예고 둘이
	//   겹치면 어느 쪽을 피할지 못 정한다. 스윙이 겹치는 것은 괜찮다 - 판단이 이미 끝난 뒤다.
	// 왜 시간이 아니라 단계로 잠그는가: 간격의 길이가 아키타입의 TelegraphDuration 에서 자동으로
	//   나온다. 전역 초 단위 노브를 두면 "공격이 얼마나 벌어지나"의 홈이 둘이 되고, 대검 적(예고 1.5초)
	//   에서는 무의미하고 잔몹(0.6초)에서는 억지로 기다리게 된다.
	// 한계는 정직하게 둔다: TelegraphDuration 이 0 인 아키타입은 태그가 안 붙어 이 게이트가 안 걸린다.
	//   그것은 결함이 아니라 "예고 없음을 저작했으면 간격도 없다"는 일관된 귀결이다.
	//
	// 잠긴 동안 아무것도 기억하지 않는다: 예고가 끝나면 다음 주기에 같은 후보들이 그대로 다시 모이고,
	//   순서는 3단계의 위치 규칙이 매번 다시 정한다.
	bool bAnyTelegraphing = false;
	for (const TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		const UVBEnemyCombatComponent* Member = Pair.Key.Get();
		if (Member && Member->IsTelegraphing())
		{
			bAnyTelegraphing = true;
			break;
		}
	}

	// 4) 배분(push). 부여의 구현은 TryAcquireAttackToken 하나뿐이라 "토큰을 준다"의 홈이 하나다.
	//    예고 게이트가 걸려 있으면 이번 주기는 통째로 건너뛴다.
	for (const TWeakObjectPtr<UVBEnemyCombatComponent>& Candidate : bAnyTelegraphing
		     ? TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>()
		     : Candidates)
	{
		UVBEnemyCombatComponent* Member = Candidate.Get();
		if (!Member)
		{
			continue;
		}
		TryAcquireAttackToken(Member, Member->GetCombatConfig()->AttackTokenCost);
	}

	// 5) 역할 배정 (히스테리시스). 진입(Commit)과 유지(Release) 거리를 다르게 둬,
	//    플레이어가 경계선에서 스트레이프할 때 역할이 매 주기 뒤집히는 것을 막는다.
	for (TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		const UVBEnemyCombatComponent* Member = Pair.Key.Get();
		const AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner)
		{
			continue;
		}

		FVBCombatMemberState& State        = Pair.Value;
		const EVBCombatRole   PreviousRole = State.Role;
		// Z 를 무시한다 - 계단/경사에서 수직차가 거리를 튀게 만들어 역할이 흔들린다.
		const double Distance = FVector::Dist2D(MemberOwner->GetActorLocation(), TargetLocation);

		// continue 로 빠지지 않고 if/else 로 수렴시킨다 - 전환 로그를 놓을 자리가 한 곳이어야
		// "어느 경로로 바뀌었든 반드시 찍힌다"가 성립한다.
		if (State.HeldTokenCost <= 0)
		{
			// 토큰이 없으면 대기다. Flanker 는 이번 마일스톤에서 배정하지 않는다.
			State.Role = EVBCombatRole::Surrounder;
		}
		else
		{
			const UVBEnemyConfig* Config = Member->GetCombatConfig();
			const double Threshold = (State.Role == EVBCombatRole::Attacker)
				                         ? Config->AttackReleaseDistance
				                         : Config->AttackCommitDistance;
			State.Role = (Distance <= Threshold) ? EVBCombatRole::Attacker : EVBCombatRole::Approaching;
		}

		// 전환 에지만 남긴다. 매 주기 현재 역할을 찍으면 그룹당 0.5초마다 멤버 수만큼 줄이 쌓여
		// 정작 봐야 할 전환 순간이 그 안에 묻힌다.
		VB_CLOG(State.Role != PreviousRole, Log, "역할 전환: %s %s -> %s (거리 %.0f)",
		        *MemberOwner->GetName(),
		        *UEnum::GetValueAsString(PreviousRole), *UEnum::GetValueAsString(State.Role), Distance);
	}

	// 6) 슬롯 배정
	AssignRingSlots(Group);
}

void UVBCombatCoordinatorSubsystem::AssignRingSlots(FVBCombatGroup& Group)
{
	const UVBCombatSettings* Settings = GetDefault<UVBCombatSettings>();
	AActor* Target = Group.Target.Get();
	if (!Target)
	{
		return;
	}

	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>> Waiting;
	TArray<FVector>                                 WaitingLocations;
	Waiting.Reserve(Group.Members.Num());
	WaitingLocations.Reserve(Group.Members.Num());

	// 모든 멤버를 "슬롯 없음 + 자기 위치"로 되돌린 뒤 대기자만 모은다.
	// 이 초기화가 없으면 지난 주기의 슬롯이 남아, 공격자가 된 멤버가 낡은 대기 좌표를 계속 들고 있게 된다.
	for (TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		const UVBEnemyCombatComponent* Member = Pair.Key.Get();
		const AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner)
		{
			continue;
		}

		Pair.Value.SlotIndex    = INDEX_NONE;
		Pair.Value.SlotLocation = MemberOwner->GetActorLocation();

		if (Pair.Value.Role == EVBCombatRole::Surrounder)
		{
			Waiting.Add(Pair.Key);
			WaitingLocations.Add(MemberOwner->GetActorLocation());
		}
	}

	if (Waiting.Num() == 0)
	{
		return;
	}

	// 슬롯은 좌표가 아니라 방향이다. 각도는 그룹이 정하고 거리는 멤버가 정한다 -
	//  얼마나 물러나는가는 그 적의 사거리에 상대적인 양이라(UVBEnemyConfig::WaitingStandoff)
	//  하나의 반경으로 모두를 세우면 사거리가 다른 적이 자기 사거리 안에서 대기하게 된다.
	//  그래서 결과는 원이 아니라 각도별로 반경이 다른 배치가 된다.
	//
	// 각도 기준은 타겟의 정면이 아니라 월드 +X 축이다 -
	//  정면 기준이면 플레이어가 카메라를 돌릴 때마다 슬롯 전체가 회전해 매 재계산마다 전원이 재배정되고,
	//  경로가 계속 다시 계산되며 적들이 눈에 띄게 흔들린다. 월드 고정이면 배정이 안정적이다.
	const int32   SlotCount = FMath::Max(1, Settings->SurroundSlotCount);
	const FVector Center    = Target->GetActorLocation();

	TArray<FVector> SlotDirections;
	SlotDirections.Reserve(SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		const double Theta = 2.0 * UE_DOUBLE_PI * static_cast<double>(SlotIndex) / static_cast<double>(SlotCount);
		SlotDirections.Add(FVector(FMath::Cos(Theta), FMath::Sin(Theta), 0.0));
	}

	// 멤버별 대기 거리를 미리 뽑는다. 쌍 순회(M*N) 안에서 자산을 매번 조회할 이유가 없다.
	TArray<double> WaitingDistances;
	WaitingDistances.Reserve(Waiting.Num());
	for (const TWeakObjectPtr<UVBEnemyCombatComponent>& Member : Waiting)
	{
		const UVBEnemyConfig* MemberConfig = Member.IsValid() ? Member->GetCombatConfig() : nullptr;
		WaitingDistances.Add(MemberConfig
			? static_cast<double>(MemberConfig->AttackCommitDistance + MemberConfig->WaitingStandoff)
			: 0.0);
	}

	// 전쌍 정렬 그리디. 멤버마다 순차로 최근접 슬롯을 고르는 단순 그리디는 순회 순서가 결과를 좌우해
	//  먼저 처리된 멤버가 유리해지고 경로가 서로 교차한다. 전쌍 정렬은 순서 편향이 없고 결정론적이다.
	//
	// 비용: 쌍 M*N, 정렬 O(M*N log M*N). M=대기 멤버 수, N=SurroundSlotCount(기본 8).
	//  이 비용은 프레임이 아니라 재계산 주기(기본 0.5초)에 한 번만 낸다.
	//  M 에 하드 상한을 두지 않는 것은 의도다 - 상한을 두면 초과 멤버가 역할 없이 멈춰 서고,
	//  그것은 성능이 아니라 동작의 퇴행이다. 실제 비용도 문제가 아니다: M=30 이어도 240쌍이고
	//  0.5초에 한 번 정렬하는 값이다. 여기를 손대야 할 신호는 M 이 커지는 것이 아니라
	//  프로파일러에서 이 함수가 실제로 잡히는 것이다.
	TArray<FVBSlotPairing> Pairings;
	Pairings.Reserve(Waiting.Num() * SlotDirections.Num());
	for (int32 MemberIndex = 0; MemberIndex < Waiting.Num(); ++MemberIndex)
	{
		for (int32 SlotIndex = 0; SlotIndex < SlotDirections.Num(); ++SlotIndex)
		{
			// 같은 각도라도 멤버마다 목표점이 다르다. 그래서 좌표를 쌍 안에서 만든다.
			const FVector Candidate = Center + SlotDirections[SlotIndex] * WaitingDistances[MemberIndex];

			FVBSlotPairing Pairing;
			Pairing.Distance    = FVector::DistSquared(WaitingLocations[MemberIndex], Candidate);
			Pairing.MemberIndex = MemberIndex;
			Pairing.SlotIndex   = SlotIndex;
			Pairings.Add(Pairing);
		}
	}

	Pairings.Sort([](const FVBSlotPairing& A, const FVBSlotPairing& B)
	{
		// (거리, 멤버, 슬롯) 사전식 순서. 정확 비교라야 추이성이 성립한다.
		if (A.Distance != B.Distance)
		{
			return A.Distance < B.Distance;
		}
		if (A.MemberIndex != B.MemberIndex)
		{
			return A.MemberIndex < B.MemberIndex;
		}
		return A.SlotIndex < B.SlotIndex;
	});

	// 내비 투영은 배정이 끝난 목표점에만 건다(멤버당 1회).
	//  종전에는 슬롯 전체를 미리 투영했으나, 멤버마다 목표점이 달라진 지금 그렇게 하면
	//  투영 횟수가 M*N 이 된다. 게다가 투영은 점을 크게 옮길 수 있어 배정 전에 걸면
	//  기하가 아니라 투영 결과가 짝을 정하게 된다 - 짝짓기는 순수 기하로 하는 편이 옳다.
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	const FVector        ProjectionExtent(Settings->SlotNavProjectionExtent);

	TBitArray<> MemberTaken(false, Waiting.Num());
	TBitArray<> SlotTaken(false, SlotDirections.Num());
	for (const FVBSlotPairing& Pairing : Pairings)
	{
		if (MemberTaken[Pairing.MemberIndex] || SlotTaken[Pairing.SlotIndex])
		{
			continue;
		}

		if (FVBCombatMemberState* State = Group.Members.Find(Waiting[Pairing.MemberIndex]))
		{
			FVector SlotLocation = Center + SlotDirections[Pairing.SlotIndex] * WaitingDistances[Pairing.MemberIndex];
			if (NavSystem)
			{
				// 투영 실패 시 원래 좌표를 그대로 쓴다. 벽 안쪽 슬롯으로 MoveTo 하면 적이 벽을 향해 비빈다.
				FNavLocation Projected;
				if (NavSystem->ProjectPointToNavigation(SlotLocation, Projected, ProjectionExtent))
				{
					SlotLocation = Projected.Location;
				}
			}

			const int32 PreviousSlot = State->SlotIndex;
			State->SlotIndex    = Pairing.SlotIndex;
			State->SlotLocation = SlotLocation;

			// 배정이 바뀔 때만 찍는다. 매 주기 현재 배정을 찍으면 0.5초마다 대기자 수만큼 쌓여
			// 정작 봐야 할 재배정 순간이 묻힌다(5단계 역할 전환 로그와 같은 규율).
			//
			// 왜 이 로그가 필요한가: 대기 적이 링에 도달하지 못하는 증상을 만드는 원인이 셋인데
			// (슬롯 미배정 / MoveTo 실패 / 엔진 도착 판정) 셋 다 화면에서 똑같이 "제자리에 서 있다"로
			// 보인다. 슬롯이 어디에 찍혔는지와 멤버가 거기서 얼마나 떨어져 있는지를 남기면 갈린다.
			//   슬롯이 타겟에서 0 근처 = 슬롯 미배정(자기 위치가 남은 것)
			//   슬롯은 링 반경인데 멤버 거리가 안 줄어듦 = 이동이 안 일어남
			const AActor* MemberOwner = Waiting[Pairing.MemberIndex].IsValid()
				                            ? Waiting[Pairing.MemberIndex]->GetOwner()
				                            : nullptr;
			VB_CLOG(PreviousSlot != Pairing.SlotIndex && MemberOwner, Log,
			        "슬롯 배정: %s idx=%d 타겟에서 %.0f 멤버까지 %.0f",
			        *GetNameSafe(MemberOwner), Pairing.SlotIndex,
			        FVector::Dist2D(State->SlotLocation, Center),
			        FVector::Dist2D(State->SlotLocation, MemberOwner ? MemberOwner->GetActorLocation() : Center));
		}
		MemberTaken[Pairing.MemberIndex] = true;
		SlotTaken[Pairing.SlotIndex]     = true;
	}

	// 슬롯보다 대기자가 많으면 남는 멤버는 SlotIndex=INDEX_NONE 과 자기 위치를 유지한다.
	// 가짜 슬롯을 지어내지 않는다 - 부재는 값이 아니고, BT 는 제자리 대기가 된다.
	// 그 경우 MoveTo 는 항상 즉시 도착으로 끝나고 로그도 안 남으므로, 도달 가능해지면
	// 위 배정 로그의 '부재'가 유일한 단서가 된다(적 수가 SurroundSlotCount 를 넘을 때).
}
