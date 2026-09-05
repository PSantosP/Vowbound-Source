// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBWeaponStateTypes.h"
#include "Components/ActorComponent.h"
#include "VBWeaponStateComponent.generated.h"

class AVBCharacter;
class UAbilitySystemComponent;
class UVBWeaponStateConfig;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBWeaponStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBWeaponStateComponent();
	
	FORCEINLINE EVBLocomotionState GetLocomotionState() const { return LocomotionState; }
	FORCEINLINE EVBWeaponType GetCurrentWeaponType() const { return CurrentWeaponType; }
	FORCEINLINE EVBWeaponType GetDefaultWeaponType() const { return DefaultWeaponType; }
	// Tab 재발도용 무기 결정 (FIND-030, user 확정 06-10): 선택 무기(상태2 잔존) → 마지막 사용 무기 → 디폴트.
	// 자동납도가 평상 복귀하며 CurrentWeaponType 을 비워도, 직전에 쓰던 무기를 다시 꺼내는 게 직관과 일치.
	FORCEINLINE EVBWeaponType GetResummonWeaponType() const
	{
		if (CurrentWeaponType  != EVBWeaponType::None) return CurrentWeaponType;
		if (LastUsedWeaponType != EVBWeaponType::None) return LastUsedWeaponType;
		return DefaultWeaponType;
	}
	// 술어 정본 (DEC-006): 무기On = ArmedCombat 뿐 / 전투 = Unarmed 가 아닌 모든 상태 (상태2·3).
	FORCEINLINE bool IsWeaponSummoned() const { return LocomotionState == EVBLocomotionState::ArmedCombat; }
	FORCEINLINE bool IsInCombat() const { return LocomotionState != EVBLocomotionState::Unarmed; }
	FORCEINLINE bool IsTempSheathed() const { return bIsTempSheathed; }

	// 이 상태에서 쓸 로코모션 MM 풀 - 홈은 현재 무기의 ArmedMMPool/SheathedMMPool 이다.
	// None 은 "무기 없는 기본 풀"을 뜻하며 평상(Unarmed)·미등록 무기·미지정이 모두 여기로 온다.
	// 상태 enum 으로 받는 이유: 소비처(VBAnimInstance)가 crouch 리다이렉트 전의 WSC 진실을 들고 있어
	//   컴포넌트가 LocomotionState 를 다시 읽으면 오염된 값과 갈릴 수 있다.
	EVBWeaponType GetMMPoolForState(EVBLocomotionState InState) const;

	// 로코모션이 따라야 할 상태. 파쿠르 임시수납 중에는 무기가 없는 것으로 본다 - 그 구간의 연기와
	//   속도는 평상이어야 한다. 이 판정이 두 곳에 있으면 애니와 속도가 서로 다른 상태를 믿게 된다.
	FORCEINLINE EVBLocomotionState GetEffectiveLocomotionState() const
	{
		return bIsTempSheathed ? EVBLocomotionState::Unarmed : LocomotionState;
	}

	// 지금 써야 할 MM 풀. 이 값 하나가 재생 클립(VBAnimInstance)과 이동 속도 프로파일
	//   (AVBCharacter::UpdateMaxSpeed)을 함께 정한다. 둘을 따로 정하면 보폭과 클립이 어긋나 워프비가
	//   윈도우(0.85~1.5)를 벗어난다 - 대검 납도가 그랬다(속도 270 vs 비무장 클립 500 = 0.54, 2026-08-14).
	FORCEINLINE EVBWeaponType GetMMPool() const { return GetMMPoolForState(GetEffectiveLocomotionState()); }

public:
	// 발도 — 무기On = 전투 자세 (→ ArmedCombat 직행. DEC-006: 비전투+무기On 칸은 없다)
	UFUNCTION(BlueprintCallable, Category="Vowbound|Weapon")
	void SummonWeapon(EVBWeaponType InWeaponType);

	// 납도 — 무기만 집어넣고 전투는 유지 (ArmedCombat → ArmedExploration, DEC-006 ②).
	// 전투 이탈은 TryCombatExit 타이머가 담당 (상태2·3 어디서든 → Unarmed 직행).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Weapon")
	void SheatheWeapon();

	// 전투 자극 (락온/피격 등) — 발도 없이 전투 진입(Unarmed→ArmedExploration), 이미 전투면 타이머 리셋.
	// 공격의 자동 발도는 여기가 아니라 공격 GA 의 EnsureWeaponSummoned 경로가 담당 (DEC-006 ①③ 분리).
	UFUNCTION(BlueprintCallable, Category="Vowbound|Weapon")
	void EnterCombat();
	
	// 전투 타이머 리셋(공격/피격마다 호출)
	void ResetCombatTimer();

	// 전투 이탈 타이머 영구 정지. 사망 시 AVBCharacter::HandleDeath 가 호출한다.
	// 왜 필요한가: 게임오버 화면은 게임을 pause 하지 않으므로 방치하는 동안에도 타이머가 계속 돈다.
	//   CombatExitDelay 가 지나면 TryCombatExit 가 시체를 Unarmed 로 전이시키고, 무기를 든 채였다면
	//   MulticastPlayWeaponChangeActing 이 hand_r 에 수납 VFX 를 스폰한다 - 시체 손에서 이펙트가 터진다.
	// PauseTimer 가 아니라 ClearTimer 인 이유: 사망은 되돌아오지 않는다. 일시정지는 파쿠르 임시수납 몫이다.
	void StopCombatTimer();
	
	// 파쿠르 임시수납 (VBGA_Traversal에서 호출)
	UFUNCTION(BlueprintCallable, Category="Vowbound|Weapon")
	void TempSheatheForParkour();
	
	// 파쿠르 후 재소환(VBGA_Traversal 완료 콜백)
	UFUNCTION(BlueprintCallable, Category="Vowbound|Weapon")
	void RestoreFromTempSheathe();
	
	// 무기 자동소환 보장 (공격 GA에서 호출)
	void EnsureWeaponSummoned(EVBWeaponType InWeaponType);
	
public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnLocomotionStateChanged,
		EVBLocomotionState, NewState,
		EVBWeaponType, NewWeaponType);
	
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Weapon")
	FOnLocomotionStateChanged OnLocomotionStateChanged;
	
	
protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// 전투 진입/이탈/수납 타이밍 튜닝 DataAsset. 미할당 시 CDO 디폴트 fallback (GetWeaponStateConfig).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Weapon|Combat")
	TObjectPtr<UVBWeaponStateConfig> WeaponStateConfig;

	// WeaponStateConfig 안전 접근자 — 미할당이면 클래스 CDO 디폴트 반환(절대 null 아님).
	const UVBWeaponStateConfig* GetWeaponStateConfig() const;

	// 적 감지 (구조적 — 트레이스 채널은 튜닝값 아님)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Weapon|Combat")
	TEnumAsByte<ECollisionChannel> EnemyTraceChannel = ECC_Pawn;
	
	// 무기별 데이터
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Weapon|Data")
	TMap<EVBWeaponType, FVBWeaponSummonData> WeaponDataMap;

	// 무기 최초 소환 시 기본 타입 (이전 보유 무기가 없을 때 사용)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Weapon|Data")
	EVBWeaponType DefaultWeaponType = EVBWeaponType::Katana;

	// 마지막으로 실제 발도한 무기 (FIND-030) — Unarmed 복귀로 CurrentWeaponType 이 비워진 뒤의 Tab 재발도가
	// 디폴트(Katana)로 새지 않게 한다. ServerSummonWeapon 성공 시에만 갱신, 의도적으로 클리어하지 않음.
	// 비복제: 소비처(GA_WeaponSummon)가 서버 권한에서만 읽는다 (MP 하드닝 배치 FIND-002/010 패턴).
	EVBWeaponType LastUsedWeaponType = EVBWeaponType::None;
	
	// 무기 전환 연기 멀티캐스트 — 페어 게이트 몽타주 + 소환 VFX. 코스메틱 전용(상태 변경 없음).
	// 왜 GameplayCue가 아닌 WSC 멀티캐스트: 자동수납 타이머 등 GA를 안 거치는 전환 경로까지
	// 단일 지점에서 커버하고, 무기 상태 복제가 이미 WSC 자체 RPC 패턴이라 컨벤션이 일치.
	// (정식 GameplayCue 전환은 차후 리뷰 후보 — FIND-022 노트)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayWeaponChangeActing(EVBWeaponType FromType, EVBWeaponType ToType, bool bCombatStance);

	// Server RPC
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSummonWeapon(EVBWeaponType InWeaponType);
	
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSheatheWeapon();
	
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerEnterCombat();
	
protected:
	UPROPERTY(ReplicatedUsing=OnRep_LocomotionState)
	EVBLocomotionState LocomotionState = EVBLocomotionState::Unarmed;
	
	UPROPERTY(ReplicatedUsing=OnRep_LocomotionState)
	EVBWeaponType CurrentWeaponType = EVBWeaponType::None;
	
	// NOTE: 아래 3개는 GC/리플리케이션 내부 상태. exposure 키워드 없이 UPROPERTY() 유지.
	UPROPERTY()
	bool bIsTempSheathed = false;

	// 파쿠르 전 상태 기억 파쿠르 후 복원하기 위함
	UPROPERTY()
	EVBLocomotionState StateBeforeTempSheathe = EVBLocomotionState::Unarmed;

	// 파쿠르 전 무기 타입 기억 파쿠르 후 복원하기 위함
	UPROPERTY()
	EVBWeaponType WeaponTypeBeforeTempSheathe = EVBWeaponType::None;

private:
	// 무기가 손에서 바뀌거나 치워지면 가드를 끊는다 (FIND-063, 2026-07-26).
	// 왜 헬퍼인가: 호출처가 3곳(발도·교체 / 납도 / 파쿠르 임시수납)이고 셋 다 같은 이유로 필요하다.
	//   가드 상태(GuardStartServerTime)와 State.Combat.Guarding 이 무기를 따라가지 않으면
	//   "무기를 치웠는데 옛 무기 가드로 패링/블록 판정"이 난다. 한 곳이라도 빠지면 그 경로로 그대로 샌다.
	// AssetTags 가 선행돼야 한다: CancelAbilities 는 대상 GA 의 AssetTags 를 매칭한다 — UVBGA_Guard 생성자의
	//   SetAssetTags(Ability.Defense.Guard) 가 없으면 이 호출은 아무 로그 없이 무음 실패한다.
	// 서버 권위 전용: 가드 상태 자체가 서버 진실이라 클라에서 부를 이유가 없다.
	void CancelGuardAbility();

	// 로코모션 상태 변경 시 클라이언트에서 델리게이트 브로드 캐스트
	UFUNCTION()
	void OnRep_LocomotionState();
	
	// 상태 전환읜 단일 진입점이다.
	void SetLocomotionState(EVBLocomotionState NewState);
	
	// 전투 이탈 타이머 (재)무장. 재무장 지점이 5곳이라 딜레이 출처가 갈라질 수 있어 한 곳으로 모았다.
	// StopCombatTimer 의 ClearTimer 는 여기 포함하지 않는다 — 그쪽은 '정지'이고,
	// 파쿠르 중 사망 시 재점화를 막는 비자명한 불변식이 걸려 있다(그 함수 주석 참조).
	void ArmCombatExitTimer();

	// 타이머 콜백(락온 해제, 적거리, 타이머)
	void TryCombatExit();
	
	// TryCombatExit에서만 호출한다.
	// OverlapMultiByChannel로 EnemyProximityRadius 내 적 검사
	bool AreEnemiesNearby() const;

	// GAS 어빌리티가 태그 조건으로 상태를 판단할 수 있도록
	// ASC에 State.Armed.Katana 등 태그 추가/제거
	void ApplyWeaponTags(EVBWeaponType InWeaponType, bool bAdd);
	
	// CMC 플래그 동기화 
	void ApplyLocomotionCMCState(EVBLocomotionState InState);
	
	// 타이머 핸들 — 전투 이탈 시도용 단일 타이머.
	// (SheatheTimer 는 DEC-006 ④에서 제거 — 전투 이탈 = 납도 동반 Unarmed 직행, 2단계 하강 없음)
	FTimerHandle CombatExitTimerHandle;
	
	// 캐싱 (GC-only)
	UPROPERTY()
	TWeakObjectPtr<AVBCharacter> OwnerCharacter;
};
