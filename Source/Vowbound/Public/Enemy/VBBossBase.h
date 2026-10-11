// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/VBEnemyBase.h"
#include "VBBossBase.generated.h"

class UVBBossConfig;
class UAnimMontage;
struct FVBBossPhase;

/**
 * 보스 공통 클래스 (design .sw/boss/design.md §3, §11). AVBEnemyBase 위에 «페이즈» 만 얹는다.
 * 무엇: 체력이 UVBBossConfig::AdditionalPhases 의 문턱을 지날 때마다 다음 페이즈로 들어간다 -
 *   진행 중인 공격 GA 를 끊고, 전환 몽타주를 틀고, 그동안 BT 의 이동·공격 분기를 태그로 닫고, 쓰는 공격 태그를 바꾼다.
 * 왜 액터가 홈인가: 페이즈는 체력 문턱으로 갈리는 정체성 상태다. 컴포넌트(UVBEnemyCombatComponent)는
 *   «어느 공격인가» 를 이미 액터에게 묻는 방향(GetCombatConfig)이라 같은 방향으로 GetCurrentAttackInputTag 하나를 더 묻는다.
 * 권한: 페이즈 판단과 전환은 서버(OnHealthChanged 가 서버에서만 HandleDeath 를 부르는 것과 같은 자리)에서만 한다.
 *   CurrentPhaseIndex 는 복제되고 OnRep 은 원격 클라의 연출(몽타주)만 맡는다 - 싱글은 리슨 서버라 OnRep 이
 *   안 불리므로(memory onrep_does_not_fire_on_authority) 서버는 EnterPhase 안에서 연출을 직접 튼다.
 * 이름 주의: EVBMatchPhase(AVBGameMode) 는 «매치» 페이즈다. 다른 개념이라 여기는 Boss 접두를 쓴다.
 */
UCLASS(Abstract)
class VOWBOUND_API AVBBossBase : public AVBEnemyBase
{
	GENERATED_BODY()

public:
	AVBBossBase(const FObjectInitializer& ObjectInitializer);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 현재 페이즈의 공격 태그. 0 이면 베이스(EnemyConfig->AttackAbilityInputTag), N 이면 AdditionalPhases[N-1].
	virtual FGameplayTag GetCurrentAttackInputTag() const override;

	// 0 = 첫 페이즈. UVBBossConfig::AdditionalPhases 와의 대응은 그 헤더에 있다.
	int32 GetCurrentPhaseIndex() const { return CurrentPhaseIndex; }

	// 전환 몽타주가 도는 중인가(서버 전용 사실. 클라는 태그도 몽타주도 연출로만 받는다).
	bool IsInPhaseTransition() const { return bPhaseTransitionActive; }

	// EnemyConfig 를 보스 설정으로. 보스 BP 에 잡몹 설정이 꽂혀 있으면 null 이다 - 그때는 페이즈가 없고 BeginPlay 가 1회 경고한다.
	const UVBBossConfig* GetBossConfig() const;

protected:
	virtual void BeginPlay() override;

	// 베이스가 사망을 판정한 뒤 페이즈 문턱을 본다. 죽었으면 페이즈는 보지 않는다.
	virtual void OnHealthChanged(const struct FOnAttributeChangeData& Data) override;

	// 전환 중 죽으면 전환 상태(태그·델리게이트)를 먼저 걷는다. 래그돌이 몽타주를 끊는 경로에 기대지 않는다.
	virtual void HandleDeath() override;

	// 지금 페이즈. 서버가 EnterPhase 로 올리고, 원격 클라는 OnRep 으로 연출만 받는다.
	// 노출 키워드가 없으므로 Category 를 붙이지 않는다(GC-only 규칙과 같은 UHT 판정).
	UPROPERTY(ReplicatedUsing=OnRep_CurrentPhaseIndex)
	int32 CurrentPhaseIndex = 0;

	// 이번 전환이 «연출 없이» 페이즈만 바꾸는가. 서버가 EnterPhase 에서 정하고(엎드린 채 문턱을 지남 - design .sw/boss/design.md §14.4),
	//   서버의 BeginPhaseTransition 과 원격 클라의 OnRep_CurrentPhaseIndex 가 같은 값을 본다.
	// 왜 판단을 복제 데이터에 싣는가(FIND-131): 클라가 Downed 태그를 따로 읽으면 태그(ASC 서브오브젝트)와 이 인덱스(액터 프로퍼티)의
	//   도착 순서가 보장되지 않아 같은 프레임에도 답이 갈릴 수 있다. 같은 액터의 프로퍼티끼리는 한 번치로 오고 RepNotify 는 번치의
	//   값이 전부 적용된 뒤 불리므로, OnRep 안에서 이 값을 읽는 것은 안전하다. 사망 쪽이 ReplicatedDeathState 에 판단을 실은 것과 같은 모양.
	UPROPERTY(Replicated)
	bool bPhaseTransitionSilent = false;

	UFUNCTION()
	void OnRep_CurrentPhaseIndex();

private:
	// 서버 전용. 페이즈를 올리고 전환을 시작한다. 두 문턱을 한 대에 지나면 마지막 것 하나로 들어간다(연출은 한 번).
	void EnterPhase(int32 NewPhaseIndex);

	// 서버 전용. 전환 태그·슈퍼아머를 걸고 몽타주를 튼다. 몽타주가 없거나 재생이 실패하면 즉시 끝낸다.
	void BeginPhaseTransition(const FVBBossPhase& Phase);

	// 서버 전용. 멱등 - 몽타주 종료 델리게이트·사망·재생 실패 세 경로가 여기로 모인다.
	void EndPhaseTransition();

	// Serial 은 BindUObject 페이로드. 전환 중에 다음 문턱을 또 지나면 앞 몽타주의 종료 델리게이트가 블렌드아웃 뒤에 늦게
	//   도착하는데(AnimInstance.cpp::UAnimInstance::QueueMontageEndedEvent), 그것이 새 전환의 태그를 벗기면 안 된다.
	//   세대가 다르면 무시한다. 몽타주 포인터 비교로는 안 된다 - 두 페이즈가 같은 몽타주를 쓸 수 있다.
	void OnTransitionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Serial);

	// 연출만. 서버(EnterPhase)와 원격 클라(OnRep) 양쪽이 부른다. 반환 = 실제 재생 길이(0 이면 실패).
	float PlayTransitionMontage(const FVBBossPhase& Phase);

	// 서버 전용 래치들. 태그를 «걸었을 때만» 떼기 위해 둔다(공격 GA 의 bGrantedSuperArmor 와 같은 이유).
	bool bPhaseTransitionActive = false;
	bool bGrantedTransitionSuperArmor = false;

	// 전환 세대. BeginPhaseTransition 마다 1 오른다. OnTransitionMontageEnded 가 자기 세대인지 대조한다.
	int32 PhaseTransitionSerial = 0;

	// 전환 계측(서버 전용). 종료 로그 한 줄이 «몽타주가 끝까지 돌았나 · 그동안 BT 가 보스를 움직였나» 를 답한다 -
	//   PIE 는 user 몫이라 이 두 가지를 로그 없이 물으면 체감 보고에만 기대게 된다.
	FVector PhaseTransitionStartLocation = FVector::ZeroVector;
	double PhaseTransitionStartTime = 0.0;
};
