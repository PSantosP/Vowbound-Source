// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBSwingCombatProfile.generated.h"

class FDataValidationContext; // IsDataValid 인자 — 헤더에서 Misc/DataValidation.h 를 끌지 않는다(VBSwingImpactProfile.h 동형)

/**
 * FVBSwingCombatScales
 *
 * 스윙 한 번의 전투 수치 보정. 담는 것은 값이 아니라 배율이다.
 *
 * 왜 절대값이 아니라 배율인가 — 이 선택이 두 가지를 동시에 푼다:
 *  1) 미저작과 0저작의 구분 문제. 절대값이면 float 기본값 0 이 "안 건드림"인지 "0으로 정함"인지 알 수 없어
 *     bOverride_ 플래그가 필요해진다. 배율이면 미저작의 표현이 항등원 1.0 이고, x * 1.0f 는 모든 유한 x 에
 *     대해 비트 동일이라 "저작 안 함 == 현행 그대로"가 자료구조 수준에서 공짜로 성립한다.
 *  2) 자산 공유. 절대값 자산을 두 무기가 공유하면 그 순간 두 무기의 절대 데미지가 같아져 무기 정체성이 사라진다.
 *     배율이면 같은 "마무리타는 무겁게" 자산을 세 무기에 그대로 꽂아도 각 무기의 절대값은 유지된다.
 *  UVBSwingImpactProfile::ShakeScale 이 자산 진폭 대신 배율을 택한 것과 같은 계산이다.
 *
 * 한계: leaf 값이 0 인 필드는 배율로 되살릴 수 없다(0 x n = 0). 그건 입도 문제가 아니라 기본값 문제이므로
 *  답은 배율을 절대값으로 바꾸는 것이 아니라 leaf 에 기준값을 저작하는 것이다.
 */
USTRUCT(BlueprintType)
struct FVBSwingCombatScales
{
	GENERATED_BODY()

	// 최종 데미지 배율. 실효 데미지 = AttackPower x leaf.DamageMultiplier x 변형.DamageScale x 이 값.
	// 곱이 네 층인 이유는 층마다 입도가 다르기 때문이다(캐릭터 / 무기 / 변형 / 스윙).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float DamageScale = 1.0f;

	// 넉백 속도 배율. 음수를 막는 이유: 넉백 방향은 판정이 나간 방향으로 이미 확정돼 있고,
	//  음수 배율은 그 방향을 뒤집어 "한 스윙 = 한 방향" 불변식을 깬다(VBGA_MeleeAttackBase 넉백 주석 참조).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float KnockbackStrengthScale = 1.0f;

	// 넉백 상하 성분 배율. 여기만 음수를 허용한다 - 의도적이다.
	// 부호 반전이 곧 "반응 종류"의 차이다: 런처는 위로, 내려찍기는 아래로. 그 상하 성분은 KnockbackZ 가
	//  담당한다고 코드가 이미 적어 두었는데, 값이 무기 leaf 입도라 스윙마다 다르게 줄 수단이 없었다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float KnockbackZScale = 1.0f;

	// 히트스톱 지속시간 배율. 하한이 0 이 아닌 이유: 곱 결과가 0 이면 히트스톱 큐가 값을 거부하는 상태로
	//  떨어진다. leaf 의 HitStopDuration 이 같은 이유로 하한을 두고 있으므로 곱셈 쪽에서도 같이 막는다 -
	//  한쪽만 막으면 우회로가 생긴다.
	// 시간 배율(HitStopTimeDilation)은 여기 없다. 그 값은 작을수록 무겁다는 반대 방향이라 한 자산 안에
	//  섞으면 "배율은 크면 강하다"는 규약이 필드 하나에서만 뒤집힌다. 무게 차이는 지속시간이 낸다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.001"))
	float HitStopDurationScale = 1.0f;

	// 배율이 하나라도 항등원이 아닌가. IsDataValid 와 계측 로그 게이트가 쓴다.
	bool HasAnyEffect() const;
};

/**
 * UVBSwingCombatProfile
 *
 * 스윙 한 번의 전투 수치 보정 저작 단위. "이 타만 더 아프게 / 더 밀리게 / 더 오래 멈추게".
 *
 * 왜 UVBSwingImpactProfile 에 합치지 않았나 — 셋 다 반대로 간다:
 *  1) 공유 관계가 묶인다. 쉐이크 프로필은 "궤적이 같으면 공유"로 만들어졌는데, 여기에 전투 수치를 얹으면
 *     궤적은 같고 무게만 다른 스윙이 공유를 못 해 자산이 쪼개진다. 쪼갠 사본의 축이 원본과 갈라지면
 *     쉐이크 방향이 조용히 바뀐다.
 *  2) 관심사가 뭉개진다. 스윙 축은 카메라 쉐이크 전용으로 남기기로 이미 결정했다(넉백에 쓰지 않는다).
 *     연출 자산에 게임플레이 권위 수치를 넣는 것은 그 결정을 자료구조로 되돌리는 것이다.
 *  3) 소유권 규칙이 갈라진다. 쉐이크 프로필은 GameplayCueNotify(클라 연출)가 SourceObject 로 읽고,
 *     이 자산은 GA(서버 권위)만 읽는다. 독자가 다르면 홈이 다르다.
 *
 * 왜 구조체가 아니라 자산인가: 노티가 GameplayEvent 페이로드로 실어 보내야 하는데 그 슬롯이
 *  UObject 참조(OptionalObject/OptionalObject2)라서다. USTRUCT 는 실리지 않는다.
 *
 * 저작 지점: 공격 몽타주의 Attack Trace 노티.SwingCombatProfile - 스윙 단위, 이 하나뿐이다.
 *  무기 leaf 에 기본 프로필을 두지 않는다. 부재의 결과가 "무보정 = 현행 그대로"이므로 코드에 리터럴이
 *  생기지 않고, leaf 에 기본 배율을 두면 같은 수치를 한 자산 안에서 두 번 결정하게 된다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBSwingCombatProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBSwingCombatScales Scales;

#if WITH_EDITOR
	// 전 배율이 1.0 인 자산을 저장 시점에 잡는다.
	// 왜 런타임 검사로 충분하지 않은가: 그 자산의 런타임 증상은 "노티에 꽂았는데 아무 일도 안 일어남"이고,
	//  그건 배선 실패와 구분이 안 된다. 저장 시점이면 원인이 즉시 이름을 갖는다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
