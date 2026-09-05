// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Character/VBWeaponStateTypes.h" // EVBWeaponType (TMap 키) — VBDodgeConfig.h:7 동형
#include "VBParryConfig.generated.h"

class UAnimMontage;
class FDataValidationContext; // IsDataValid 인자 — 헤더에서 Misc/DataValidation.h 를 끌지 않는다

/**
 * FVBGuardSet — 한 무기의 가드 콘텐츠 한 벌(몽타주 2종 + 섹션명 3종).
 *
 * 왜 struct 로 묶는가: 섹션명은 몽타주와 한 몸이다. 따로 두면 "BigSword 몽타주 + 카타나 섹션명"
 *   교차가 표현 가능해지고, 그 조합에서 Montage_SetNextSection 은 무음 실패한다(엔진이 INDEX_NONE 을
 *   그대로 저장하고 로그를 안 남긴다 — FIND-056② 가 막으려던 실패). struct 로 묶으면 그 조합이
 *   타입 수준에서 불가능해진다.
 * 가정: 섹션명 디폴트 = 카타나 저작 관례. 새 무기 행을 추가하면 이미 올바른 값이 들어와 있다.
 * 부작용: 없음(순수 데이터).
 */
USTRUCT(BlueprintType)
struct FVBGuardSet
{
	GENERATED_BODY()

	// 가드 진입/유지 몽타주. null 을 허용한다 — 세트는 등록됐는데 몽타주가 비면 '애니 없는 권위 가드'가 된다.
	//  버그가 아니라 기존 설계다(가드는 몽타주 없이도 성립 — 판정은 정상이고 화면만 없다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry")
	TObjectPtr<UAnimMontage> GuardMontage = nullptr;

	// 패링/블록 성립 순간 재생하는 받아넘김(전신 DefaultSlot, 가드 몽타주를 덮는다).
	// 블렌드인은 반드시 0.06s 로 저작할 것. 기본 0.25s 면 짧은 Accept 클립(BigSword=0.267s)이 페이드인만 하다 끝난다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry")
	TObjectPtr<UAnimMontage> ParryAcceptMontage = nullptr;

	// 가드 진입 섹션. 재생 직후 Loop 로 링크된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry")
	FName GuardStartSection = FName("Default");

	// 가드 유지 섹션. 자기 자신으로 링크해 RMB 홀드 동안 무한 반복.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry")
	FName GuardLoopSection = FName("Loop");

	// 가드 해제 섹션(진입 시 물러난 만큼 복귀하는 루트모션 구간). next=None → 재생 후 자연 블렌드아웃.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry")
	FName GuardEndSection = FName("End");
};

/**
 * 가드/패링 튜닝 DataAsset (DEC-009).
 *
 * 구조 규약 — 2026-07-26 프로파일 승격(BigSword 확장)
 *   무기별 변형이 생겨 프로파일 컴포지션으로 승격했다(전역=플랫 DataAsset / 변형=키드 프로파일 규약).
 *   2026-08-17 정정: 종전 이 줄은 "`VBParryConfig.h:16-17` 이 예고한" 이라고 적었으나 그 위치는
 *     FVBGuardSet 을 struct 로 묶는 이유이고, 예고문은 이 파일 어디에도 없다 - 승격 때 원문이 덮이고
 *     인용만 남은 것으로 보인다. 없는 근거를 가리키느니 규약 자체를 적는다.
 *   골격은 UVBWeaponMovesetConfig::Movesets(TMap, 폴백 없음, 미등록=nullptr) 를 따랐다.
 *   UVBDodgeConfig 의 슬롯 폴백은 일부러 쓰지 않는다. 닷지의 통일 세트(Frank Evade)는 무기 무관 범용
 *   클립이라 어떤 무기로 재생해도 성립하지만, 가드엔 무기 무관 클립이 없다. 폴백을 두면
 *   "BigSword 를 든 채 카타나 가드 포즈"가 나온다 → 폴백 자체가 버그다.
 *
 * 조회 규약 (게이트 ⊥ 콘텐츠)
 *   - WeaponGuardSets 키 존재 = 그 무기의 가드 "권한". UVBGA_Guard 가 이걸로 활성 여부를 판단한다.
 *   - GuardMontage 유무 = "콘텐츠" 유무. 키만 있고 몽타주가 비면 애니 없는 권위 가드(합법).
 *   → Fighter/Magic/None 은 행이 없으므로 현행대로 가드 불가. 확장 = 행 1개, C++ 무변경(CHORE-032 청산).
 *
 * 무엇이 무기별로 가고 무엇이 안 가는가 — 구조가 정해준 비대칭이니 되돌리지 말 것.
 *   무기별(TMap): 몽타주·섹션명. 소비처가 AVBCharacter 내부라 WSC 로 현재 무기를 안다.
 *   전역(root 4종): 패링 윈도우/블록 배수/경직 세기/락아웃 하한. 소비처가 데미지 hub 인데
 *     hub 는 IVBGuardable* 만 쥐어 무기를 모른다. 무기별로 쪼개려면 hub 에 Cast<AVBCharacter> 를
 *     부활시켜야 하고 그건 FIND-057a 를 되돌리는 것이다.
 *   hub 에 WeaponStateComponent 를 넣는 방향은 영구 금지. 정말 필요해지면 FVBGuardSet 에 override 를 넣고
 *     IVBGuardable::GetEffectiveParryWindow() 를 추가한다(캐릭터가 자기 무기를 아니까 hub 는 여전히 모른다).
 *
 * config 소유권 RULE (FIND-057c) — 판단 기준은 "누가 읽는가" 하나다.
 *   GA 만 읽는다 → GA 소유 / 컴포넌트 안에서만 → 그 컴포넌트 / 데미지 hub 가 읽는다 → 피격자 액터.
 *   UVBParryConfig 는 hub 소비처라 AVBCharacter 소유가 옳다.
 *   이번 승격으로도 이 판정은 불변이다. 무기별 몽타주가 추가돼도 전투 수치의 소비처가 여전히 hub 이기 때문.
 *
 * 예외 조항 (2026-08-09 명문화) — 컴포넌트 config 를 외부 액터가 읽어도 되는 유일한 경우:
 *   그 값이 (a) 그 컴포넌트 기능 자체의 튜닝값이고
 *          (b) 컴포넌트가 승인한 단일 접근 경로로만 읽히며
 *          (c) 이관하면 한 기능의 튜닝이 두 자산으로 쪼개져 오히려 나빠질 때.
 *   해당: LockOnLookDampingFactor / SwitchTargetMouseThreshold / SwitchTargetCooldown
 *     — AVBCharacter::HandleLook 이 읽지만 "락온 중 카메라를 어떻게 다루는가"는 락온의 관심사다.
 *   해당 아님: bAutoLockOnParry(아래) / bAutoLockOnGuard(UVBGA_Guard) — 락온 자산에 있었지만
 *     튜닝 주제는 패링과 가드다. 한 기능의 데이터 홈이 둘로 갈라진 상태였고 2026-08-09 이관으로 해소했다.
 *   이 조항은 RULE 을 약화시키지 않는다. 판정 기준("누가 읽는가")은 그대로고 경계만 명문화한 것이다.
 *
 * 경직 주의: 패링 "적 경직"은 GE_Stagger(no-op 확정 FIND-032/033)가 아니라 히트리액트(Event.Combat.Hit)
 *   재사용으로 낸다. StaggerHitReactMagnitude 는 그 이벤트의 EventMagnitude 로 실린다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBParryConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// ── 전투 수치 (전역 — 데미지 hub 소비처. 무기별로 쪼개지 말 것) ──

	// 패링 윈도우(초). GDD 11 §4.1 = 0.15s. 무기별 차등 금지 — 플레이어가 한 번 배워 근육기억으로 쓰는 공정성 숫자다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry", meta=(ClampMin="0.0"))
	float ParryWindow = 0.15f;

	// 패링 성공 후 반격 입력을 받는 시간. 이 동안만 State.Combat.RiposteWindow 가 붙는다.
	// ParryWindow(입력 정밀도)와 다른 축이다 — 이쪽은 보상 창이라 넉넉해야 손맛이 산다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry", meta=(ClampMin="0.0"))
	float RiposteWindow = 0.8f;

	// 패링이 성립하는 순간 그 공격자로 자동 락온할지. 소비처 = AVBCharacter::OpenRiposteWindow.
	// 대상이 이미 확정된 액터(공격자)라 카메라 탐색이 필요 없어 서버에서 바로 건다 — 클라-카메라-우선
	// 계약은 탐색(FindBestTarget)에만 걸리므로 위반이 아니다. 이미 락온 중이면 수동 락을 뺏지 않는다.
	// 부작용: 방어만 하려던 상황에서도 카메라가 공격자에게 끌린다. 싫으면 false.
	// 2026-08-09 UVBTargetLockConfig 에서 이관. 이관 전 저작값 true(리드백 실측) = 이 기본값.
	// hub 가 읽는 값이 아니라 캐릭터가 읽는 값이라, 위의 "무기별로 쪼개지 말 것" 제약과는 무관하다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry")
	bool bAutoLockOnParry = true;

	// 블록 1회당 소모 스태미나. 패링(무효화)에는 소모가 없다 — 정확히 막으면 손해가 없어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry", meta=(ClampMin="0.0"))
	float StaminaCostOnBlock = 10.0f;

	// 패링 성공 보상 스태미나. 블록보다 크게 둬 "막기보다 받아치기"를 유도한다(GDD 11 §4.1).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry", meta=(ClampMin="0.0"))
	float StaminaGainOnParry = 30.0f;

	// 블록 데미지 배수 — 가드 중(윈도우 밖) 피격 시 ×이 값. GDD 70% 감소 = 0.30.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry", meta=(ClampMin="0.0", ClampMax="1.0"))
	float BlockDamageMultiplier = 0.30f;

	// 패링 성공 시 공격자에게 보내는 히트리액트 이벤트 크기(경직 payload).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry", meta=(ClampMin="0.0"))
	float StaggerHitReactMagnitude = 2.0f;

	// Accept 재생 중 락아웃의 하한(초). 실제 락아웃 = max(Accept 길이, 이 값).
	// 왜 무기별이 아닌가(실측): AM_Katana_ParryAccept=0.833s / BigSword Accept=0.267s — 둘 다 0.1 보다 길어
	//   max() 첫 항이 항상 지배한다. 0.1 은 "재생 실패 시에만 쓰이는 진짜 하한"이라 쪼갤 실익 0.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry", meta=(ClampMin="0.0"))
	float MinGuardAcceptLockout = 0.1f;

	// ── 무기별 가드 콘텐츠 ──

	// 미등록 무기는 가드 불가다 (폴백 없음 — 클래스 주석 "조회 규약").
	// 구조는 UVBWeaponMovesetConfig::Movesets 와 동형 — 무기 추가 = 데이터 1행, C++ 무변경.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Parry|Montage")
	TMap<EVBWeaponType, FVBGuardSet> WeaponGuardSets;

	// 무기 타입으로 가드 세트 조회. 미등록이면 nullptr = 가드 불가(호출부가 게이트로 쓴다).
	const FVBGuardSet* FindGuardSet(EVBWeaponType WeaponType) const;

#if WITH_EDITOR
	// 섹션명 계약을 에셋 저장/쿡 시점에 검증(CHORE-031).
	// 왜 런타임 래치가 아닌가: 옛 bGuardSectionContractChecked 는 인스턴스 스코프인데 계약은 에셋 스코프였다 —
	//   카타나로 먼저 가드하면 BigSword 세트는 영영 미검사(커버리지 0). 검사에 필요한 값이 전부 이 DataAsset 위에
	//   있어 런타임 상태가 아예 불필요하다.
	// 커버리지 한계(알고 쓸 것, 2026-07-26 2차 verify): 검증기는 저장되는 그 에셋만 본다. 계약의 한쪽 끝인
	//   섹션명은 여기 있지만 다른 쪽 끝은 몽타주 에셋이다 → `AM_*_Guard` 에서 "Loop" 섹션을 지우고 몽타주만
	//   저장하면 이 DA 는 재검증되지 않아 쿡 시점까지 무검출이다(삭제된 런타임 래치가 잡던 케이스가 여기 해당).
	//   몽타주 섹션을 편집했으면 DA_Parry_Default 도 반드시 재저장할 것 — 그때 이 검사가 다시 돈다.
	// 시그니처: UE 5.8 현행은 const 버전(Object.h::UObject::IsDataValid, #if WITH_EDITOR 안). 비-const/TArray<FText>& 는 5.3 deprecated.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
