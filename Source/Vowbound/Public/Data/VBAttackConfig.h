// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Engine/EngineTypes.h" // ECollisionChannel (HitTraceChannel) — transitive 의존 제거, VBTraversalConfig.h 와 동일 패턴
#include "Engine/DataAsset.h"
#include "AbilitySystem/VBHitImpact.h" // EVBHitImpact (HitImpact)
#include "VBAttackConfig.generated.h"

class UAnimMontage;
class UVBWeaponStyleBehavior;
class UVBSwingImpactProfile;

/**
 * FVBAttackMontageVariant
 * 한 공격의 동등 변형 하나. "어느 것이 맞는가"가 아니라 "동등한 것 중 하나"라
 * TMap 키가 아니라 배열 원소다(무기별 세트인 FVBDodgeMontageSet 과 다른 축).
 */
/**
 * FVBStackFullEffect
 * 스택형 추가효과 하나와, 그것이 한도에 도달했을 때 터질 효과들의 짝.
 * 임계값은 여기 두지 않는다 — 스택형 GE 자신의 StackLimitCount 가 유일한 홈이다.
 *  여기에 숫자를 또 두면 자산과 코드가 서로 다른 한도를 믿게 된다.
 */
USTRUCT(BlueprintType)
struct FVBStackFullEffect
{
	GENERATED_BODY()

	// 쌓이는 쪽(예: GE_Bleed_Katana). OnHitExtraEffects 에도 들어 있어야 실제로 쌓인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TSubclassOf<UGameplayEffect> StackingEffect;

	// 한도 도달 시 대상에게 적용할 것들(예: GE_Bleed_Burst, GE_Stagger). 순서대로 적용된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TArray<TSubclassOf<UGameplayEffect>> BurstEffects;
};

// 변형이 어느 쪽을 향해 휘두르는가(공격자 기준). Any = 방향 무관(종전 변형 전부).
// UVBGA_EnemyMeleeAttack::SelectVariantIndex 가 표적의 좌우로 먼저 거른 뒤 거리 대역을 본다(design .sw/boss/design.md §14.3).
// 첫 사용처 = 보스가 엎드린 채 휘두르는 Down_Attack_Left/Right - 반대쪽 팔을 휘두르면 안 맞는다.
UENUM(BlueprintType)
enum class EVBAttackSide : uint8
{
	Any,
	Left,
	Right
};

USTRUCT(BlueprintType)
struct FVBAttackMontageVariant
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	// 이 변형이 휘두르는 쪽. Any 가 아닌 변형이 하나라도 있으면 표적의 좌우(공격자 로컬 Y 부호, VBComputeHitDirection 과 같은 규약)와
	//   맞는 변형만 후보가 된다. 어느 클립이 어느 쪽인지는 손 위치를 재서 채운다 - 이름을 믿지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat")
	EVBAttackSide Side = EVBAttackSide::Any;

	// GA 의 MontagePlayRate 에 곱해진다. 1.0 = 클립 원래 템포.
	// 왜 변형별로 두나: 변형끼리 클립 길이가 다르면 후딜이 들쭉날쭉해진다(카타나 실측 1.667s vs 2.5s).
	//   GA 의 MontagePlayRate 는 GA 단위라 변형별로 줄 수 없다.
	//   루트모션은 재생속도에 비례해 속도만 바뀌고 이동거리는 보존되므로,
	//   이 값으로 거리를 건드리지 않고 템포만 맞출 수 있다(DodgeMontagePlayRate 와 같은 성질).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.1"))
	float PlayRateScale = 1.0f;

	// config 의 DamageMultiplier 에 곱해진다. 1.0 = 그대로.
	// 왜 변형별로 두나: 변형끼리 타격 횟수가 다르면 총 데미지가 갈린다.
	//   트레이스 노티는 타격마다 하나이고 ProcessedActors 가 호출마다 초기화되므로
	//   2타 변형은 같은 적을 두 번 때린다(VBGA_MeleeAttackBase.cpp::PerformAttackTrace 의 ProcessedActors 선언과 등록 —
	//   히트당 1회 등록이라 노티가 둘이면 같은 적이 두 번 걸린다).
	//   DamageMultiplier 는 config 단위라 변형별로 줄 수 없어 2타 변형만 총량이 두 배가 된다.
	//   이 값으로 타격 수를 상쇄한다 - 예: 2타면 0.5 를 줘 1타 변형과 총량을 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float DamageScale = 1.0f;

	// 이 변형의 멈추는 거리(cm). 음수 = 자산의 WarpApproachOffset 그대로.
	// 왜 변형별로 두나: 멈추는 거리는 «그 클립의 손 뻗음» 이 정하는데(design .sw/boss/design.md §12.8) 변형끼리 뻗음이 다르다
	//   (트롤 01 280 / 03 200 / 04 160). 자산 하나의 값으로 묶으면 짧은 클립이 긴 클립의 거리에서 멈춰 헛치거나, 긴 클립이 짧은
	//   클립의 거리까지 눌려 발이 민다. 해석 홈은 UVBAttackConfig::GetVariantApproachOffset 하나 - 계획·선택·워프가 다 그것을 읽는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat", meta=(ClampMin="-1.0"))
	float WarpApproachOffsetOverride = -1.0f;
};

/**
 * 공격 어빌리티 설정 DataAsset
 * 공격 유형별 수치를 에디터에서 관리
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBAttackConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVBAttackConfig();

	// 판정 사거리(cm, 애니 고유 = 배율 1 단위). 읽을 때는 GetScaledAttackRange - 공격자 몸 배율을 곱한다(AttackRadius·멈추는 거리도 같다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	float AttackRange = 200.0f;

	// 이 공격에 플레이어가 무엇으로 답할 수 있는가(design §11). Attack.Answer.Parryable(기본, 가드·패링 통함, 흰색) /
	//   Attack.Answer.DodgeOnly(가드 무시, 빨강). 두 소비자가 같은 값을 읽는다 - 적 예고 큐가 색·소리로 «약속» 하고,
	//   데미지 허브가 스펙의 동적 태그로 «이행» 한다. 값이 하나라 약속과 이행이 어긋날 수 없다.
	// 왜 적 아키타입이 아니라 여기인가: 같은 적이 잡기와 베기를 섞으면 답은 공격마다 갈린다.
	// 플레이어 공격도 이 값을 실어 보내지만 적은 가드하지 않으므로 지금은 읽는 곳이 없다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(Categories="Attack.Answer"))
	FGameplayTag AttackAnswer;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	float AttackRadius = 50.0f;

	// 적중 판정 trace 채널. 왜: Traversal(UVBTraversalConfig::TraceChannel)과 동일 패턴 —
	// 향후 적 전용 채널 분리/투과 판정 변경을 데이터로. 디폴트 ECC_Pawn = 현행 동작 보존.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TEnumAsByte<ECollisionChannel> HitTraceChannel = ECC_Pawn;

	// 이 무기 공격의 템포 배율. GA 의 MontagePlayRate 에 곱해진다. 1.0 = 클립 원래 속도.
	// 왜 무기 leaf 인가: 재생속도 노브가 GA 단위(전 무기 공유)와 변형 단위(FVBAttackMontageVariant)
	//   둘뿐이라 "이 무기만 느리게"를 표현할 입도가 없었다. 변형이 없는 무기는 변형 노브도 못 쓴다.
	//   무기마다 클립 템포가 다른데(맨손 5타 3.3초 vs 카타나 5타 7.5초) 그 차이를 데이터로 못 만들면
	//   애니를 다시 뽑는 수밖에 없다.
	// 루트모션은 재생속도에 비례해 속도만 바뀌고 이동거리는 보존되므로 간격 설계가 흔들리지 않는다.
	// 노티 시각은 몽타주 시간축에 상대적이라 함께 스케일된다 - 판정/콤보 창 타이밍이 따로 어긋나지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.1"))
	float MontagePlayRateScale = 1.0f;

	// 데미지
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	float DamageMultiplier = 1.0f;
	
	// 게임필
	// 히트스톱의 유일한 홈이다. 종전엔 GA 기본인자와 큐 폴백에도 같은 값이 있어 홈이 셋이었다.
	// ClampMin 을 두는 이유: 0 은 "히트스톱 없음"이 아니라 배선 오류로 취급된다(VBGC_HitStop 이 거부한다).
	//   0 을 그대로 흘리면 만료 타이머 없는 시간 감속이 걸려 복구되지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.001"))
	float HitStopDuration = 0.04f;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.001"))
	float HitStopTimeDilation = 0.01f;

	// 히트스톱 범위 (2026-09-05, .sw/combat-feel/design.md §8). 참 = 공격자와 맞은 적만 CustomTimeDilation 으로 «멈칫»(세계·카메라는 계속).
	//   거짓 = 종전대로 전역 시간 배율(세계 전체가 멈춤). 참고작(격투·벨트스크롤·GoW)은 예외 없이 전투원만 멈춘다 - user 「게임이 멈춘 느낌」.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GameFeel")
	bool bHitStopCombatantsOnly = true;

	// 피격자를 공격자보다 이만큼(초) 더 멈춘다. 벨트스크롤 실측(펀처 6/8프레임)의 비대칭. 0 = 같게.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GameFeel", meta=(ClampMin="0.0", ClampMax="0.2"))
	float HitStopVictimExtraSeconds = 0.02f;
	
	// GE 참조
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TSubclassOf<UGameplayEffect> DamageEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	FGameplayTag GameplayCueTag;
	
	// 콤보
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combo")
	int32 MaxComboCount = 1;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TArray<FName> ComboSectionNames;

	// === 캔슬 문법 (2026-09-05, 설계 .sw/combat-feel/design.md) ===
	// 왜 무기 leaf 에 두나: 참고작들이 무기(무게)마다 버퍼·캔슬 시점을 다르게 준다. 값을 코드에 두면 무기 하나 바꿀 때마다 빌드가 필요하다.
	// 콤보 입력 버퍼. 창이 열리기 전에 들어온 입력을 이 시간(초) 안이면 창이 열릴 때 소비한다. 0 = 버퍼 없음(종전: 창 안 입력만 인정).
	// 기본 0.5 인 이유: 우리 클립의 «섹션 시작 → 창 열림» 이 0.14~0.72초라 0.25초로는 두 번 누르기가 살지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0", ClampMax="2.0"))
	float ComboInputBufferSeconds = 0.5f;

	// 참 = 창이 열려 있고 «닿는 프레임이 지난 뒤» 저장 입력을 즉시 소비해 다음 타로 넘어간다. 거짓 = 종전대로 창이 닫힐 때 넘어간다.
	// 2026-09-05 정정: 처음엔 창 열림 순간에 넘겼는데 창이 닿기 «직전» 에 열려(카타나 1타 창 0.27 / 닿음 0.30) 그 타의 닿는 프레임이 건너뛰어졌다
	//   (user 「스킵이 되는 느낌」). 그래서 전환 조건에 «닿음» 을 넣었다 - 창 열림과 닿음 중 늦은 쪽이 전환점이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	bool bChainAtWindowOpen = true;

	// AI 자동 콤보 (design .sw/enemy-combo/design.md). true 면 콤보 창이 열릴 때 «입력이 있었던 것» 으로 친다 -
	//   적에게는 버튼이 없으므로 이 플래그가 그 자리다. 잇는 규칙(닿은 뒤 여운 → 섹션 점프)은 플레이어와 같은 코드가 돈다.
	//   플레이어 자산은 false 그대로. MaxComboCount 1 인 자산에서는 아무 뜻이 없다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Combat|Combo")
	bool bAutoChainCombo = false;

	// 닿은 뒤 다음 타로 넘어가기까지의 여운(초). 0 이면 닿는 프레임 직후. 스윙의 팔로스루가 조금 보이게 하는 값.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0", ClampMax="0.5"))
	float ChainDelayAfterHitSeconds = 0.08f;

	// 참 = 창이 닫혔는데 다음 타가 없으면 몽타주를 멈추고 어빌리티를 끝낸다(회복 캔슬). 굳는 시간이 «섹션 끝까지» 에서 «창 닫힘까지» 로 준다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	bool bCancelRecoveryAtWindowClose = true;

	// 회복 캔슬 때 몽타주 블렌드아웃 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RecoveryBlendOutSeconds = 0.15f;

	// 회피 오프셋 유예(초). 회피가 끊은 콤보를 이 시간 안에 같은 무기로 이어 받으면 다음 타부터 시작한다. 0 = 끔.
	// 피격·가드·완료로 끝난 콤보는 기억하지 않는다 - 「맞을 때만 끊긴다」.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0", ClampMax="5.0"))
	float DodgeOffsetGraceSeconds = 1.0f;

	// === 예고 (적 공격 한정, design .sw/enemy-soldier/design.md §3.6) ===
	// 예고 길이·몽타주의 기본은 아키타입(UVBEnemyConfig::TelegraphDuration/TelegraphMontage)이 정한다. 이 둘은 «이 공격만의 예외» 다 -
	//   슈퍼아머 반격은 긴 예고(Charge Begin+Loop)가 곧 표시라 공격마다 달라야 한다. 무브셋(config)과 변형(variant)의 관계와 같은 두 층이다.
	// < 0 = 아키타입 값. 0 = 이 공격은 예고 없음.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="-1.0"))
	float TelegraphDurationOverride = -1.0f;

	// 비면 아키타입의 TelegraphMontage. 길이는 위 값이 정하고 이 클립은 연출이다(아키타입 쪽과 같은 규약).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> TelegraphMontage;

	// === 무기 무브셋 확장 (4-스타일, 2026-06-01) ===
	// 이 무브셋(무기 타입별 약/강)에서 재생할 공격 몽타주.
	// UVBWeaponMovesetConfig 경유로 사용. 무브셋 미할당 시 GA의 레거시 AnimMontage 사용(회귀 안전).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UAnimMontage> AttackMontage;

	// 이 공격의 추가 변형. 비어 있으면 AttackMontage 하나만 쓴다(= 현행 동작).
	// 0번 변형은 AttackMontage 다 - 필드를 배열로 이주시키지 않는 이유는 flat -> 배열 원소 이동이
	//   CoreRedirects 로 커버되지 않아 값이 무음 유실되기 때문이다. 유실 결과는 "몽타주 없음 ->
	//   즉시 Trace 폴백"이라 로그 한 줄만 남기고 게임은 계속 돈다.
	// 왜 TMap 이 아니라 TArray 인가: WeaponDodgeSets 같은 키드 세트는 "어느 것이 맞는가"를 고르지만
	//   변형은 "동등한 것 중 아무거나"라 키가 존재하지 않는다.
	// 주의: 콤보 공격(MaxComboCount>1)에 변형을 넣을 때는 변형끼리 섹션명이 같아야 한다.
	//   섹션 전이는 이름으로 하므로 이름이 다르면 폴백 규칙으로 샌다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TArray<FVBAttackMontageVariant> MontageVariants;

	// 유효 변형 개수(AttackMontage 포함, null 원소 제외). 0 이면 재생할 몽타주가 없다.
	int32 GetMontageVariantCount() const;

	// 커서 값으로 변형을 고른다. 내부에서 개수로 나머지연산하므로 호출부가 범위를 안 지켜도 된다.
	// 반환 Montage 가 null 이면 재생할 것이 없다(호출부가 즉시-Trace 폴백).
	FVBAttackMontageVariant ResolveMontageVariant(int32 VariantIndex) const;

	// 변형의 멈추는 거리(cm, [0, 사거리] 로 잘라서). 변형 override 가 음수면 WarpApproachOffset. 인덱스는 ResolveMontageVariant 와 같은 규칙.
	//   계획(UVBEnemyCombatComponent::PlanNextAttack)·스윙 선택(SelectVariantIndex)·워프(ComputeApproachOffset)가 같은 값을 보게 하는 한 홈.
	// BodyScale = 공격자의 보폭 배율(AVBEnemyBase::ResolveStrideScale). 거리 필드는 애니 고유(배율 1) 단위라 여기서 곱한다.
	float GetVariantApproachOffset(int32 VariantIndex, float BodyScale) const;

	// 판정 사거리·반경(cm)에 공격자의 보폭 배율을 곱한 값(2026-09-29, design .sw/boss/design.md §15).
	// 왜 필드를 직접 읽지 않나: 필드는 애니 고유(배율 1) 단위다. 큰 몸은 같은 클립으로 더 멀리 닿으므로 읽는 자리마다 곱해야 하는데,
	//   곱하는 자리가 흩어지면 한쪽만 곱해져 판정과 멈추는 거리가 어긋난다. 플레이어·배율 1 인 적은 곱하기 1 이라 무영향.
	float GetScaledAttackRange(float BodyScale) const { return AttackRange * BodyScale; }
	float GetScaledAttackRadius(float BodyScale) const { return AttackRadius * BodyScale; }

	// 적중 시 대상에 추가 적용할 GE (출혈/스태거 등 스타일 고유 효과, 데이터 기반).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TArray<TSubclassOf<UGameplayEffect>> OnHitExtraEffects;

	// 위 스택형 효과가 한도에 도달했을 때 터뜨릴 것들.
	// 왜 OnHitExtraEffects 와 배열을 나누는가: 저쪽은 "맞으면 매번", 이쪽은 "쌓이면 한 번"이라
	//  발동 조건이 다르다. 한 배열에 섞으면 원소마다 의미가 갈려 데이터만 보고는 읽을 수 없다.
	// 왜 기존 배열의 원소 타입을 바꾸지 않는가: 타입 변경은 이미 저작된 자산 값을 무음 유실시킨다.
	//  additive 필드가 안전하다.
	// 왜 이 배열이 필요해졌는가: UE 5.8 이 GE 최초 생성 경로에서도 오버플로 핸들러를 부르고
	//  (GameplayEffect.cpp - HandleActiveGameplayEffectStackOverflow 호출 지점, 5.8 은 2곳) 그 핸들러의 OverflowEffects 루프가 스택 한도 도달 여부를 보지 않아,
	//  엔진의 OverflowEffects 를 쓰면 첫 타격부터 터진다. 판정을 우리 쪽으로 가져왔다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TArray<FVBStackFullEffect> StackFullEffects;

	// 이 무브셋(공격) 재생 동안 시전자에게 슈퍼아머를 부여할지 여부.
	// 왜: 거대검 leaf(약공+강공 둘 다) true → 스윙 중 피격돼도 히트리액트로 안 끊김(데미지는 그대로 받음).
	//     (2026-07-16 확장: 최초 강공 전용이었으나 "거대검은 원래 묵직하다" 컨셉으로 거대검 전체로 넓힘.)
	//     데이터 플래그로 무기 전용 분리 — 공격 GA는 4무기 공유라 GA 클래스 태그로는 거대검만 줄 수 없음.
	//     억제 방식 = State.Combat.SuperArmor 태그를 피격자의 UVBGA_HitReact 가 ActivationBlockedTags 로 걸어
	//     히트리액트 어빌리티 자체를 활성화 차단(DEBT-009 2단계, 2026-07-16). 데미지/VFX/사운드는 그대로 들어간다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	bool bSuperArmorDuringAttack = false;

	// 스타일 고유 동작 전략(옵션). null이면 순수 데이터 처리(기본 SphereTrace).
	// 예) 마법=투사체 spawn. 데이터로 표현 못 하는 메커닉만 여기 구현(Phase 4).
	UPROPERTY(EditDefaultsOnly, Instanced, Category="Vowbound|Combat")
	TObjectPtr<UVBWeaponStyleBehavior> StyleBehavior;

	// (B) 접근 모션워핑이 적 정중앙 대신 멈출 사거리 오프셋(cm). 적 중심에서 공격자 쪽으로 이 거리만큼 앞에서 정지.
	// 0 = 정중앙 동작. 런타임에 [0, AttackRange]로 clamp(트레이스가 닿도록 — 헛침 방지).
	// 애니 고유(배율 1) 단위다 - GetVariantApproachOffset 이 공격자 몸 배율을 곱한다(변형 override 도 같다). MinWarpStandoff·FarStartApproachOffset 은 곱하지 않는다(여유값·플레이어 느낌 규칙).
	// 이 값이 곧 스윙 후 적과의 중심간 거리다. 캡슐 반경 합(약 80cm)을 빼면 눈에 보이는 간격이 나온다.
	// 전제: 몽타주 SkewWarp 모디파이어의 bWarpTranslation=1 일 때만 효과가 있다. 0이면 translation 워프가 없어 이 값이 무의미(FIND-035).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	float WarpApproachOffset = 0.0f;

	// 접근 워프 최소 standoff(cm). 워프 목적지가 플레이어보다 항상 살짝 적 쪽에 있게 여유를 남겨
	// ToSyncPoint 가 항상 적을 향하게 함(degenerate 회피, BUG-007). 0=여유 없음. 현행 리터럴(20) 보존.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	float MinWarpStandoff = 20.0f;

	// 멀리서 시작한 스윙(적까지 거리 > AttackRange)의 접근 오프셋(cm). 0 = WarpApproachOffset 그대로.
	// 2026-09-06 user 「회피로 멀어졌을 때 붙는 보정이 없나」: 로그상 워프는 7m 에서도 1.4m 까지 붙고 있었다(SkewWarp, 거리 상한 없음).
	//   «안 붙는다» 로 느껴진 것은 멈추는 거리가 1.5m(WarpApproachOffset)라 붙은 티가 안 나서다. 그래서 먼 시작만 더 가까이 세운다 -
	//   가까이서 시작한 스윙의 간격(user FEEL 로 잡은 150)은 건드리지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float FarStartApproachOffset = 110.0f;

	// (A) 피격 시 대상에 가할 수평 임펄스 세기(cm/s). 공격자 스탠스가 push 결정. 0 = 넉백 없음(현행 보존).
	// 적 CMC BrakingDecelerationWalking=2048 이 빠르게 감쇠 → "짧은 임팩트 슬라이드"(의도된 FEEL). 약하면 값을 올린다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	float KnockbackStrength = 0.0f;

	// (B) 넉백에 실을 상방 속도(cm/s). 2026-07-31 신설 - 위 주석이 예약해 둔 확장이 실제로 필요해졌다.
	// 왜 필요한가: LaunchCharacter 는 속도를 세팅하면서 MOVE_Falling 으로 바꾸지만
	//   bForceNextFloorCheck 도 함께 켠다(CharacterMovementComponent.cpp HandlePendingLaunch).
	//   Z=0 이면 캐릭터가 실제로 뜨지 않아 다음 프레임에 바닥을 찾고 Walking 으로 복귀하며,
	//   거기서 지상 마찰(기본 8)과 제동(기본 2048)이 수평 속도를 0.1초 안에 지운다.
	//   적 CMC 는 이 값들을 설정하지 않아 UE 기본값 그대로다 - 그래서 넉백이 화면에 안 나타났다(user 실측).
	// 선례: 회피가 DodgeVerticalBoost=(0,0,100) 을 더해 같은 문제를 피한다(VBGA_Dodge.cpp:152).
	// 0 = 순수 수평(종전 동작). 값이 크면 붕 뜨므로 짧은 팝 정도가 적당하다.
	//
	// 실제 밀리는 거리는 이 둘의 함수다. KnockbackStrength 단독이 아니다 - 그건 속도(cm/s)다.
	//   체공시간 t = 2 * KnockbackZ / (980 * GravityScale)
	//   거리 d = KnockbackStrength * t + KnockbackStrength^2 / (2 * BrakingDecelerationWalking)
	//            (앞항 = 체공 이동. 적 CMC 는 BrakingDecelerationFalling=0 이라 공중에서 감속이 없다)
	//            (뒷항 = 착지 후 제동 거리)
	//   현재값 예: 280 / 120 -> t=0.245s, d = 68.6 + 19.1 = 약 88cm.
	//
	// 간격 설계의 핵심: 다음 스윙에서 접근 워프가 플레이어를 WarpApproachOffset 까지 다시 당기므로
	//   플레이어는 스윙마다 정확히 d 만큼 전진한다. 즉 d 가 곧 '한 타당 전진 거리'이고,
	//   이것이 너무 크면 매 타 크게 뛰어다니고 너무 작으면 제자리에서 비빈다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	float KnockbackZ = 120.0f;

	// (C) 이 공격이 대상 Poise 에서 깎는 양(D8). 0 = 경직 파이프라인을 타지 않는다(현행 보존).
	// 왜 공격자 소유인가: "이 무브가 얼마나 무거운가"는 때리는 쪽의 성질이다. KnockbackStrength 와 정확히 같은 축이다.
	//   맞는 쪽의 버티는 양(MaxPoise)은 반대로 피격자 소유이고 GE_InitStats 가 정한다 - 두 값이 만나 결과가 난다.
	// 데미지와 같은 스펙에 SetByCaller(Data.PoiseDamage)로 실어 보낸다(넉백 3종과 같은 방식).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0"))
	float PoiseDamage = 0.0f;

	// (D) 이 공격의 반응 등급(DEC-025). 받는 쪽 저항(ImpactResistance)만큼 깎여 실효 등급이 되고, 그것이 반응 콘텐츠를 고른다.
	// 왜 공격자 소유인가: PoiseDamage 와 같은 축이다 - 「얼마나 무거운 무브인가」는 때리는 쪽 성질이다.
	// 왜 피해량에서 유도하지 않는가: user 결정(2026-09-29) - 공격마다 직접 고른다. 유도하면 특정 무브를 원하는 반응에 맞출 수 없다.
	// 기본 Light = 현행(모든 피격이 플린치). 한 스윙만 다르게 하려면 스윙 프로필(UVBSwingCombatProfile)이 덮는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	EVBHitImpact HitImpact = EVBHitImpact::Light;

	// (E) 띄우는 높이(cm). 실효 등급이 띄움일 때만 쓰인다 - 저항에 깎여 넘어뜨림이 되면 무시된다.
	// 왜 KnockbackZ 를 쓰지 않는가: 그 값(자산 17개 전부 120)은 «수평 넉백이 지상 마찰에 안 지워지게 하는 짧은 팝» 이다.
	//   띄우기와 뜻이 다르고, 그것을 키우면 모든 가벼운 타격이 같이 뜬다.
	// 높이로 적는 이유: 디자이너가 생각하는 단위다. 속도는 허브가 sqrt(2 g h) 로 바꾼다(중력 배율 반영).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0", Units="cm"))
	float LaunchHeight = 0.0f;

	// 판정(트레이스)을 대상 쪽으로 틀어 주는 최대 각도(도). 0 = 보정 없음(순수 정면).
	// 왜 필요한가: 트레이스는 캐릭터 정면 일직선 스윕이라 대상이 조금만 옆으로 벗어나도 빗나간다.
	//   워프의 Facing 이 회전을 맞춰 주지만 창 안에서만이고, 캡슐 자체는 락온 strafe 로
	//   컨트롤 회전(카메라)을 따라가므로 카메라가 살짝 어긋나면 그대로 빗나간다(2026-08-02 user 실측).
	// 이 각도 안에 대상이 있으면 판정 방향을 대상 쪽으로 돌린다. 밖이면 정면 그대로 -
	//   즉 등 뒤나 옆의 적이 갑자기 맞는 일은 없다.
	// 반경(AttackRadius)을 키우는 대신 이 방식을 쓰는 이유: 반경은 옆 적까지 무차별로 쓸어
	//   "안 겨눈 대상이 맞는" 결과가 되지만, 조준 보정은 겨눈 대상 하나를 확실히 맞히는 것이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat", meta=(ClampMin="0.0", ClampMax="90.0"))
	float AimAssistMaxAngle = 35.0f;

	// 이 공격의 기본 임팩트 쉐이크 프로필. 트레이스 노티에 프로필이 없을 때만 쓰이는 2순위 폴백이다.
	//
	// 왜 이 필드가 존재해야 하는가: 이것이 없으면 C++ 이 "방향 = 정면"을 리터럴로 들어야 하고,
	//  그 순간 밀림 방향이 데이터가 아니게 된다(규칙 26). 여기 자산을 꽂으면 정면 밀림조차
	//  DA_SwingImpact_Forward.LocalSwingAxis 라는 데이터 값이 된다.
	// 왜 1순위가 아닌가: 입도가 공격당 1개라 콤보 3타가 전부 같은 방향으로 굳는다.
	//  스윙마다 다른 방향은 노티에 프로필을 꽂아서 낸다 — MontageVariants 의 PlayRateScale 이
	//  config 단위 입도로는 부족해 변형 배열로 옮겨진 것과 같은 이유다.
	// 미할당이고 노티도 비어 있으면 쉐이크가 발동하지 않고 GA 가 Warning 을 남긴다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GameFeel")
	TObjectPtr<UVBSwingImpactProfile> DefaultSwingImpactProfile;

};
