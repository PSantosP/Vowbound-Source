// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h" // AttackAbilityInputTag - FGameplayTag 를 값으로 보관
#include "Character/VBTeamTypes.h"
#include "VBEnemyConfig.generated.h"

class UAnimMontage;
class FDataValidationContext; // IsDataValid 인자 — 헤더에서 Misc/DataValidation.h 를 끌지 않는다

/**
 * 적(Enemy) 공통 튜닝 DataAsset.
 * 무엇: VBEnemyBase 의 이동/사망 수치를 외부화 — 여러 적 아키타입이 하나의 에셋을 공유해 중앙에서 튜닝.
 * 왜: 기존엔 값이 VBEnemyBase 클래스 디폴트에 박혀 적 BP 마다 복제됐다(DRY 위반).
 *     DataAsset 로 빼면 에디터에서 즉시 튜닝 가능 — 헤더 디폴트 변경은 Live Coding 으로 BP CDO 에
 *     반영되지 않는 한계를 회피(빌드/재시작 불필요).
 * 가정: VBEnemyBase 가 TObjectPtr<UVBEnemyConfig> 로 참조. 미할당 시 이 클래스의 C++ 디폴트(CDO)가 fallback.
 * 부작용: 없음(순수 데이터).
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBEnemyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 소속 세력. 적 종류마다 다를 수 있으므로 코드가 아니라 여기가 홈이다 -
	// 새 세력의 적을 추가할 때 C++ 을 고치지 않기 위함이다(세력 목록 자체는 EVBTeam).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Team")
	EVBTeam Team = EVBTeam::Infected;

	// 기본 이동 속도 (BeginPlay 에서 CMC MaxWalkSpeed 에 적용)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	float MaxWalkSpeed = 300.0f;

	// 회전 속도 (CMC RotationRate — bUseControllerDesiredRotation AI 회전 경로)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Movement")
	FRotator RotationRate = FRotator(0.0f, 360.0f, 0.0f);

	// 일반 사망 시 Instigator 명성 변동량 (음수 = 명성 하락)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	float DeathReputationChange = -5.0f;

	// 사망(래그돌) 후 액터 제거까지 대기 시간(초)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Death")
	float DeathCleanupDelay = 5.0f;

	// === AI 시야 (AVBAIController 가 OnPossess 에서 소비) ===
	// 왜 컨트롤러가 아니라 적 자산에 있는가: 정찰병과 경비병이 같은 시야를 가지면 배치 설계가 성립하지 않는다.
	//   VBEnemyBase::EnemyConfig 가 EditDefaultsOnly 라 적 BP 마다 다른 자산을 꽂을 수 있으므로,
	//   아키타입별 차등은 코드 변경 없이 자산 하나 더 만드는 것으로 끝난다.
	// 주의: 이 값들은 반드시 런타임(OnPossess)에서 적용해야 한다. 컨트롤러 생성자에서 읽으면
	//   BP/자산이 준 값이 아직 존재하지 않아 영원히 C++ 디폴트로 굳는다.

	// 적을 인지하는 최대 거리(cm). 이 밖은 시야 쿼리 대상 자체가 아니다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float SightRadius = 2000.0f;

	// 이미 본 대상을 놓치는 거리(cm). SightRadius 보다 커야 경계선에서 감지/해제가 떨리지 않는다.
	// 두 값의 차이가 곧 히스테리시스 폭이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float LoseSightRadius = 2500.0f;

	// 시야 반각(도). 정면 기준 좌우 각각 이 각도까지 본다 - 60이면 실제 시야는 120도다.
	// 반각이라는 사실을 놓치면 의도의 두 배로 넓어진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float PeripheralVisionAngleDegrees = 60.0f;

	// 감지 기억 유지 시간(초). 시야에서 사라진 자극이 이 시간 뒤 만료된다.
	// 짧으면 모퉁이 뒤로 숨는 즉시 추격이 끊기고, 길면 시야 밖에서도 계속 따라온다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Perception")
	float SightMaxAge = 5.0f;

	// === AI 전투 조율 (UVBEnemyCombatComponent / 코디네이터 / 공격 GA 가 소비) ===
	// 왜 전역 설정(UVBCombatSettings)이 아니라 여기인가: 이 값들은 아키타입마다 갈린다. 돌격형과 거대몹이
	//   같은 확정 거리·텔레그래프를 가지면 아키타입이라는 개념 자체가 성립하지 않는다.
	//   반대로 토큰 총량·슬롯 수는 그룹 전체의 사실이라 전역 설정이 홈이다.

	// 이 아키타입이 공격 한 번에 소비하는 토큰 코스트(D2). 잡몹 1, 대형몹은 2 이상을 줘
	//   "큰 놈이 붙으면 잔몹이 못 붙는다"를 자산만으로 표현한다.
	// 0 이하는 토큰 시스템을 무의미하게 만들므로(전원 동시 공격) 코디네이터가 1 로 바닥을 잡는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="1"))
	int32 AttackTokenCost = 1;

	// 이 거리 안으로 들어오면 접근을 멈추고 공격을 시작한다(cm).
	// 반드시 공격 자산(UVBAttackConfig)의 AttackRange 보다 짧아야 한다 - 길면 사거리 밖에서 헛스윙한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float AttackCommitDistance = 180.0f;

	// 이 거리 밖으로 벗어나야 공격 역할을 놓고 다시 접근한다(cm). 반드시 AttackCommitDistance 보다 크다.
	// 두 값의 차이가 곧 히스테리시스 폭이다 - SightRadius/LoseSightRadius 와 같은 규율이며,
	//   같게 두면 플레이어가 경계선에서 스트레이프할 때 역할이 매 주기 떨린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float AttackReleaseDistance = 200.0f;

	// 토큰이 없을 때 공격 사거리 밖으로 얼마나 물러나 대기하는가(cm).
	// 실제 대기 거리 = AttackCommitDistance + 이 값. 반경을 직접 적지 않고 사거리에 더하는 이유:
	//   "얼마나 물러나는가"는 그 적의 사거리에 상대적인 양이다. 절대 반경으로 두면 사거리가 다른 적을
	//   추가할 때마다 그 적이 자기 사거리 안에서 대기하게 되어 "누가 지금 위협인가"가 거리로 표현되지 않는다.
	// 종전에는 UVBCombatSettings::SurroundRadius(전역 250)였다. 전역값과 아키타입 사거리(180)의
	//   간격 70 은 아무도 정하지 않은 우연이었고, 적을 하나 더 만드는 순간 깨지는 구조였다.
	// 값이 클수록 대기자가 뒤로 빠져 전장이 넓어 보이고, 작을수록 에워싼 압박이 강해진다.
	//   기본값 70 은 종전 동작을 그대로 재현하는 값이다(250 - 180).
	// 계약: 이 값이 (AttackReleaseDistance - AttackCommitDistance) 보다 커야 한다.
	//   작으면 대기자가 공격 유지 거리 안에 서서 역할 히스테리시스가 깨진다. IsDataValid 가 검사한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float WaitingStandoff = 70.0f;

	// 공격 GA 활성화 후 실제 스윙이 나가기까지의 예고 시간(초, D3). 0 이면 예고 없이 즉시 스윙.
	// 플레이어가 회피 입력을 넣을 수 있는 창이 이 값이다 - 게임 난이도의 1차 노브다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.0"))
	float TelegraphDuration = 0.6f;

	// 예고 중 재생할 몽타주(선택). 미할당이면 제자리에서 멈추는 것 자체가 예고다.
	// 시간(TelegraphDuration)이 이 몽타주의 길이를 따르지 않는 것은 의도다 - 길이를 따르면
	//   타이밍의 홈이 자산 두 곳(수치와 클립)이 되고, 클립을 갈아 끼울 때마다 난이도가 조용히 바뀐다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat")
	TObjectPtr<UAnimMontage> TelegraphMontage = nullptr;

	// 이 아키타입이 쓰는 공격 GA 의 InputTag. BT 노드의 AbilityTag 가 비어 있을 때 여기서 읽는다.
	// 왜 트리가 아니라 여기인가: 트리는 아키타입 전체가 공유하는데 공격 태그는 아키타입마다 갈린다.
	//   태그를 트리에 적으면 아키타입을 하나 더할 때 트리를 통째로 복제해야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(Categories="Input"))
	FGameplayTag AttackAbilityInputTag;

	// 토큰을 쥔 채 접근만 하는 시간의 상한(초). 넘으면 회수해 대기 중인 멤버에게 넘긴다.
	// 이것은 결함이 아니라 정상 재배분이라 로그도 Warning 이 아니라 Log 다.
	// 왜 전역(UVBCombatSettings)이 아닌가: 타당한 상한이 이 아키타입의 MaxWalkSpeed 와 SightRadius 에
	//   달렸다. 느린 거대몹과 빠른 잔몹이 같은 상한을 쓰면 거대몹은 붙기도 전에 매번 토큰을 뺏긴다.
	//   반대로 MaxTokenHoldSeconds 는 '공격 GA 가 안 끝났다'를 잡는 결함 탐지기라 전역이 옳다 -
	//   두 값은 이름이 비슷하지만 성격이 다르다.
	// 저작 지침: (SightRadius - AttackCommitDistance) / MaxWalkSpeed 보다 커야 정상 접근이 잘리지 않는다.
	//   디폴트 기준 (2000-180)/300 = 6.1초이므로 8.0 은 1.3배 여유다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|AI|Combat", meta=(ClampMin="0.5"))
	float MaxApproachHoldSeconds = 8.0f;

#if WITH_EDITOR
	// 저장 시점에 자산 내부 계약을 검사한다. 둘 다 위반해도 런타임 로그가 한 줄도 안 남는 부류라
	//   에디터에서 이름을 붙여 주는 것이 유일한 조기 발견 경로다.
	// 시그니처: UE 5.8 현행은 const 버전(Object.h::UObject::IsDataValid, WITH_EDITOR 안).
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
