// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // FTimerHandle
#include "Subsystems/WorldSubsystem.h"
#include "AI/VBCombatTypes.h"
#include "VBCombatCoordinatorSubsystem.generated.h"

class UVBEnemyCombatComponent;

/**
 * 그룹 안의 멤버 하나에 대한 코디네이터의 결정 기록.
 *
 * 왜 컴포넌트가 아니라 여기 있는가: 역할/토큰/슬롯은 전부 '그룹 안에서의 상대적 결정'이다.
 *   컴포넌트에 복사해 두면 같은 사실의 홈이 둘이 되고, 재계산 사이에 두 값이 갈린다.
 *
 * UPROPERTY 를 붙이지 않는다: UObject 강참조가 하나도 없다(약참조 + POD 뿐).
 *   리플렉션 등록은 GC 강참조가 있을 때만 필요하고, 등록하면 코디네이터가 죽은 적을 살려 두게 된다.
 *   UVBTimeDilationSubsystem::ActiveRequests 와 같은 판단이다.
 */
USTRUCT()
struct FVBCombatMemberState
{
	GENERATED_BODY()

	EVBCombatRole Role = EVBCombatRole::Idle;

	// 0 = 미보유. 이 값들의 합이 곧 사용 중인 토큰량이다 - 가용량 캐시를 따로 두지 않는 이유.
	int32 HeldTokenCost = 0;

	int32   SlotIndex    = INDEX_NONE;
	FVector SlotLocation = FVector::ZeroVector;

	// 임대 시작 시각(월드 게임시간). 후보 수집에 거리 조건이 없으므로 이 시각은 곧 '접근 시작'이다.
	// 접근 구간 상한(UVBEnemyConfig::MaxApproachHoldSeconds)이 재는 값이 이것이다.
	double TokenGrantedTime = 0.0;

	// 공격 GA 가 실제로 켜진 시각. -1 = 아직 공격 전(접근 중).
	// TokenGrantedTime 을 덮어쓰지 않는 이유: 두 구간은 상한도 의미도 다르다. 덮어쓰면
	//   '토큰을 언제 받았는가'가 사라져 접근 구간을 잴 수 없다.
	// 센티널이 0 이 아니라 -1 인 이유: 0 은 월드 시작 직후의 유효한 게임시간이다.
	double AttackStartedTime = -1.0;

	// 토큰을 받은 순간의 타겟까지 수평 거리(cm). -1 = 기준 없음(미보유이거나 타겟이 없었다).
	// 위치 기준 회수(RecomputeGroup 2.6)가 유일한 독자다. '지금 멀다'가 아니라 '부여 때보다
	//   멀어졌다'를 물어야 하기 때문에 든다 - 순번 규칙이 가장 멀리 물러난 자를 뽑으므로
	//   절대 거리로 판단하면 방금 준 토큰을 걸어보기도 전에 도로 뺏는다.
	double DistanceAtGrant = -1.0;

	// 이번 임대에서 공격 구간 초과를 이미 보고했는가. 재계산 주기(0.5초)마다 같은 경고가 반복되면
	//   그 사이에 낀 진짜 신규 결함이 묻힌다.
	bool bReportedHoldOverrun = false;

	// 임대 스코프 필드를 전부 미보유 상태로 되돌린다. Role/Slot 은 임대가 아니라 배치라 건드리지 않는다.
	// 왜 함수인가: 회수 지점이 넷(정상 반납 / 만료 / 접근 초과 / 위치 기준)이고 전부 같은 4줄이었다.
	//   임대 스코프 필드를 하나 더할 때 네 곳을 함께 고쳐야 했고, 하나만 빠뜨리면 지난 임대의 값이
	//   다음 임대로 새는데 로그가 안 남는다. 홈이 하나면 그 실수가 표현 불가능해진다.
	void ClearLease()
	{
		HeldTokenCost        = 0;
		AttackStartedTime    = -1.0;
		DistanceAtGrant      = -1.0;
		bReportedHoldOverrun = false;
	}
};

/**
 * 한 타겟을 둘러싼 전투 그룹 하나.
 * 위와 같은 이유로 UPROPERTY 를 붙이지 않는다.
 */
USTRUCT()
struct FVBCombatGroup
{
	GENERATED_BODY()

	TWeakObjectPtr<AActor> Target;

	TMap<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState> Members;

	// 그룹당 1개. Tick 을 쓰지 않는 대신 이 타이머가 역할/슬롯을 재계산한다(NFR2).
	FTimerHandle RecomputeTimer;
};

/**
 * 적 그룹 전투의 조율 계층 (D1).
 *
 * 무엇: 타겟별로 그룹을 만들어 어택 토큰 배분 / 역할 배정 / 링 슬롯 배정을 한 함수 안에서 원자적으로 결정한다.
 * 왜 UWorldSubsystem: 조율은 per-world 상태다. 월드와 함께 생성/소멸해 레벨 전환 시 stale 그룹이 없고,
 *       레벨에 배치할 액터가 필요 없다(UVBTimeDilationSubsystem 과 같은 형태).
 * 왜 push 인가: BT 가 토큰을 pull 하면 "누가 다음 순번인가"가 BT tick 순서라는 암묵값이 되고,
 *       쿨다운 중인 적이 토큰을 쥐고 못 때리는 교착이 생긴다. push 면 순번이 명시적 규칙이 된다.
 * Tick: 없다. FTickableGameObject 를 상속하지 않는다 - 조율은 그룹당 타이머 1개.
 * 권위: 모든 변이 API 가 입구에서 NM_Client 조기 반환한다. 역할/토큰/슬롯은 복제하지 않는다.
 * 부작용: 그룹당 반복 타이머 1개. 신규 복제/RPC 0개.
 */
UCLASS()
class VOWBOUND_API UVBCombatCoordinatorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 어디서나 접근하는 정적 헬퍼. WorldContext 로 UWorld 를 찾아 그 world 의 서브시스템을 반환.
	UFUNCTION(BlueprintPure, Category="Vowbound|AI", meta=(WorldContext="WorldContextObject"))
	static UVBCombatCoordinatorSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem
	virtual void Deinitialize() override;
	//~ End USubsystem

	// 멤버를 타겟 그룹에 넣는다. 멱등 - 이미 같은 그룹에 있으면 기존 결정(역할/토큰/슬롯)을 보존한다.
	// 그룹이 없으면 만들고 재계산 타이머를 건다.
	void RegisterCombatant(UVBEnemyCombatComponent* Member, AActor* Target);

	// 멤버 기록을 통째로 지운다. 보유 토큰 항이 사라지므로 가용량이 자동으로 회복된다 - 반납 코드가 없다.
	void UnregisterCombatant(UVBEnemyCombatComponent* Member);

	// 지정 코스트로 토큰을 즉시 취득 시도. 코디네이터의 주기 배분(RecomputeGroup)이 정상 경로이고,
	//   이것은 즉시성이 필요한 외부 호출자를 위한 진입점이다.
	bool TryAcquireAttackToken(UVBEnemyCombatComponent* Member, int32 Cost);

	// 토큰 반납. 정상 호출자는 UVBEnemyCombatComponent 의 State.Combat.Attacking 하강 에지 하나뿐이다.
	// 부작용: 그 자리에서 RecomputeGroup 을 부른다 - 회수·정렬·배분·역할·슬롯이 동기로 한 바퀴 돈다.
	//   타이머를 기다리면 다음 적이 붙기까지 한 주기가 통째로 비기 때문이다(2026-08-21 실측 0.14~0.65초).
	//   그룹을 순회하는 도중에 부르면 안 되는 이유이기도 하다.
	void ReleaseAttackToken(UVBEnemyCombatComponent* Member);

	// 공격 개시 통지. 임대 시계를 여기서 다시 찍는다.
	// 왜 필요한가: 토큰은 후보 선정 시점(= 접근 시작)에 부여되므로 부여 시각으로 재면
	//   MaxTokenHoldSeconds 가 헤더가 선언한 '한 번의 공격 행동 상한'이 아니라 접근 시간을 잰다.
	//   2026-08-20 실측으로 그것이 동시 공격 상한을 붕괴시키는 것을 확인했다 - 만료가 공격 도중에
	//   터져 스윙 중인 멤버의 슬롯을 비웠고 세 번째 적이 그것을 집었다.
	// 유일한 호출자는 UVBEnemyCombatComponent 의 State.Combat.Attacking 상승 에지다.
	void NotifyAttackStarted(UVBEnemyCombatComponent* Member);

	FVector       GetAssignedSlotLocation(const UVBEnemyCombatComponent* Member) const;
	EVBCombatRole GetAssignedRole(const UVBEnemyCombatComponent* Member) const;

	// 이 멤버가 쥔 토큰 코스트. 0 = 미보유. 컴포넌트의 HasAttackToken() 이 유일한 소비처다.
	// 왜 역할에서 역산하지 않는가: 역할은 토큰에서 파생된 값이라, 역으로 읽으면 파생 방향이 뒤집혀
	//   역할 규칙을 바꾸는 순간 토큰 보유 판정이 조용히 함께 바뀐다.
	int32 GetHeldTokenCost(const UVBEnemyCombatComponent* Member) const;

	// 진단용 - 이 타겟 그룹의 잔여 토큰. 캐시가 아니라 매번 멤버 보유량의 합으로 계산한다.
	//   캐시 카운터를 두면 반환 경로를 하나만 빠뜨려도 영원히 줄어드는 누수가 표현 가능해진다.
	// 블루프린트에 노출하지 않는다: 매 호출이 멤버 수만큼의 합산이라, BP 가 위젯 틱이나
	//   Event Tick 에서 부르면 빈도에 상한이 없는 O(N) 순회가 된다. 진단은 C++ 과 디버거로 한다.
	//   BP 에서 정말 필요해지면 그때 캐시된 값을 내보내는 별도 경로를 만든다(이 함수를 여는 게 아니라).
	int32 GetAvailableTokens(const AActor* Target) const;

private:
	// 타이머 콜백. 그룹 참조가 아니라 타겟 키를 나른다 - TMap 은 재해싱 시 값의 주소가 바뀌므로
	//   참조/포인터를 캡처하면 dangling 이 된다.
	void OnRecomputeTimer(TWeakObjectPtr<AActor> TargetKey);

	// 정리 -> 토큰 배분 -> 역할 배정 -> 슬롯 배정. 그룹 상태를 바꾸는 유일한 함수다.
	void RecomputeGroup(FVBCombatGroup& Group);

	// 슬롯 방향 생성 + 전쌍 정렬 그리디 배정. RecomputeGroup 의 마지막 단계.
	// 각도는 그룹이 정하고 거리는 멤버가 정하므로 결과는 원이 아니라 각도별로 반경이 다른 배치다.
	void AssignRingSlots(FVBCombatGroup& Group);

	// 그룹 해체(타이머 해제 후 맵에서 제거). 타이머를 먼저 지우지 않으면 소멸한 핸들이 남는다.
	void DisbandGroup(const TWeakObjectPtr<AActor>& TargetKey);

	// 그룹에 재계산 타이머가 없으면 건다. 등록이 멱등이므로 이 함수도 멱등이어야 한다.
	void EnsureRecomputeTimer(FVBCombatGroup& Group, AActor* Target);

	// 멤버 -> 그가 속한 그룹. 멤버가 든 CombatTarget 이 곧 그룹 키라 역인덱스를 따로 두지 않는다
	//   (두면 소속 사실의 홈이 둘이 된다). 약참조 키라 타겟이 파괴된 뒤에도 조회가 성립한다.
	const FVBCombatGroup* FindGroupForMember(const UVBEnemyCombatComponent* Member) const;
	FVBCombatGroup*       FindGroupForMemberMutable(const UVBEnemyCombatComponent* Member);

	// 멤버 -> 그의 결정 기록.
	const FVBCombatMemberState* FindMemberState(const UVBEnemyCombatComponent* Member) const;

	// 잔여 토큰 = 총량 - 멤버 보유량의 합. 저장하지 않고 매번 계산하는 것이 이 설계의 안전성이다.
	int32 ComputeAvailableTokens(const FVBCombatGroup& Group) const;

	bool IsServer() const;

	TMap<TWeakObjectPtr<AActor>, FVBCombatGroup> Groups;
};
