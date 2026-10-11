// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBHitReactConfig.generated.h"

class UAnimMontage;
class UGameplayEffect;

/**
 * FVBHitReactSet
 * 한 스탠스의 히트리액트 콘텐츠 묶음.
 *
 * 왜 1필드 struct 인가 (지금은 몽타주 하나뿐인데):
 *  - 방향별(Front/Back/Left/Right) 히트리액트 콘텐츠가 생기면 이 struct 에 필드를 추가하는 것이 확장 지점이다.
 *  - 맵의 값 타입을 TObjectPtr<UAnimMontage> 로 두면 방향 추가 시 TMap 값 타입이 바뀌어
 *    이미 직렬화된 스탠스 엔트리가 전부 소실된다.
 *  - FVBWeaponMoveset(VBWeaponMovesetConfig.h) 가 같은 이유로 같은 형태를 쓴다 — 프로젝트 관례 일치.
 */
// 어디서 맞았나(피격자 기준). 4분면 - 앞·뒤·왼·오른. 히트리액트 몽타주 택일의 키다.
UENUM(BlueprintType)
enum class EVBHitDirection : uint8
{
	Front,
	Back,
	Left,
	Right
};

// 공격자가 피격자 기준 어느 분면에 있나. 피격 몽타주와 경직 몽타주가 같은 답을 봐야 하므로 홈을 하나로 둔다
//   (2026-09-17: 종전에는 UVBGA_HitReact 안에 인라인이었고, 경직에 방향이 생기면서 두 벌이 될 자리였다).
// 왜 «미는 방향» 이 아니라 «맞은 방향» 인가: 넉백 요(SetByCaller)에는 조준 보정이 섞인다. 몸짓은 어디서 맞았나다.
// Instigator 가 없으면 Front - 무방향 자산과 같은 결과라 새 실패 모드를 안 만든다.
VOWBOUND_API EVBHitDirection VBComputeHitDirection(const AActor* Victim, const AActor* Instigator);

// 피격 이벤트의 Instigator 를 «위치를 가진 몸» 으로 푼다. 효과 컨텍스트의 Instigator 는 ASC 소유자라 플레이어가 때리면
//   PlayerState(위치 원점·회전 0)가 온다(AbilitySystemComponent.cpp::UAbilitySystemComponent::MakeEffectContext). 위치·방향을 읽는
//   소비자는 이것을 거친다 - 2026-09-29 에 방향 판정과 넘어짐 회전 두 곳이 같은 결함이었다. 몸을 못 찾으면 null.
VOWBOUND_API const AActor* VBResolveInstigatorBody(const AActor* Instigator);

USTRUCT(BlueprintType)
struct FVBHitReactSet
{
	GENERATED_BODY()

	// 무방향 기본 히트리액트. 방향별 몽타주가 비면 이것으로 폴백한다(AM_HitReaction 이 여기 들어간다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	// 방향별 히트리액트(2026-09-06, design .sw/enemy-soldier/design.md §4). 비면 Montage 폴백 - 기존 자산은 손대지 않아도 그대로 돈다.
	//   방향은 피격자 기준 «공격자가 어디 있나» 다(UVBGA_HitReact 가 이벤트 Instigator 위치로 4분면을 정한다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> MontageFront = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> MontageBack = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> MontageLeft = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> MontageRight = nullptr;

	// 방향 몽타주가 있으면 그것, 없으면 무방향 Montage.
	UAnimMontage* ResolveByDirection(EVBHitDirection Direction) const;
};

/**
 * FVBKnockdownSet
 * 그로기(다운) 콘텐츠와 수치 (design .sw/boss/design.md §14.3). UVBGA_Knockdown 이 읽는다.
 *
 * 왜 이 자산 안인가: 경직(StaggerSet)과 같은 이유 - «맞은 쪽의 반응 콘텐츠» 의 홈은 하나여야 적 BP 가 반응 자산 하나만 꽂는다.
 * 저작 판정은 IsAuthored() 하나다 - 넘어짐과 기상이 둘 다 있어야 다운이 «성립» 한다. 한쪽만 있으면 미저작으로 보고
 *   UVBGA_Knockdown 은 서 있는 경직으로 떨어진다(넘어졌는데 못 일어나는 상태를 만들지 않는다).
 */
USTRUCT(BlueprintType)
struct FVBKnockdownSet
{
	GENERATED_BODY()

	// 넘어지는 클립(보스: Hit_To_Down, 앞으로 엎어진다). 루트모션 그대로.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> FallMontage = nullptr;

	// 엎드린 대기. 끝나면 GA 가 다시 튼다 - 자산에 루프 절을 파지 않는다(자산은 원본 그대로, 반복은 코드의 일).
	// 이 몽타주는 «즉시 무게 1» 로 들어와야 한다 - 블렌드 모드 관성(Inertialization, 권장) 또는 블렌드인 0 (2026-09-29).
	//   대기는 언제나 «앞 몽타주가 이미 빠지는 중» 에 틀린다(넘어짐·다운 피격·반격의 블렌드아웃 알림에서 튼다). 그때 0 에서부터 표준 블렌드로
	//   들어오면 한 프레임 늦은 만큼 두 무게의 합이 1 아래로 내려가고, 모자란 몫만큼 몽타주 아래의 서 있는 자세가 섞여 누운 몸이 뜬다
	//   (AnimInstanceProxy.cpp::FAnimInstanceProxy::GetSlotWeight - 원본 무게 = 1 - 합).
	//   블렌드인 0 은 뜸은 막지만 앞 몽타주의 남은 블렌드아웃까지 0 으로 잘라 뚝 끊긴다(AnimMontage.cpp::FAnimMontageInstance::Stop).
	//   관성은 무게는 즉시 1 로 넣고 자세 차이만 블렌드 시간에 걸쳐 녹인다 - 대신 ABP 의 슬롯 «뒤» 에 관성 노드가 있어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> DownIdleMontage = nullptr;

	// 엎드린 채 맞을 때의 방향별 반응. 방향 판정은 서 있을 때와 같은 함수(VBComputeHitDirection). 비면 반응 없음(발광만).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBHitReactSet DownHitSet;

	// 일어나는 클립(보스: Down_To_Stand, 뒤로 물러나며 선다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> GetUpMontage = nullptr;

	// 엎드려 있는 시간(초). 넘어짐·기상 클립 길이는 여기 안 든다 - 그 둘은 자산이 정한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float DownHoldSeconds = 4.5f;

	// 넘어지는 클립의 이 시각(클립 초) 이후 피격은 넘어짐을 끊고 방향별 다운 피격(DownHitSet)으로 받는다. 음수 = 넘어지는 동안은 안 받는다.
	// 값은 «넘어짐 클립이 엎드린 대기 자세에 닿는 시각» 을 재서 넣는다 - 그 전에 끊으면 다운 피격 첫 자세로 몸이 튄다.
	//   트롤 실측(2026-09-29, 0번 본 기준 뼈 7개 최대 차이 vs Down_Idle t=0): 2.4초 47cm / 2.6초 13cm / 2.8초 4.8cm, 2.8초 이후 루트 이동 0.6cm.
	// 끊긴 만큼(클립 남은 시간)은 엎드린 대기에 더해 전체 다운 길이를 지킨다 - 때린다고 그로기가 짧아지면 안 된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float FallHitAcceptSeconds = -1.0f;

	// 기상 뒤 이 시간 안의 기절 게이지 소진은 넘어지지 않고 서 있는 경직(StaggerSet)이 된다. 0 = 매번 넘어진다.
	//   넘어뜨리는 타격(Event.Combat.KnockedDown, DEC-025)에는 걸리지 않는다 - 이 쿨다운은 «파탄마다 14초 공짜» 를 막으려는 것이고
	//   넘어뜨림은 공격 등급이 이미 고른 반응이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float CooldownSeconds = 25.0f;

	// 기절 게이지 소진(Event.Combat.PoiseBroken)이 넘어뜨리나(DEC-025). true = 보스 그로기(현행). false = 파탄은 서 있는 경직(StaggerSet)이고
	//   넘어짐은 넘어뜨리는 타격으로만 온다 - 병사·감염체가 이것이다(파탄은 지금처럼 3.7초 기절, 넘어짐은 강타가 낸다).
	// 왜 GA 를 둘로 나누지 않나: 같은 이벤트(PoiseBroken)에 경직 GA 와 넘어짐 GA 를 둘 다 걸면 활성 순서에 따라 둘 다 돈다
	//   (design .sw/boss/design.md §14.3 - 한 이벤트에 GA 하나).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	bool bFallOnPoiseBreak = true;

	// 넘어져 있는 동안 자기에게 거는 GE(Infinite). 받는 피해 배수(UVBCombatAttributeSet::DamageTakenScale)의 값은 이 자산이 든다.
	//   비면 배수 없음 - 그래도 다운은 성립한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TSubclassOf<UGameplayEffect> VulnerabilityEffect;

	// 엎드린 채 휘두르는 반격 GA 의 InputTag. 비면 반격 없음. 켜는 쪽은 UVBGA_Knockdown(BT 는 이 동안 닫혀 있다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(Categories="Input"))
	FGameplayTag DownAttackInputTag;

	// 표적이 이 거리(cm, 수평) 안이면 반격한다. 0 = 반격 없음. 반격 클립의 손끝 반경을 재서 채운다(이름을 믿지 않는다).
	//   애니 고유(배율 1) 단위다 - 읽는 자리(UVBGA_Knockdown::TickDownAttackWatch)가 몸 배율(AVBEnemyBase::ResolveStrideScale)을 곱한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float DownAttackTriggerRadius = 0.0f;

	// 반격과 반격 사이 최소 간격(초). 0 이면 연속 반격으로 대기가 사라진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float DownAttackIntervalSeconds = 2.0f;

	bool IsAuthored() const { return FallMontage != nullptr && GetUpMontage != nullptr; }
};

/**
 * UVBHitReactConfig
 *
 * 피격 반응(히트리액트) 데이터 홈. 피격자 스탠스 → 히트리액트 묶음 매핑.
 *
 * 왜 피격자 소유인가:
 *  - 히트리액트는 "맞은 쪽"의 반응이다. 공격자 소유인 UVBAttackConfig 에 두면 의미가 역전된다
 *    (카타나로 때렸다 → 맞은 쪽이 카타나 플린치? 아니다. 맞은 쪽이 카타나 자세였으면 카타나 플린치다).
 *  - 그래서 이 자산은 재생 주체(히트리액트 GA / GCN 베이스)가 EditDefaultsOnly 로 참조한다.
 *
 * 왜 DataAsset 인가:
 *  - GameplayCueNotify_Static 은 CDO 로 실행되어 인스턴스 상태를 가질 수 없다 → 데이터는 외부 자산이어야 한다.
 *  - CLAUDE.md 데이터 주도 원칙: 스탠스별 분기를 C++ if 문이 아니라 데이터로 표현한다.
 *
 * 폴백 규약: 미등록 스탠스 / 미할당 몽타주는 DefaultSet 으로 폴백한다.
 *   (UVBWeaponMovesetConfig::GetAttackConfig 의 "미등록이면 폴백" 관례와 동일 — 회귀 안전)
 *   적(AVBEnemyBase)은 UVBWeaponStateComponent 를 갖지 않으므로 항상 None → DefaultSet 이 쓰인다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBHitReactConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 스탠스 미등록/미할당 시 항상 이것. 오늘의 AM_HitReaction 이 여기 들어간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBHitReactSet DefaultSet;

	// 피격자의 현재 스탠스별 오버라이드. 미등록 = DefaultSet 폴백.
	// 오늘은 비워둔다 — 스탠스별 콘텐츠가 아직 없어 전원 DefaultSet 을 타며 현행 동작이 100% 보존된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TMap<EVBWeaponType, FVBHitReactSet> StanceOverrides;

	// 경직(Poise break) 반응 콘텐츠. 히트리액트보다 무겁고 길다.
	// 왜 새 자산(UVBStaggerConfig)을 만들지 않는가: 둘 다 "맞은 쪽의 반응 콘텐츠"라는 같은 관심사이고,
	//   자산을 가르면 적 BP 가 반응 자산을 두 개 꽂아야 해서 한쪽만 빠지는 사고가 생긴다.
	// 스탠스별 오버라이드를 두지 않는 이유: 경직 콘텐츠는 아직 하나뿐이고, 필요해지면 히트리액트와
	//   같은 형태(TMap<EVBWeaponType, FVBHitReactSet>)로 확장한다 - 그때도 홈은 여기 하나다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBHitReactSet StaggerSet;

	// 무거운 피격(DEC-025, design .sw/hit-reaction-tiers/design.md §3). 실효 등급이 «무거움» 인 타격에 UVBGA_HitReact 가 튼다.
	// 경직(StaggerSet)과 무엇이 다른가: 경직은 기절 게이지가 «쌓여서» 다 깎인 결과이고 State.Combat.Staggered 를 들어 BT 를 닫는다.
	//   무거운 피격은 «한 대가 셌다» 는 반응이라 플린치와 같은 수명(몽타주 하나, HitReacting)이다. 클립이 크고 길 뿐이다.
	// 스탠스별 오버라이드를 두지 않는 이유는 StaggerSet 과 같다 - 콘텐츠가 생기면 그때 같은 형태로 넓힌다.
	// 비어 있으면 가벼운 세트로 떨어지고 GA 가 경고를 남긴다(무반응보다 «약한 반응» 이 낫고, 미저작은 로그로 드러난다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBHitReactSet HeavySet;

	// 가산 플린치(2026-09-29, design .sw/boss/design.md §14.13). 몸통 반응을 틀 수 없는 자리(넘어지는 중·일어나는 중)에서
	//   지금 동작 위에 상체만 움찔하게 얹는다. 전부 «가산» 몽타주여야 하고 슬롯은 DefaultSlot 뒤의 가산 슬롯이다(ABP 에 그 슬롯 노드가 있어야 한다).
	//   가산이 아닌 몽타주는 몸통을 덮어 기상을 가리므로 재생 전에 거절된다(UVBGA_Knockdown::PlayFlinch).
	// 왜 그로기 세트가 아니라 여기인가: «몸통 반응이 막힌 자리의 반응» 은 슈퍼아머(공격 중 경직 면역) 중 타격도 같은 성질이다. 소비자는 지금 그로기 하나다.
	// 비면 그 자리의 타격은 반응 없음(발광만)이고 로그가 이유를 남긴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBHitReactSet FlinchSet;

	// 히트리액트 몽타주 재생 속도. 경직도 이 값을 쓴다 -
	// 근거 없이 노브를 늘리면 두 값이 서로 다른 방향으로 흘러간다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	float MontagePlayRate = 1.0f;

	// 그로기(다운) 콘텐츠와 수치. 비워 두면(IsAuthored false) UVBGA_Knockdown 이 StaggerSet 의 서 있는 경직으로 떨어진다 -
	//   다운 클립이 없는 아키타입(병사·감염체)은 이 필드를 몰라도 된다. design .sw/boss/design.md §14.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	FVBKnockdownSet KnockdownSet;

	// 피격자 스탠스로 히트리액트 몽타주 해석. 미등록/미할당이면 DefaultSet.Montage,
	// 그것도 없으면 nullptr → 호출부가 재생을 스킵한다(크래시 대신 무동작).
	UAnimMontage* ResolveMontage(EVBWeaponType Stance) const;

	// 스탠스 + 방향. 스탠스 세트(없으면 DefaultSet)에서 방향 몽타주, 없으면 그 세트의 무방향 Montage, 그것도 없으면 DefaultSet 순.
	UAnimMontage* ResolveMontage(EVBWeaponType Stance, EVBHitDirection Direction) const;

	// 경직 몽타주 해석. 미할당이면 nullptr 를 돌려준다 - 히트리액트 몽타주로 폴백하지 않는다.
	//   폴백하면 "경직 콘텐츠 없음"이 "가벼운 플린치"로 보여 미저작이 눈에 안 띈다.
	// 형제 ResolveMontage 와 달리 스탠스 인자를 받지 않는다: StaggerSet 은 단일 세트라 스탠스가
	//   결과에 영향을 줄 수 없다. 못 쓰는 인자를 받으면 디자이너가 스탠스별 경직을 저작하고
	//   아무 일도 안 일어나는 것을 에러 없이 겪는다 - 시그니처는 할 수 있는 것만 말해야 한다.
	//   스탠스별 경직 콘텐츠가 생기면 그때 StanceOverrides 와 인자를 함께 추가한다(호출부 1곳).
	UAnimMontage* ResolveStaggerMontage() const;

	// 방향까지 본 경직 몽타주. StaggerSet 의 방향 슬롯이 비면 그 세트의 무방향 Montage 로 떨어진다.
	// 왜 인자 없는 형제를 남기나: 방향을 모르는 호출부(자가 경직·환경 피해)가 생길 수 있고,
	//   그때 Front 를 지어내 넘기면 «앞에서 맞았다» 는 거짓이 자산 선택에 섞인다.
	// 2026-09-17 신설 - 방향별 경직 클립을 가진 첫 콘텐츠가 보스다(design .sw/boss/design.md §13.3).
	//   그전까지는 단일 세트라 방향이 결과를 못 바꿨고, 그래서 시그니처가 방향을 안 받았다.
	UAnimMontage* ResolveStaggerMontage(EVBHitDirection Direction) const;

	// 무거운 피격 몽타주. 방향 슬롯이 비면 HeavySet 의 무방향 Montage, 그것도 없으면 nullptr -
	//   가벼운 세트로의 폴백은 호출자(UVBGA_HitReact)가 경고와 함께 한다. 여기서 폴백하면 미저작이 조용해진다.
	UAnimMontage* ResolveHeavyMontage(EVBHitDirection Direction) const;
};
