// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Character.h"
#include "Animation/VBAnimationTypes.h" // EVBGait - ApplyGait 인자를 값으로 받는다
#include "Character/VBTargetable.h"
#include "Types/VBTeamTypes.h"
#include "Character/VBDownedImpactReceiver.h"
#include "GameplayTagContainer.h" // GetCurrentAttackInputTag 반환형
#include "VBEnemyBase.generated.h"

class UVBAbilitySystemComponent;
class UVBHealthAttributeSet;
class UVBCombatAttributeSet;
class UVBReputationAttributeSet;
class UGameplayEffect;
class UVBEnemyConfig;
class UVBHitFlashComponent;
class UVBDeathDissolveComponent;
class UVBRagdollComponent;
class UVBContextEffectComponent;
class UVBEnemyLookAtComponent;
class UVBEnemyCombatComponent;
class UVBMeleeTraceComponent;
class UContextualAnimSceneActorComponent;
class UMotionWarpingComponent;

/** RPC를 놓친 클라이언트도 현재 사망 단계를 복원하기 위한 지속 상태. */
USTRUCT()
struct FVBEnemyDeathState
{
	GENERATED_BODY()
	UPROPERTY() bool bDead = false;
	UPROPERTY() bool bInteractionDeath = false;
	UPROPERTY() bool bRagdoll = false;
	UPROPERTY() bool bHasDeathMontage = false;
	UPROPERTY() FVector_NetQuantize10 LaunchVelocity = FVector::ZeroVector;
	UPROPERTY() double ServerDeathTime = 0.0;
	UPROPERTY() float RagdollDelay = 0.0f;
	UPROPERTY() TObjectPtr<UAnimMontage> DeathMontage = nullptr;
};

UENUM()
enum class EVBRecoverableRagdollPhase : uint8
{
	None,
	Ragdoll,
	GettingUp
};

/** Persistent phase for clients that were not relevant when the multicast was sent. */
USTRUCT()
struct FVBEnemyRecoverableRagdollState
{
	GENERATED_BODY()
	UPROPERTY() EVBRecoverableRagdollPhase Phase = EVBRecoverableRagdollPhase::None;
	UPROPERTY() FVector_NetQuantize10 LaunchVelocity = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<UAnimMontage> GetUpMontage = nullptr;
	UPROPERTY() double ServerStartTime = 0.0;
};

/**
 * 모든 적의 기초 클래스
 * ASC를 Character가 직접 소유 (NPC: Respawn 없음)
 */
UCLASS(Abstract)
class VOWBOUND_API AVBEnemyBase : public ACharacter, public IAbilitySystemInterface, public IVBTargetable,
                                  public IGenericTeamAgentInterface, public IVBDownedImpactReceiver
{
	GENERATED_BODY()

public:
	//~ Begin IGenericTeamAgentInterface
	// 소속은 적 종류별 config 가 소유한다 - 같은 세력끼리는 Friendly 라 서로 지각하지 않는다.
	// GetEnemyConfig() 는 미할당 시 CDO 를 돌려주므로 절대 null 이 아니다.
	virtual FGenericTeamId GetGenericTeamId() const override;
	//~ End IGenericTeamAgentInterface

	AVBEnemyBase(const FObjectInitializer& ObjectInitializer);
	
	// IAbilitySystemInterface 구현
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// 기본 Ability 부여 목록 (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GAS")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;
	
	// 초기 스탯 설정용 GE (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|GAS")
	TSubclassOf<UGameplayEffect> InitStatsEffect;
	
	virtual bool IsTargetable() const override;
	// 락온 카메라가 조준할 '올린 지점'(가슴). 구현은 .cpp — 소켓/본 조회 + actor+Z 폴백(ZeroVector 반환 금지).
	virtual FVector GetTargetMarkerLocation() const override;
	bool IsDead() const;

	// 락온 카메라가 조준할 마커 본/소켓(가슴 높이). 스켈레톤별 자동 스케일 — UE 마네킹 기본 상체 본.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|LockOn")
	FName MarkerBoneName = FName("spine_03");

	// 위 본/소켓 부재 시 폴백: 캡슐 중심(GetActorLocation) 기준 Z 상승(cm). 가슴 높이대(+40~60).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|LockOn")
	float MarkerFallbackHeight = 50.0f;

	// 가산 플린치 몽타주를 모든 머신에서 지금 동작 위에 얹는다(2026-09-29, design .sw/boss/design.md §14.13). 부르는 쪽 = 서버의 그로기 GA.
	// 왜 멀티캐스트인가: 플린치는 ASC 로 못 튼다(능력의 «현재 몽타주» 가 바뀌어 기상 태스크가 어긋난다) - ASC 몽타주 복제를 못 타므로
	//   따로 실어 보낸다. 연출일 뿐이라 Unreliable 이다 - 하나 빠져도 상태는 안 틀린다.
	// 가산 검사는 부르는 쪽이 한다. 여기서는 같은 슬롯의 앞 가산 몽타주를 멈추고 그룹 정지 없이 튼다(구현 주석).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastPlayAdditiveFlinch(UAnimMontage* Montage);

	//~ Begin IVBDownedImpactReceiver - 처형을 버텨내고 물리로 누운 몸(설계 .sw/downed-hit). 서버에서만 불린다.
	virtual bool IsBodyDownedInPhysics() const override;
	virtual float ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale) override;
	//~ End IVBDownedImpactReceiver
protected:
	virtual void BeginPlay() override;

	// 평소에는 꺼져 있다(생성자 bCanEverTick=false). «몰아지는 래그돌»(A 단계)이 도는 동안에만 켜진다 -
	//   시체 하나를 위해 살아 있는 적 전부가 매 프레임 틱을 돌 이유가 없다.
	virtual void Tick(float DeltaSeconds) override;
	
	// ASC (Character 직접 소유)
	UPROPERTY(VisibleAnywhere, Category="Vowbound|GAS")
	TObjectPtr<UVBAbilitySystemComponent> AbilitySystemComponent;
	
	// AttributeSet (ASC가 라이프타임 관리, GC-only — exposure 키워드 없이 UPROPERTY())
	UPROPERTY()
	TObjectPtr<UVBHealthAttributeSet> HealthAttributeSet;

	UPROPERTY()
	TObjectPtr<UVBCombatAttributeSet> CombatAttributeSet;

	// 피격 플래시 상태 소유자(MID/타이머). GameplayCue 가 FindComponentByClass 로 찾아 위임한다.
	// BP 추가가 아니라 C++ 생성인 이유: 신규 적 BP 마다 배선이 빠질 수 있고 그 누락은 코드 리뷰에 보이지 않는다.
	// 큐에서 lazy 생성하지 않는 이유: 하필 피격 순간에 RegisterComponent 비용을 얹게 된다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|GameFeel")
	TObjectPtr<UVBHitFlashComponent> HitFlashComponent;

	// 시체가 깎여 사라지는 연출의 상태 소유자. HitFlashComponent 와 같은 이유로 C++ 생성이다 -
	//   BP 배선을 빠뜨리면 그 적만 사라지지 않고 시체로 남는데, 그 사고는 로그 없이 조용하다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|GameFeel")
	TObjectPtr<UVBDeathDissolveComponent> DeathDissolveComponent;

	UPROPERTY(VisibleAnywhere, Category="Vowbound|Animation")
	TObjectPtr<UVBEnemyLookAtComponent> LookAtComponent;

	UPROPERTY(VisibleAnywhere, Category="Vowbound|Death")
	TObjectPtr<UVBRagdollComponent> RagdollComponent;
	// 애니 순간 이펙트(발 딛음·땅 내려치기·몸 떨어짐). 적마다 표를 BP 에서 준다 - 트롤 전용 표를 공용 표 앞에(design .sw/context-effects/design.md §3).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TObjectPtr<UVBContextEffectComponent> ContextEffectComponent;

	// 그룹 전투 조율 상태(역할/토큰/슬롯의 조회 창구). HitFlashComponent 와 같은 이유로 C++ 생성이다 -
	// 신규 적 BP 마다 배선이 빠질 수 있고 그 누락은 "이 적만 협조하지 않는다"로만 나타나 눈에 안 띈다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|AI")
	TObjectPtr<UVBEnemyCombatComponent> CombatComponent;

	// 무기 궤적 적중 판정(.sw/weapon-trace). 위와 같은 이유로 C++ 생성이다. 어느 날(손·가상 칼)로 쓸지는 적 BP 기본값의 ShapeSet 이 정한다 -
	//   비어 있으면 판정 창이 열리지 않고 공격 GA 가 옛 구 판정으로 대신하며 경고한다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Combat")
	TObjectPtr<UVBMeleeTraceComponent> MeleeTraceComponent;

	// 처형 장면의 피해자 자리(DEC-019). 플러그인이 이 컴포넌트로 두 액터를 묶는다.
	// 왜 생성자인가: 종전에는 처형이 시작되는 순간 UVBFinisherComponent 가 붙였는데, 그러면 공격자의
	//   씬 바인딩과 이 컴포넌트가 서로 다른 액터 채널로 가서 도착 순서가 보장되지 않는다. 컴포넌트가
	//   늦은 원격 클라에서는 피해자만 씬에 안 들어가 «플레이어가 허공에 처형하고 적은 제 로코모션을 돈다».
	//   호스트 PIE 로는 안 보이는 결함이라 미리 다는 쪽으로 없앴다 - 고치는 게 아니라 경쟁 자체를 지운다.
	// 복제 설정은 없다. 플러그인 생성자가 SetIsReplicatedByDefault(true) 를 이미 부른다
	//   (ContextualAnimSceneActorComponent.cpp::UContextualAnimSceneActorComponent 생성자).
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Finisher")
	TObjectPtr<UContextualAnimSceneActorComponent> FinisherSceneComponent;

	// 공격 몽타주의 SkewWarp 노티가 붙을 자리(design .sw/boss/design.md §12). 종전에는 적에 이 컴포넌트가 없어
	//   병사·보스 몽타주에 걸어 둔 워프 구간이 전부 무동작이었고 루트모션이 생으로 돌았다(보스 03 = 350cm 돌진).
	//   위 셋과 같은 이유로 C++ 생성이다 - BP 마다 붙이면 빠진 적만 «지나쳐 가는» 것으로 늦게 드러난다.
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Animation")
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;

	// 일반 사망 시 명성 변동 GE (GE_ReputationChange와 동일 GE 사용)
	// BP_InfectedEnemy 등에서 설정
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	TSubclassOf<UGameplayEffect> DefaultDeathReputationEffect;

	// 이동/사망 튜닝값 DataAsset. 미할당 시 UVBEnemyConfig CDO 디폴트가 fallback (GetEnemyConfig).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Config")
	TObjectPtr<UVBEnemyConfig> EnemyConfig;

	// EnemyConfig 안전 접근자 — 미할당이면 클래스 CDO 디폴트 반환(절대 null 아님).
	// public 인 이유: AVBAIController 가 빙의한 폰의 시야 값을 읽는다. 필드 자체는 protected 를 유지해
	//   외부는 getter 만 보게 한다 - 널 안전이 getter 에만 있기 때문이다.
public:
	const UVBEnemyConfig* GetEnemyConfig() const;

	// 전투 조율 컴포넌트. BTService 는 폰에서 FindComponentByClass 로 찾지만(폰 타입에 묶이지 않기 위해),
	// 적 코드 안에서는 이 getter 를 쓴다 - 컴포넌트가 항상 존재한다는 사실이 여기서만 보장되기 때문이다.
	UVBEnemyCombatComponent* GetCombatComponent() const { return CombatComponent; }

	// 사망 디졸브를 이 액터 위에 그대로 재생한다. 액터를 죽이지도 파괴하지도 않는다.
	//
	// 왜 있나: 종전에는 이 연출을 보려면 PIE 에서 적을 실제로 죽여야 했다. 값 하나를 고칠 때마다
	//   전투를 한 번 거쳐야 하니 확인 한 번에 수 분이 들고, 그 사이 무엇이 달라졌는지가 흐려진다.
	//   더미를 세워 두고 버튼으로 재생하면 같은 조건을 몇 번이든 되풀이할 수 있다 - 값의 효과를
	//   판정하려면 한 번이 아니라 여러 번 같은 결과가 나와야 하는데, 그 반복을 싸게 만드는 것이 이 버튼이다.
	// 왜 컴포넌트가 아니라 여기인가: 설정(UVBEnemyConfig::DeathDissolve)을 아는 것이 이 클래스다.
	//   컴포넌트에 두면 시험용 설정 필드를 따로 만들게 되고, 그러면 같은 값의 집이 둘이 된다.
	// 한계: 진행은 머티리얼이 View.RealTime 으로 센다. 뷰포트가 Realtime 이 아니면 깎이지 않는다.
	UFUNCTION(CallInEditor, Category="Vowbound 사망 디졸브 시험")
	void PreviewDeathDissolve();

	// 시험으로 깎인 메시를 원래대로 돌린다. 길이 0 이 머티리얼의 꺼짐 게이트다.
	UFUNCTION(CallInEditor, Category="Vowbound 사망 디졸브 시험")
	void ResetDeathDissolvePreview();

	// 걸음을 바꾼다 (design .sw/enemy-gait/design.md).
	// 무엇: EnemyConfig 의 WalkSpeed / RunSpeed 중 하나를 CMC MaxWalkSpeed 에 쓴다. Sprint 는 적에게 없어
	//   Run 과 같이 다룬다 - EVBGait 를 새로 만들지 않고 재사용하는 값이라 미사용 값 하나가 남는 것이 정상이다.
	// 왜 여기가 홈인가: 속도를 쓰는 주체는 하나여야 한다. 종전에는 BeginPlay 한 곳이었고, 걸음을 나눈 뒤에도
	//   여전히 이 함수 하나다. 호출자(UVBBTService_UpdateCombat)는 «어느 걸음인가» 만 정한다.
	// 부작용: CMC MaxWalkSpeed 변경. 값이 실제로 바뀔 때만 VB_LOG 한 줄.
	// 계약 주의 - 같은 이름의 `AVBCharacter::ApplyGait` 와 계약이 다르다(2026-09-07 /verify 지적).
	//   저쪽: 권한 가드 + Sprint 거부 + 서버 RPC 쌍, 속도는 매 프레임 tick 이 다시 계산한다.
	//   이쪽: 권한 가드 없음. 결정은 서버(BTService)에서만 오고 `MaxWalkSpeed` 는 복제되지 않는다.
	//   즉 클라이언트의 적은 BeginPlay 가 세운 걷기 값에 머무는데, 시뮬레이트 프록시는 이동을 복제로
	//   받으므로 화면에는 안 드러난다. 적 걸음이 클라 예측에 쓰이게 되면 그때 가드와 복제가 여기 붙는다.
	void ApplyGait(EVBGait NewGait);

	// 지금 걸음. 전환 에지를 가리기 위해 보관한다 - 매 주기 같은 값을 쓰면 로그가 그룹당 적 수만큼 쌓인다.
	EVBGait GetGait() const { return CurrentGait; }

	// 이 적이 «지금» 쓸 기본 공격 GA 의 InputTag. UVBEnemyCombatComponent::GetAttackInputTag 가 슈퍼아머 반격이
	//   아닐 때 이것을 돌려준다 - «어느 공격인가» 의 홈은 그 함수 하나이고, 이 가상은 그 함수가 액터에게 묻는 창구다.
	// 기본 구현은 EnemyConfig 의 AttackAbilityInputTag 그대로라 잡몹은 종전과 같다.
	//   보스(AVBBossBase)가 페이즈에 따라 다른 태그를 돌려준다.
	// 왜 컴포넌트가 아니라 액터의 가상인가: 페이즈는 액터의 정체성 상태(체력 문턱)라 액터가 홈이고,
	//   컴포넌트는 이미 GetCombatConfig 로 액터에게 묻는 방향이라 같은 방향을 하나 더 쓰는 것뿐이다.
	virtual FGameplayTag GetCurrentAttackInputTag() const;

	// 이 적의 «보폭 배율». 메시가 크면 같은 클립이 그리는 보폭도 그만큼 커지므로, 이 값이
	//   «애니 고유 속도» 와 «지면 속도» 사이의 환산 계수가 된다.
	// 규약: 자산(블렌드스페이스 Speed 축, UVBEnemyConfig 의 Walk/RunSpeed)은 전부 애니 고유 속도로
	//   적는다. 배율은 CMC 경계에서 딱 두 번 만난다 - ApplyGait 가 곱해서 MaxWalkSpeed 에 넣고,
	//   UVBEnemyAnimInstance 가 나눠서 블렌드스페이스에 넘긴다.
	// 왜 여기가 홈인가: 배율은 BP 의 성질이라 자산이 알 수 없는 값이다(그래서 UVBEnemyConfig 의
	//   IsDataValid 도 배율을 모른 채 축 최대와 비교한다 - 자산이 애니 단위라야 그 검사가 성립한다).
	//   식이 두 곳에 흩어지면 한쪽만 고쳐지고, 그때 다리와 실제 이동이 조용히 어긋난다.
	// 균일 배율을 가정한다 - 캐릭터에 비균일 배율은 쓰지 않는다.
	// 한계: UVBEnemyConfig::GetNormalApproachSeconds() 는 이 배율을 모르므로 접근 시간을 실제보다
	//   길게(보수적으로) 잡는다. 토큰 상한이 넉넉해지는 방향이라 안전해서 그대로 둔다.
	// 2026-09-29 규약 확장(design .sw/boss/design.md §15): 공격 자산의 거리(판정 사거리·반경·멈추는 거리)와
	//   그로기 반격 반경도 애니 고유 단위로 적고, 읽는 자리에서 이 배율을 곱한다 - 크기를 바꿀 때 배율 하나만 고치면 된다.
	float GetStrideScale() const;

	// 아무 액터의 보폭 배율. 적이면 GetStrideScale, 아니면(플레이어 등) 1 - 공격 GA 처럼 적·플레이어가 같은 코드를 탈 때 쓴다.
	static float ResolveStrideScale(const AActor* Actor);

	/**
	 * 이번 래그돌은 «던져진» 것이라 물려받은 운동량을 지우지 않는다. 처형 피해자 쪽이 부른다.
	 *
	 * 왜 필요한가: SettleRagdollVelocity 는 「사망 클립의 마지막 자세는 이미 바닥이라 남길 운동량이
	 *   없다」는 전제 위에 서 있다. 처형 피해자는 그 전제가 정반대다 - 클립이 공중에서 끝나고
	 *   (실측: 클립 끝 0.1초의 root 속도가 벌마다 96~217 cm/s - stand_B 95.7 / stand_F 150.9 /
 *   run_B 145.7 / walk_L 216.7) 그 운동량이 곧 나머지 비행이다.
	 *   지우면 공중에서 그대로 멈춘다(2026-09-19 user PIE 「날라가다가 중간에 래그돌 돼」).
	 * 래그돌의 주인은 이 클래스 하나로 두고 «어떤 죽음이었는지» 만 입력으로 받는다 - 전제가 둘이면
	 *   값을 합칠 자리가 없으므로 상황을 구별해 줘야 한다.
	 * 순서: 사망을 일으키기 «전» 에 불러야 한다. 사망이 곧바로 래그돌과 정착 예약으로 이어진다.
	 */
	void KeepRagdollMomentum(const FVector& InLaunchVelocity)
	{
		bRagdollKeepsMomentum = true;
		RagdollLaunchVelocity = InLaunchVelocity;
	}

	// 처형 피해를 버틴 대상에게 다음 사망용 입력을 남기지 않는다.
	void ClearPendingInteractionRagdoll()
	{
		if (!bEnteredRagdoll)
		{
			bRagdollKeepsMomentum = false;
			RagdollLaunchVelocity = FVector::ZeroVector;
		}
	}

	// A surviving interaction victim follows the same physics/capsule path as a corpse,
	// then hands the get-up montage to GA_GetUp when the body has settled.
	bool StartRecoverableRagdoll(const FVector& LaunchVelocity, UAnimMontage* GetUpMontage);
	bool IsRecoverableRagdollActive() const { return bRecoverableRagdollActive; }
	void FinishRecoverableGetUp();

protected:


	// 사망 처리 (서버에서만 호출)
	virtual void HandleDeath();

	// 래그돌/콜리전 전환을 모든 Net Role에 동시 적용 (서버 HandleDeath에서 호출)
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHandleDeathCosmetic(const FVBEnemyDeathState& State);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartRecoverableRagdoll(const FVector& LaunchVelocity, UAnimMontage* GetUpMontage);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartRecoverableGetUp();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFinishRecoverableGetUp();

	void HandleDeathCosmetic(float ElapsedSinceDeath);
	void ApplyDeathState(bool bFromLiveEvent);
	void ReplayDeathState();
	UFUNCTION()
	void OnRep_DeathState();
	UPROPERTY(ReplicatedUsing=OnRep_DeathState)
	FVBEnemyDeathState ReplicatedDeathState;
	UFUNCTION()
	void OnRep_RecoverableRagdollState();
	UPROPERTY(ReplicatedUsing=OnRep_RecoverableRagdollState)
	FVBEnemyRecoverableRagdollState ReplicatedRecoverableRagdollState;
	FTimerHandle DeathStateReplayTimer;
	FTimerHandle DeathRagdollTimer;
	bool bDeathVisualReady = false;
	bool bPendingLiveDeath = false;
	bool bDeathCosmeticStarted = false;
	bool bSuppressRagdollLaunch = false;

	// 래그돌 전환 + 정리 타이머. 사망 몽타주가 없으면 HandleDeathCosmetic 이 곧바로, 있으면 그 몽타주가 끝난 뒤 부른다.
	//   쪼갠 이유: 「무엇을 끄나(충돌·이동·플래시)」와 「언제 물리로 넘기나」의 시점이 갈렸다(design .sw/boss/design.md §13.3).
	void EnterRagdoll();

	// 래그돌이 물려받은 속도를 «다음 틱에» 재고 지운다. 켠 그 프레임에 읽으면 0 이 나온다 -
	//   Chaos 는 키네마틱에서 시뮬레이션으로 넘어가는 바디의 초기 속도를 다음 물리 단계에서 심기 때문이다.
	//   2026-09-18 에 그 자리에서 재고 「0 이니 속도가 원인이 아니다」로 읽어 진단을 한 번 버렸다.
	//   두 번 예약한다(다음 틱 · 0.3초 뒤) - 앞의 것이 지우고, 뒤의 것이 «지운 뒤에도 다시 붙는가» 를 답한다.
	//   뒤에서도 크면 힘의 출처가 애니가 아니라 솔버다(바디 초기 겹침에 의한 밀어내기).
	void SettleRagdollVelocity();

	// 사망 몽타주가 끝났다. 재생 시작 때의 세대 번호를 들고 와 낡은 델리게이트를 무시한다 -
	//   같은 액터가 죽는 동안 다른 몽타주가 끼어들면 그쪽 종료가 먼저 도착할 수 있다.
	UFUNCTION()
	void OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	
	// 사망 후 정리 (래그돌 후 제거)
	void DestroyAfterDeath();

	// 래그돌이 두 번 걸리지 않게 하는 래치. 몽타주 종료와 안전망 타이머가 둘 다 도착할 수 있다.
	bool bEnteredRagdoll = false;

	// 던져진 래그돌인가. 서 있으면 SettleRagdollVelocity 가 재기만 하고 지우지 않는다.
	//   왜 그런지는 KeepRagdollMomentum 주석에 있다.
	bool bRagdollKeepsMomentum = false;

	// 이번 래그돌이 운동량을 남기는가. 던져진 것(bRagdollKeepsMomentum)이거나 설정이 그러라고 하면 참.
	//
	// 왜 함수인가: 지우는 자리가 셋이다(진입 직후 · 다음 틱 · 0.3초 뒤). 각자 조건을 적으면
	//   한 곳만 고치는 날이 오고, 그러면 「지웠는데 안 지워진」 것도 「안 지웠는데 지워진」 것도
	//   로그만으로는 못 가린다. 판정의 홈을 하나로 둔다.
	bool ShouldPreserveRagdollMomentum() const;

	// 래그돌을 켤 때 전 바디에 «설정» 할 월드 속도(cm/s). 더하지 않는다 - 더하면 두 번 돌 때 쌓인다.
	//   0 이면 발사하지 않는다(보통 사망 경로가 그렇다).
	FVector RagdollLaunchVelocity = FVector::ZeroVector;

	// 사망 코스메틱이 시작된 월드 시각. 정리 지연을 «죽은 순간부터» 재려고 든다 - 사망 몽타주가 있으면
	//   래그돌 진입이 그 길이만큼 늦으므로, 빼지 않으면 시체가 자산에 적은 것보다 오래 남는다.
	double DeathCosmeticStartTime = 0.0;

	// === «몰아지는 래그돌» (A 단계) — GASP BP_MovementMode_Ragdoll 의 이식 ===
	//
	// GASP 에서 래그돌은 스위치가 아니라 «이동 상태» 다. 그 상태의 틱이 하는 일은 셋이다 —
	//   ①물리 바디(pelvis)가 있는 자리를 목표로 잡고 ②캡슐을 스윕으로 거기까지 옮기고
	//   ③그 이동량을 나눠 속도로 심는다. 그래서 몸이 앞장서고 캡슐이 따라가며,
	//   액터 위치가 계속 유효하게 남는다.
	// 우리는 Mover 가 아니라 CMC 라 이동 모드를 만들 수는 없다. 만드는 것은 «그 틱 하나» 다.
	//   Mover 의 Try Safe Move And Slide 자리에 CMC 의 SafeMoveUpdatedComponent 가 선다.
	//
	// 주인 지도 - 규칙 26. 이 상태를 쓰는 주체가 둘이다.
	//   «몸» 의 주인은 물리다. 캡슐은 따라가는 쪽이고, A 가 도는 동안 메시의 SetWorldTransform 은
	//   아무도 부르지 않는다. 캡슐을 옮겨도 화면이 안 움직이는 이유: 시뮬레이션 중인 바디의 변환은
	//   월드 공간에 있고, 엔진이 컴포넌트 변환의 역으로 되돌려 컴포넌트 공간 포즈를 다시 만든다
	//   (SkeletalMeshComponent.cpp::USkeletalMeshComponent::PerformBlendPhysicsBones).
	//   둘 다 몸을 옮기려 들면 몸이 두 배로 간다 - 그래서 여기 적어 둔다.

	// A 단계를 시작한다. EnterRagdoll 의 끝에서 부른다.
	void BeginDrivenRagdoll();

	// A 단계 한 틱. Tick 에서만 부른다.
	void TickDrivenRagdoll(float DeltaTime);

	// A -> B. 틱을 끄고 캡슐을 쉬게 한다. 조건을 만족했거나 상한에 걸렸을 때.
	void EndDrivenRagdoll(const TCHAR* Reason);
	void ApplyRecoverableRagdoll(const FVector& LaunchVelocity, UAnimMontage* GetUpMontage);
	void ApplyRecoverableGetUp();
	void ResetRecoverableRagdoll();

	// 누운 몸에 속도 충격을 준다(모든 머신 - 래그돌은 머신마다 따로 시뮬레이션된다). 서버의 몸이 기상 판정의 근거라 서버도 이 길로 받는다.
	//   연출 겸 상태라도 Unreliable 인 이유: 하나 빠지면 그 머신의 몸이 덜 밀릴 뿐 판정·기상은 서버 몸이 정한다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDownedImpact(FVector_NetQuantize Point, FName Bone, FVector_NetQuantize Velocity);

	// 몰아지는 래그돌의 캡슐: 월드에만 막히는 질의 전용. A 단계를 시작할 때와, 누운 채 죽어 사망 쪽으로 넘길 때 같은 값을 건다.
	void ConfigureDrivenRagdollCapsule();

	// 시체 정리(디졸브·파괴) 예약. EnterRagdoll 의 두 갈래(새로 쓰러짐 / 누운 채 죽음)가 같이 부른다.
	void ScheduleDeathCleanup(double ElapsedSinceDeath);

	// 쫓아갈 뼈를 고른다. 설정의 DrivenRagdollFollowBone -> RagdollRootBone -> "pelvis" 순.
	//   골격에 없으면 None 을 돌려주고, 부르는 쪽이 A 를 포기한다(조용히 제자리를 쫓지 않게).
	FName ResolveDrivenRagdollBone() const;

	// A 가 도는 중인가. 이것이 참인 동안에만 액터 틱이 켜져 있다.
	bool bDrivenRagdollActive = false;

	FName   DrivenRagdollBone;
	double  DrivenRagdollStartTime = 0.0;
	FVector DrivenRagdollStartLocation = FVector::ZeroVector;

	// 문턱 아래로 «연속» 몇 프레임 있었나. 한 프레임만 보면 솔버가 튀는 프레임에 속는다.
	int32 DrivenRagdollSettledFrames = 0;
	bool bDrivenRagdollHasFloor = false;
	bool bRecoverableRagdollActive = false;
	// 서버: 쓰러짐이 Downed 를 붙였나. 붙인 쪽만 뗀다 - 넘어짐 GA 의 Downed 와 개수가 섞이지 않게(루즈 태그는 개수로 센다).
	bool bRecoverableDownedTagGranted = false;
	double NextRecoverableGetUpCheckTime = 0.0;
	UPROPERTY(Transient) TObjectPtr<UAnimMontage> RecoverableGetUpMontage;
	FName RecoverableCapsuleProfile;
	FName RecoverableMeshProfile;
	ECollisionEnabled::Type RecoverableCapsuleCollision = ECollisionEnabled::QueryAndPhysics;
	ECollisionEnabled::Type RecoverableMeshCollision = ECollisionEnabled::QueryOnly;
	FCollisionResponseContainer RecoverableCapsuleResponses;
	FCollisionResponseContainer RecoverableMeshResponses;
	ECollisionChannel RecoverableCapsuleObjectType = ECC_Pawn;
	ECollisionChannel RecoverableMeshObjectType = ECC_Pawn;

	// Health Attribute 변경 감지 -> 사망 판정.
	// 왜 protected virtual 인가: 보스(AVBBossBase)가 같은 델리게이트에서 페이즈 문턱을 봐야 한다. 보스가 따로
	//   구독하면 «사망» 과 «페이즈» 가 같은 프레임에 두 번 판단되어 순서가 갈릴 수 있으므로, 한 진입점을 열어 두고
	//   파생이 Super 를 부른 뒤 자기 판단을 얹게 한다. 잡몹은 종전과 같다.
	virtual void OnHealthChanged(const struct FOnAttributeChangeData& Data);

private:
	// 마지막으로 적용한 걸음. BeginPlay 가 Walk 로 세운다.
	EVBGait CurrentGait = EVBGait::Walk;

	// 일반 사망 시 Instigator에게 명성 패널티 적용
	void ApplyDefaultDeathReputation();
};
