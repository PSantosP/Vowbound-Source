// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Enemy/VBEnemyBase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/AnimNode_PoseSearchHistoryCollector.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "AI/VBEnemyCombatComponent.h"
#include "CharacterCommon/VBHitFlashComponent.h"
#include "CharacterCommon/VBDeathDissolveComponent.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBContextEffectComponent.h"
#include "Animation/VBEnemyLookAtComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "ContextualAnimSceneActorComponent.h"
#include "MotionWarpingComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h" // SettleRagdollVelocity - 바디별 속도
#include "PhysicsEngine/BodySetup.h"   // 바디의 뼈 이름
#include "Animation/AnimInstance.h" // 사망 몽타주 재생
#include "Animation/AnimMontage.h"
#include "Data/VBEnemyConfig.h"
#include "Data/VBDeathDissolveConfig.h" // EnterRagdoll - 디졸브 설정을 컴포넌트에 건넨다
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"          // TickDrivenRagdoll - 바닥 스윕
#include "CollisionQueryParams.h"  // SCENE_QUERY_STAT
#include "Vowbound/Vowbound.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AI/VBAIController.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"


AVBEnemyBase::AVBEnemyBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 틱은 «꺼진 채로 시작» 한다. 켤 수 있게만 두고 실제로는 «몰아지는 래그돌»(A 단계)만 켠다.
	//   bCanEverTick 을 false 로 두면 SetActorTickEnabled(true) 가 조용히 아무 일도 안 한다 -
	//   그 조합이 곧 「코드는 있는데 한 번도 안 도는」 상태다.
	PrimaryActorTick.bCanEverTick          = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// Follow the last completed physics pose BEFORE evaluating the next mesh pose.
	// Moving the capsule after EndPhysics reinterprets finalized component-space
	// bones in a new frame, producing a visible jump without moving physics bodies.
	PrimaryActorTick.TickGroup = TG_PrePhysics;

	// ASC 생성 — replication 은 UVBAbilitySystemComponent 생성자가 자체 설정(SetIsReplicatedByDefault)하므로 여기선 호출 불요
	AbilitySystemComponent = CreateDefaultSubobject<UVBAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	// ASC 기본값은 Mixed(플레이어용)이나, NPC는 Minimal로 다시 재설정한다.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);


	// Attribute 생성
	HealthAttributeSet = CreateDefaultSubobject<UVBHealthAttributeSet>(TEXT("HealthAttributeSet"));
	CombatAttributeSet = CreateDefaultSubobject<UVBCombatAttributeSet>(TEXT("CombatAttributeSet"));

	// 피격 플래시 컴포넌트 — 모든 적이 자동으로 갖는다. BP 배선 누락으로 특정 적만 안 빛나는 사고를 구조적으로 막는다.
	HitFlashComponent = CreateDefaultSubobject<UVBHitFlashComponent>(TEXT("HitFlashComponent"));

	// 사망 디졸브 컴포넌트 — 위와 같은 이유로 C++ 생성. 빠뜨리면 그 적만 시체로 남는다.
	DeathDissolveComponent = CreateDefaultSubobject<UVBDeathDissolveComponent>(TEXT("DeathDissolveComponent"));
	RagdollComponent = CreateDefaultSubobject<UVBRagdollComponent>(TEXT("RagdollComponent"));
	ContextEffectComponent = CreateDefaultSubobject<UVBContextEffectComponent>(TEXT("ContextEffectComponent"));
	LookAtComponent = CreateDefaultSubobject<UVBEnemyLookAtComponent>(TEXT("LookAtComponent"));

	// 전투 조율 컴포넌트 — 위와 같은 이유로 C++ 생성. 이 컴포넌트가 없으면 BTService 가 조용히 아무것도
	// 하지 않아 그 적만 그룹에 합류하지 않는다(로그 없는 결함).
	CombatComponent = CreateDefaultSubobject<UVBEnemyCombatComponent>(TEXT("CombatComponent"));

	// 무기 궤적 판정 - 위와 같은 이유로 C++ 생성. 모양 자산은 적 BP 마다 꽂는다.
	MeleeTraceComponent = CreateDefaultSubobject<UVBMeleeTraceComponent>(TEXT("MeleeTraceComponent"));

	// 처형 장면의 피해자 자리 — 위 둘과 같은 이유로 C++ 생성이다. 여기서는 «BP 배선 누락» 에
	// 「원격 클라에서만 도착이 늦는다」가 하나 더 얹힌다. 근거는 헤더 주석에 있다.
	FinisherSceneComponent =
		CreateDefaultSubobject<UContextualAnimSceneActorComponent>(TEXT("FinisherSceneComponent"));

	// 공격 워프의 소비자. 몽타주의 AnimNotifyState_MotionWarping 이 소유자에서 이 컴포넌트를 찾아 구간을 건다 -
	//   없으면 노티가 조용히 아무것도 안 한다(2026-09-15 실측: 보스 공격이 생 루트모션으로 돌았다).
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));

	// Movement 구조적 설정(튜닝값 아님). 속도/회전율은 BeginPlay 에서 EnemyConfig 적용
	// — 생성자에선 BP 가 할당한 config 가 아직 유효하지 않으므로 CDO 디폴트만 보임.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->bUseControllerDesiredRotation = true;	// AI 회전
	}

	// 네트워크
	bReplicates = true;
}

void AVBEnemyBase::BeginPlay()
{
	Super::BeginPlay();

	// 이동 튜닝값 적용 — BP 가 할당한 EnemyConfig(없으면 CDO 디폴트)에서 읽는다.
	// 속도는 여기서 확정되지 않는다. 걸음은 UVBBTService_UpdateCombat 이 매 주기 정하고
	//   ApplyGait 가 쓴다(design .sw/enemy-gait/design.md §4). 여기서는 시작 걸음만 세운다.
	// 왜 Walk 로 시작하나: 서비스가 한 번도 안 돌면(BT 미배선·컨트롤러 부재) 적은 계속 이 값으로 움직인다.
	//   걷는 적은 눈에 띄되 망가지지 않은 상태고, 뛰는 적은 고장을 정상처럼 보이게 한다.
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->RotationRate = GetEnemyConfig()->RotationRate;
	}
	ApplyGait(EVBGait::Walk);

	// BUG-009: 산 적의 캡슐(Pawn)·메시(CharacterMesh)가 둘 다 ECC_Camera=Block 이라, 소프트런지/접근워프로
	// 근접하면 플레이어 스프링암 프로브(ProbeChannel=ECC_Camera)가 적에 막혀 카메라가 홱 당겨진다.
	// 카메라 채널만 통과시키고 나머지 콜리전(Pawn 밀림/타격 트레이스)은 유지 — 액션게임 표준(카메라는 다른 캐릭터 무시).
	// BUG-008(ad4033c)은 죽은 Ragdoll 메시만 처리했고 산 적은 미처리였음 → 여기서 산 상태부터 일관되게 Ignore.
	// BP 프로파일이 생성자 설정을 덮으므로 런타임(BeginPlay)에서 per-channel override 를 건다.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	// ASC 초기화 (Owner = Avatar = Self)
	// NPC는 PlayerState 없음 -> 자기 자신이 Owner이자 Avatar
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);

		if (HasAuthority())
		{
			// 기본 Ability 부여
			for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
			{
				if (AbilityClass)
				{
					FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);
					AbilitySystemComponent->GiveAbility(Spec);
					VB_LOG(Log, "Enemy GiveAbility: %s", *AbilityClass->GetName());
				}
			}

			// 공격 어빌리티 계약을 여기서 1회 검증한다.
			// 왜 컴포넌트 BeginPlay 가 아닌가: AActor::BeginPlay 가 컴포넌트들의 BeginPlay 를 먼저
			// 돌리고 액터 본문은 그 뒤다. 컴포넌트에서 물으면 위 부여 루프가 아직 안 돌아 항상
			// '없다'로 나와 거짓 경고가 된다. 판단은 컴포넌트가 소유하고 여기는 시점만 정한다.
			if (CombatComponent)
			{
				CombatComponent->ValidateAttackAbilityContract();
			}

			// 초기스탯 GE 적용
			if (InitStatsEffect)
			{
				FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
				Context.AddSourceObject(this);
				FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(
				 InitStatsEffect, 1, Context);

				if (Spec.IsValid())
				{
					AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
					VB_LOG(Log, "Enemy InitStats 적용: %s", *InitStatsEffect->GetName());
				}
			}
		}

		// Health 변경 감지
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		                                                                UVBHealthAttributeSet::GetHealthAttribute()
		                                                               ).AddUObject(this,
			&AVBEnemyBase::OnHealthChanged);
	}
	bDeathVisualReady = true;
	if (ReplicatedDeathState.bDead)
	{
		ApplyDeathState(bPendingLiveDeath);
	}
	else if (ReplicatedRecoverableRagdollState.Phase != EVBRecoverableRagdollPhase::None)
	{
		OnRep_RecoverableRagdollState();
	}
}

void AVBEnemyBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVBEnemyBase, ReplicatedDeathState);
	DOREPLIFETIME(AVBEnemyBase, ReplicatedRecoverableRagdollState);
}

void AVBEnemyBase::OnRep_RecoverableRagdollState()
{
	if (!bDeathVisualReady || ReplicatedDeathState.bDead) return;
	const FVBEnemyRecoverableRagdollState& State = ReplicatedRecoverableRagdollState;
	if (State.Phase == EVBRecoverableRagdollPhase::None)
	{
		ResetRecoverableRagdoll();
		return;
	}
	if (!bRecoverableRagdollActive)
	{
		// A live RepNotify may arrive before its multicast. Preserve the initial
		// impulse only near the server start time; a late relevant client should
		// begin at the replicated capsule instead of replaying old flight.
		const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		const double Age = GameState ? GameState->GetServerWorldTimeSeconds() - State.ServerStartTime : 1.0;
		const bool bRecent = State.Phase == EVBRecoverableRagdollPhase::Ragdoll && GameState &&
			Age >= -0.05 && Age < 0.25;
		ApplyRecoverableRagdoll(bRecent ? FVector(State.LaunchVelocity) : FVector::ZeroVector,
			State.GetUpMontage);
	}
	if (State.Phase == EVBRecoverableRagdollPhase::GettingUp && bDrivenRagdollActive)
	{
		ApplyRecoverableGetUp();
	}
}

UAbilitySystemComponent* AVBEnemyBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

FGenericTeamId AVBEnemyBase::GetGenericTeamId() const
{
	return VBTeam::ToGenericId(GetEnemyConfig()->Team);
}

const UVBEnemyConfig* AVBEnemyBase::GetEnemyConfig() const
{
	// 할당된 config 우선, 없으면 클래스 CDO 디폴트(절대 null 아님).
	return EnemyConfig ? EnemyConfig : GetDefault<UVBEnemyConfig>();
}

void AVBEnemyBase::PreviewDeathDissolve()
{
	// 여기서 null 검사를 하지 않는다. 「무엇을 쓸 것인가」의 판단은 컴포넌트 한 곳에 있고,
	//   여기에 또 두면 한쪽만 폴백을 갖게 된다 - 2026-09-20 에 실제로 그래서 보스 트롤이 안 됐다.
	UVBDeathDissolveConfig* Dissolve = UVBDeathDissolveComponent::ResolveConfig(GetEnemyConfig()->DeathDissolve.Get());
	if (!Dissolve || Dissolve->Duration <= 0.0f)
	{
		VB_LOG(Warning, "사망 디졸브 시험: %s 에 쓸 설정이 없다 - 적 설정과 프로젝트 기본(Project Settings > Game > Vowbound Death Dissolve)이 둘 다 비었다.",
		       *GetName());
		return;
	}

	if (!DeathDissolveComponent)
	{
		VB_LOG(Warning, "사망 디졸브 시험: %s 에 디졸브 컴포넌트가 없다.", *GetName());
		return;
	}

	// 정리까지 남은 시간으로 «디졸브 길이 그대로» 를 넘긴다. 그러면 시작 지연이 0 이 되어 즉시 시작한다.
	//   이 컴포넌트는 애초에 Destroy 를 부르지 않으므로(사라지는 시각의 주인을 늘리지 않으려는 설계다)
	//   액터는 그대로 남고, 되돌리기 버튼으로 몇 번이든 다시 볼 수 있다.
	// 앞선 시험이 남아 있을 수 있으므로 먼저 지운다 - 안 지우면 이전 시작 시각이 섞여 진행률이 튄다.
	DeathDissolveComponent->ResetDissolve();
	DeathDissolveComponent->ScheduleDissolve(Dissolve->Duration, Dissolve);
}

void AVBEnemyBase::ResetDeathDissolvePreview()
{
	if (DeathDissolveComponent)
	{
		DeathDissolveComponent->ResetDissolve();
	}
}

// 걸음을 바꾼다. 속도를 쓰는 유일한 지점이다 - design .sw/enemy-gait/design.md §4.
void AVBEnemyBase::ApplyGait(EVBGait NewGait)
{
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	const UVBEnemyConfig* Cfg = GetEnemyConfig();
	// Sprint 는 적에게 없다. 들어오면 Run 으로 접는다 - 조용히 무시하면 «왜 안 빨라지지» 가 되고
	//   따로 값을 만들면 아무도 저작하지 않는 세 번째 속도가 생긴다.
	const float BaseSpeed = (NewGait == EVBGait::Walk) ? Cfg->WalkSpeed : Cfg->RunSpeed;

	// 자산의 속도는 «애니 고유» 단위다(GetStrideScale 의 규약). 메시가 큰 적은 같은 클립이 더 넓은
	//   보폭을 그리므로 지면도 그만큼 빨리 가야 발이 맞는다. 배율 1 인 적은 곱하기 1 이라 무영향.
	const float NewSpeed = BaseSpeed * GetStrideScale();

	// 값이 실제로 바뀔 때만 로그. 매 주기 찍으면 그룹당 적 수만큼 쌓여 정작 전환이 묻힌다
	//   (코디네이터의 역할 전환 로그와 같은 규칙).
	VB_CLOG(NewGait != CurrentGait, Log, "걸음 전환: %s %s -> %s (속도 %.0f)",
	        *GetName(),
	        *UEnum::GetValueAsString(CurrentGait), *UEnum::GetValueAsString(NewGait), NewSpeed);

	CurrentGait           = NewGait;
	MoveComp->MaxWalkSpeed = NewSpeed;
}

float AVBEnemyBase::GetStrideScale() const
{
	const USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return 1.0f;
	}
	const float ScaleX = static_cast<float>(MeshComp->GetComponentScale().X);
	// 0 이나 음수 배율은 나눗셈을 폭발시킨다. 그런 메시는 화면에서 이미 망가져 있으므로
	//   여기서는 «속도만이라도 정상» 으로 떨어뜨린다.
	return (ScaleX > KINDA_SMALL_NUMBER) ? ScaleX : 1.0f;
}

float AVBEnemyBase::ResolveStrideScale(const AActor* Actor)
{
	const AVBEnemyBase* Enemy = Cast<AVBEnemyBase>(Actor);
	return Enemy ? Enemy->GetStrideScale() : 1.0f;
}

FGameplayTag AVBEnemyBase::GetCurrentAttackInputTag() const
{
	// GetEnemyConfig() 는 미할당이면 CDO 를 돌려주므로 null 이 아니다. CDO 의 태그는 비어 있고, 그 부재는
	//   부팅 시 ValidateAttackAbilityContract 가 1회 경고한다(여기서 경고하면 0.5초마다 반복된다).
	return GetEnemyConfig()->AttackAbilityInputTag;
}

void AVBEnemyBase::OnHealthChanged(const struct FOnAttributeChangeData& Data)
{
	if (Data.NewValue <= 0.0f && !IsDead())
	{
		VB_LOG(Warning, "Enemy 사망! %s (Health: %1.f -> %1.f)",
		       *GetName(), Data.OldValue, Data.NewValue);
		// 서버에서만 HandleDeath 호출. 래그돌/콜리전은 Multicast로 클라에 전파됨 (타이밍 동기화).
		if (HasAuthority())
		{
			HandleDeath();
		}
	}
}

void AVBEnemyBase::ApplyDefaultDeathReputation()
{
	// 게이트가 말을 하게 둔다 (2026-08-19).
	// 종전엔 아래 넷이 전부 조용히 return 했다. 그 결과 "적을 죽였는데 명성이 안 깎인다"가
	//   원인 후보 넷을 구분할 방법 없이 화면에서만 관측됐다(user 실측). 명성은 값에 영향을 주는
	//   경로이므로 빠지는 이유가 흔적을 남겨야 한다 - 정상 분기(의료)는 Log, 나머지는 Warning 이다.

	if (!HealthAttributeSet)
	{
		VB_LOG(Warning, "%s: HealthAttributeSet 이 없어 사망 명성을 건너뛴다", *GetName());
		return;
	}

	// 의료 행위(정화/처치)는 GA에서 이미 명성 처리 -> 건너 뜀. 이건 정상 분기라 Warning 이 아니다.
	if (HealthAttributeSet->bLastDamageWasMedical)
	{
		VB_LOG(Log, "%s: 의료 사망이라 일반 사망 명성을 건너뛴다(GA 가 이미 처리)", *GetName());
		return;
	}

	// Instigator 확인
	AActor* DamageInstigator = HealthAttributeSet->LastDamageInstigator.Get();
	if (!DamageInstigator)
	{
		VB_LOG(Warning, "%s: 누가 죽였는지 기록이 없어 명성을 못 준다(LastDamageInstigator=null). "
		                "사망 컨텍스트 저장이 이 호출보다 늦게 도는지 확인하라", *GetName());
		return;
	}

	// Instigator의 ASC확인
	UAbilitySystemComponent* InstigatorASC =
			UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(DamageInstigator);

	if (!InstigatorASC)
	{
		VB_LOG(Warning, "%s: 가해자 %s 에게 ASC 가 없어 명성을 못 준다", *GetName(), *DamageInstigator->GetName());
		return;
	}

	// ReputationAttributeSet 확인 (플레이어만 보유)
	// 적끼리 죽여도 명성 변동이 없어야 함 - 그 경우는 정상이라 Log 다.
	if (!InstigatorASC->GetSet<UVBReputationAttributeSet>())
	{
		VB_LOG(Log, "%s: 가해자 %s 가 명성 세트를 갖지 않아 건너뛴다(적끼리 죽인 경우 정상)",
		       *GetName(), *DamageInstigator->GetName());
		return;
	}


	// 명성 GE 적용 (SetByCaller 패턴)
	if (!DefaultDeathReputationEffect)
	{
		VB_LOG(Warning, "Default Death Reputation Effect 미설정 : %s", *GetName());
		return;
	}

	FGameplayEffectContextHandle Context = InstigatorASC->MakeEffectContext();
	FGameplayEffectSpecHandle    Spec    = InstigatorASC->MakeOutgoingSpec(
	                                                                       DefaultDeathReputationEffect, 1, Context);

	if (Spec.IsValid())
	{
		const float ReputationChange = GetEnemyConfig()->DeathReputationChange;
		Spec.Data.Get()->SetSetByCallerMagnitude(
		                                         VBGameplayTags::Data_ReputationChange, ReputationChange);
		InstigatorASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

		VB_LOG(Log, "%s 일반 사망 -> %s 명성 %.0f",
		       *GetName(), *DamageInstigator->GetName(), ReputationChange);
	}
}

bool AVBEnemyBase::IsTargetable() const
{
	return !IsDead();
}

bool AVBEnemyBase::IsDead() const
{
	// 판정은 UVBAbilitySystemStatics 가 소유한다 - 여기는 "어느 ASC 인가"만 정한다.
	return ReplicatedDeathState.bDead || UVBAbilitySystemStatics::IsDead(AbilitySystemComponent);
}

FVector AVBEnemyBase::GetTargetMarkerLocation() const
{
	// 락온 카메라가 조준할 '올린 지점'(가슴 높이). 왜: 액터 위치(캡슐 중심)를 조준하면 근접 시 pitch가
	// 탑다운으로 처박힘 → 가슴 본을 조준해 수직차를 줄여 pitch 완만화. 부작용: 없음(순수 읽기, const).
	// ZeroVector 반환 절대 금지. 본/소켓 부재 시 반드시 actor+Z 폴백(원점 조준 방지).
	if (const USkeletalMeshComponent* MeshComp = GetMesh())
	{
		// DoesSocketExist = 소켓+본 둘 다 검사. 없는 본에 GetSocketLocation 하면 엔진이 조용히
		// 컴포넌트(발) 위치로 폴백하므로, 반드시 존재 확인 후에만 소켓 위치를 신뢰한다.
		if (MeshComp->DoesSocketExist(MarkerBoneName))
		{
			return MeshComp->GetSocketLocation(MarkerBoneName); // 스켈레톤별 자동 스케일(가슴)
		}
	}
	// 폴백: 캡슐 중심 기준 Z 상승(가슴 높이대). 절대 ZeroVector 아님.
	return GetActorLocation() + FVector(0.0f, 0.0f, MarkerFallbackHeight);
}

void AVBEnemyBase::HandleDeath()
{
	if (ReplicatedDeathState.bDead)
	{
		return;
	}
	// 0. 일반 사망 명성 처리 (의료 행위가 아닌 경우만)
	ApplyDefaultDeathReputation();

	// 엎드린 채(그로기) 죽었나 - CancelAllAbilities 가 UVBGA_Knockdown 을 끝내며 Downed 태그를 걷으므로 «그 앞에서» 읽는다.
	//   소비자는 아래 사망 몽타주 선택이다(design .sw/boss/design.md §14.3): 서 있는 사망 클립을 엎드린 자세에서 틀면 몸이 튄다(§13.8 의 모양).
	const bool bDiedDowned = UVBAbilitySystemStatics::IsDowned(AbilitySystemComponent);

	// 1. State.Dead 태그 부여
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Dead, 1, EGameplayTagReplicationState::CountToOwner);
		AbilitySystemComponent->CancelAllAbilities();
	}

	// 1-a. 처치 사건(.sw/narrative 12.4) - 퀘스트의 «n 처치» 목표가 듣는다. 플레이어가 죽였을 때만 보낸다:
	//   사건의 «일으킨 사람이 없음» 은 세계 사건이라 모든 플레이어의 퀘스트가 센다 - 적끼리 죽인 것이 내 퀘스트에 세이면 안 된다.
	//   가해자는 효과 컨텍스트의 Instigator = ASC 소유자라 플레이어면 PlayerState 다(명성 처리와 같은 값). 처형·정화로 죽어도 처치다.
	if (APlayerState* KillerState = Cast<APlayerState>(HealthAttributeSet ? HealthAttributeSet->LastDamageInstigator.Get() : nullptr))
	{
		if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
		{
			FVBGameEvent Event;
			Event.EventTag = VBGameplayTags::Event_Enemy_Killed;
			Event.TargetTags = GetEnemyConfig()->EnemyTags;
			Event.Instigator = KillerState;
			Events->Broadcast(Event);
		}

		// 처치 효과(적 드롭 등, .sw/narrative 14.5)는 사건 «뒤» - 상호작용 완료와 같은 순서 규칙(«n 처치 -> 그 전리품을 가져가라» 의 둘째 단계가 먼저 서야 한다).
		const TArray<FInstancedStruct>& KilledEffects = GetEnemyConfig()->OnKilledEffects;
		if (!KilledEffects.IsEmpty())
		{
			FVBRuleContext Context = FVBRuleContext::Make(this, KillerState);
			Context.Counterpart = this;
			VBRules::ApplyAll(KilledEffects, Context);
		}
	}

	// 1-b. 전투 그룹에서 즉시 빠진다. 시체가 그룹에 남으면 토큰과 링 슬롯을 계속 점유해
	//   "한 마리를 죽였는데 대기하던 적이 안 붙는다"가 된다. 코디네이터의 주기 정리(0.5s)를 기다리지 않고
	//   여기서 끊는 이유는 그 0.5s 가 정확히 교전이 뒤집히는 순간이기 때문이다.
	//   CancelAllAbilities 뒤에 두는 것도 의도다 - 공격 GA 취소가 먼저 State.Combat.Attacking 을 지워
	//   정상 반납 경로가 돌고, 그 다음에 등록 자체가 사라진다.
	if (CombatComponent)
	{
		CombatComponent->LeaveCombat();
	}

	// 2. 죽은 나를 노리던 AI 의 타겟을 끊는다. 플레이어 사망 경로(AVBCharacter::HandleDeath)와 대칭이다 -
	//    적 사망에는 이 통지가 없어서, 적끼리 싸울 때 죽은 상대를 액터 파괴 시점(DeathCleanupDelay)까지
	//    계속 쫓으며 때리는 시늉을 했다(2026-08-14 user 관찰). 지각의 사망 게이트는 재획득만 막을 뿐
	//    이미 블랙보드에 든 타겟은 비우지 못한다.
	//    주의: 현재 적 BP 는 BP_EnemyInfected 하나뿐이고 전부 같은 팀이라 적끼리 적대가 성립하지 않는다.
	//    즉 이 줄은 아직 실행 경로에 도달하지 않는다 - 로그가 비어 있다고 미사용으로 판단해 지우지 말 것.
	//    같은 메커니즘은 플레이어 사망 경로에서 실측 확인됐다("사망 통지: ... AI n 기의 타겟 해제").
	//    다른 Team 값을 가진 UVBEnemyConfig 를 쓰는 적을 하나 만들면 그 순간 관측 가능해진다.
	AVBAIController::NotifyActorDied(this, this);

	// 몽타주 선택도 서버에서 한 번만 한다. 늦은 수신자가 다른 사망 연출을 뽑지 않는다.
	ReplicatedDeathState.bDead = true;
	ReplicatedDeathState.bInteractionDeath = bRagdollKeepsMomentum;
	ReplicatedDeathState.LaunchVelocity = RagdollLaunchVelocity;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	ReplicatedDeathState.ServerDeathTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const UVBEnemyConfig* Config = GetEnemyConfig();
	if (bDiedDowned)
	{
		VB_LOG(Log, "%s 엎드린 채 사망 - 사망 몽타주 생략, 그 자세에서 래그돌", *GetName());
	}
	if (!bRagdollKeepsMomentum && !bDiedDowned && !Config->DeathMontages.IsEmpty())
	{
		ReplicatedDeathState.DeathMontage = Config->DeathMontages[FMath::RandRange(0, Config->DeathMontages.Num() - 1)];
		if (const UAnimMontage* Montage = ReplicatedDeathState.DeathMontage; Montage && Montage->RateScale > KINDA_SMALL_NUMBER)
		{
			ReplicatedDeathState.bHasDeathMontage = true;
			ReplicatedDeathState.RagdollDelay = FMath::Max(0.05f,
				Montage->GetPlayLength() / Montage->RateScale - Montage->GetBlendOutArgs().BlendTime);
		}
	}
	ForceNetUpdate();
	MulticastHandleDeathCosmetic(ReplicatedDeathState);
}

void AVBEnemyBase::MulticastHandleDeathCosmetic_Implementation(const FVBEnemyDeathState& State)
{
	if (!ReplicatedDeathState.bDead)
	{
		ReplicatedDeathState = State;
	}
	// 먼저 도착한 RepNotify의 더 진행된 ragdoll 단계를 이전 RPC로 되돌리지 않는다.
	ApplyDeathState(true);
}

void AVBEnemyBase::OnRep_DeathState()
{
	if (!ReplicatedDeathState.bDead || !bDeathVisualReady)
	{
		return;
	}
	// 동일 네트워크 갱신의 빠른 multicast가 먼저 실행될 기회를 준다. 복구는 발사를 재연하지 않는다.
	GetWorldTimerManager().ClearTimer(DeathStateReplayTimer);
	DeathStateReplayTimer = GetWorldTimerManager().SetTimerForNextTick(this, &AVBEnemyBase::ReplayDeathState);
}

void AVBEnemyBase::ReplayDeathState()
{
	ApplyDeathState(false);
}

void AVBEnemyBase::ApplyDeathState(bool bFromLiveEvent)
{
	if (!ReplicatedDeathState.bDead)
	{
		return;
	}
	if (!bDeathVisualReady)
	{
		bPendingLiveDeath |= bFromLiveEvent;
		return;
	}
	if (bDeathCosmeticStarted)
	{
		if (ReplicatedDeathState.bRagdoll && !bEnteredRagdoll)
		{
			EnterRagdoll();
		}
		return;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Elapsed = FMath::Max(0.0, ServerNow - ReplicatedDeathState.ServerDeathTime);
	if (ReplicatedDeathState.bHasDeathMontage && !ReplicatedDeathState.DeathMontage &&
	    !ReplicatedDeathState.bRagdoll && Elapsed < ReplicatedDeathState.RagdollDelay)
	{
		GetWorldTimerManager().SetTimer(DeathStateReplayTimer, this, &AVBEnemyBase::ReplayDeathState, 0.05f, false);
		return;
	}
	GetWorldTimerManager().ClearTimer(DeathStateReplayTimer);
	bDeathCosmeticStarted = true;
	bRagdollKeepsMomentum = ReplicatedDeathState.bInteractionDeath;
	bSuppressRagdollLaunch = !bFromLiveEvent || (!HasAuthority() && ReplicatedDeathState.bRagdoll);
	RagdollLaunchVelocity = bSuppressRagdollLaunch ? FVector::ZeroVector : FVector(ReplicatedDeathState.LaunchVelocity);
	DeathCosmeticStartTime = GetWorld()->GetTimeSeconds() - Elapsed;
	// 원격 클라에 State_Dead 를 로컬 부여한다. FindBestTarget 이 시체를 조준하는 것을 막는 타겟팅 필터 정합용.
	// 정정(2026-07-29): 이 보완의 원래 근거였던 "CountToOwner 는 소유 연결 없는 AI라 미복제"는 사실이 아니다.
	//   엔진 정의는 "Tag will replicate to all and Count will replicate to owner"(GameplayEffectTypes.h::EGameplayTagReplicationState::CountToOwner)이고
	//   직렬화도 비-소유 연결에 항목은 보내고 Count 만 1 로 대체한다(GameplayEffectTypes.cpp::FMinimalReplicationTagCountMap::NetSerialize).
	//   HasMatchingGameplayTag 는 Count>0 판정이므로 태그는 어차피 전원에게 간다.
	// 따라서 이 블록은 UE5.7 기준 중복이다. 남겨 두는 이유는 무해하고(!HasAuthority 가드), 복제 도달 전
	//   한두 틱의 창을 즉시 메워 주기 때문이다. 지울 거라면 그 타이밍 창을 먼저 확인할 것.
	// 같은 이유로 플레이어(AVBCharacter)에는 이 보완이 없다 — "owning connection 유무" 때문이 아니다.
	if (!HasAuthority() && AbilitySystemComponent && !AbilitySystemComponent->HasMatchingGameplayTag(VBGameplayTags::State_Dead))
	{
		AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Dead, 1, EGameplayTagReplicationState::CountToOwner);
	}
	HandleDeathCosmetic(Elapsed);
}

void AVBEnemyBase::HandleDeathCosmetic(float ElapsedSinceDeath)
{
	// 2. Collision 비활성화
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 3. Movement 정지
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}

	// 4. 피격 플래시 해제 — 반드시 래그돌 전환 '앞'에 둔다.
	// SetOverlayMaterial 은 MarkRenderStateDirty 로 스켈레탈 렌더 상태를 재생성하는데,
	// 물리 시뮬레이션을 켠 뒤에 부르면 그 재생성이 시뮬 시작 프레임과 겹친다.
	// 해제하지 않으면 죽는 타격의 플래시가 시체에 그대로 얼어붙는다.
	if (HitFlashComponent)
	{
		HitFlashComponent->ClearFlash();
	}

	// 5. 사망 연출 몽타주가 있으면 그것을 먼저 튼다(design .sw/boss/design.md §13.3).
	//   없으면(잡몹 셋) 아래 EnterRagdoll 이 곧바로 돌아 종전 동작 그대로다.
	//   몽타주를 재생하려면 메시가 아직 애니를 돌려야 하므로 래그돌은 끝난 뒤로 미룬다.
	UAnimInstance* AnimInst = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	UAnimMontage* DeathMontage = ReplicatedDeathState.DeathMontage;
	const float RemainingUntilRagdoll = ReplicatedDeathState.RagdollDelay - ElapsedSinceDeath;
	// 이미 끝난 연출은 처음부터 재생하지 않는다. 진행 중이면 서버 시각에 해당하는 위치부터 시작한다.
	if (!bRagdollKeepsMomentum && !ReplicatedDeathState.bRagdoll && DeathMontage && AnimInst &&
	    RemainingUntilRagdoll > 0.0f && DeathMontage->RateScale > KINDA_SMALL_NUMBER &&
	    AnimInst->Montage_Play(DeathMontage, 1.0f, EMontagePlayReturnType::MontageLength,
	                          ElapsedSinceDeath * DeathMontage->RateScale) > 0.0f)
	{
		VB_LOG(Log, "사망 연출: %s 몽타주 %s (서버 경과 %.2f초, 래그돌까지 %.2f초)",
		       *GetName(), *DeathMontage->GetName(), ElapsedSinceDeath, RemainingUntilRagdoll);
		FOnMontageEnded Ended;
		Ended.BindUObject(this, &AVBEnemyBase::OnDeathMontageEnded);
		AnimInst->Montage_SetEndDelegate(Ended, DeathMontage);
		GetWorldTimerManager().SetTimer(DeathRagdollTimer, this, &AVBEnemyBase::EnterRagdoll,
		                               RemainingUntilRagdoll, false);
		return;
	}

	EnterRagdoll();
}

void AVBEnemyBase::OnDeathMontageEnded(UAnimMontage* /*Montage*/, bool /*bInterrupted*/)
{
	// 끊겼든 끝까지 갔든 물리로 넘긴다 - 끊긴 자세에서 멈춰 서 있는 것이 더 나쁘다.
	EnterRagdoll();
}

bool AVBEnemyBase::ShouldPreserveRagdollMomentum() const
{
	const UVBEnemyConfig* Config = GetEnemyConfig();
	return !bSuppressRagdollLaunch && (bRagdollKeepsMomentum || (Config && Config->bPreserveRagdollMomentum));
}

void AVBEnemyBase::EnterRagdoll()
{
	// Death wins over a temporary, surviving-victim ragdoll on every net role.
	// 다만 몸이 이미 물리로 누워 있으면(쓰러진 채 죽음) 다시 만들지 않는다. ResetRecoverableRagdoll 은 죽은 몸의 물리를 끄므로
	//   몸이 애니 자세로 한 번 섰다가 다시 쓰러진다. 몸은 그대로 두고 «살아날 래그돌» 표지만 내려 사망 쪽으로 넘긴다(설계 .sw/downed-hit 2절 7).
	//   기상 중(물리를 애니로 넘긴 뒤)이면 종전대로 Reset 하고 새로 쓰러뜨린다 - 그때 몸은 애니가 들고 있다.
	const bool bKeepDownedBody = bRecoverableRagdollActive && bDrivenRagdollActive && !bEnteredRagdoll;
	if (bKeepDownedBody)
	{
		bRecoverableRagdollActive = false;
		RecoverableGetUpMontage = nullptr;
	}
	else if (bRecoverableRagdollActive)
	{
		ResetRecoverableRagdoll();
	}
	if (HasAuthority() && ReplicatedRecoverableRagdollState.Phase != EVBRecoverableRagdollPhase::None)
	{
		ReplicatedRecoverableRagdollState.Phase = EVBRecoverableRagdollPhase::None;
		ForceNetUpdate();
	}
	if (bEnteredRagdoll)
	{
		return;
	}
	bEnteredRagdoll = true;
	GetWorldTimerManager().ClearTimer(DeathRagdollTimer);
	if (HasAuthority() && ReplicatedDeathState.bDead)
	{
		ReplicatedDeathState.bRagdoll = true;
		ForceNetUpdate();
	}

	// 사망 연출에 이미 쓴 시간. 정리 지연은 «죽은 순간부터» 재는 값이라 몽타주에 쓴 만큼 빼야 한다.
	const double Now               = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double ElapsedSinceDeath = bDeathCosmeticStarted ? FMath::Max(0.0, Now - DeathCosmeticStartTime) : 0.0;

	if (bKeepDownedBody)
	{
		// 몸은 이미 «Ragdoll» 프로필로 시뮬레이션 중이다. 칼 판정 응답만 끈다 - 시체는 안 맞는다(ApplyRecoverableRagdoll 이 켰던 것).
		//   캡슐은 HandleDeathCosmetic 이 꺼 뒀으니 몰아지는 래그돌의 값으로 되돌린다. A 단계는 계속 돌고, 멎음·상한은 죽은 때부터 다시 잰다.
		GetMesh()->SetCollisionResponseToChannel(VBCollision::MeleeTrace, ECR_Ignore);
		ConfigureDrivenRagdollCapsule();
		DrivenRagdollStartTime = Now;
		DrivenRagdollSettledFrames = 0;
		VB_LOG(Log, "래그돌 진입: %s 쓰러진 채 사망 - 누운 몸을 그대로 시체로 넘긴다", *GetName());
		ScheduleDeathCleanup(ElapsedSinceDeath);
		return;
	}

	// 래그돌 전환
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	// 래그돌 메쉬가 카메라 프로브 채널만 통과하게 한다. 엔진 "Ragdoll" 프로파일은 Pawn/Visibility 만 Ignore 하고
	// Camera 는 기본 Block 이라, 플레이어 스프링암(bDoCollisionTest, ProbeChannel=ECC_Camera)이 쓰러진 시체에 충돌해
	// 카메라를 캐릭터 쪽으로 확 당긴다(급줌인). 반드시 SetCollisionProfileName 뒤에 둘 것 — 프로파일 설정이 채널 응답을 리셋한다.
	// 물리(QueryAndPhysics/시뮬레이션/월드 충돌)는 그대로 유지 — 오직 카메라 프로브만 통과.
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 어느 바디를 시뮬레이션하나. 설정의 RagdollRootBone 이 있으면 그 뼈와 아래만 - 뿌리 뼈의 바디는 키네마틱으로 남긴다.
	//   SetSimulatePhysics(true) 는 PhysType_Default 바디 전부에 다시 켜므로(엔진 SkeletalMeshComponentPhysics.cpp::
	//   USkeletalMeshComponent::SetSimulatePhysics) 그 길에서는 부르지 않는다 - 부르면 뿌리 바디가 되살아난다.
	//   bBlendPhysics 는 그 함수가 세우던 것이라 여기서 직접 세운다.
	const FName RagdollRoot = GetEnemyConfig() ? GetEnemyConfig()->RagdollRootBone : NAME_None;
	if (!RagdollRoot.IsNone() && GetMesh()->GetBoneIndex(RagdollRoot) != INDEX_NONE)
	{
		GetMesh()->SetAllBodiesBelowSimulatePhysics(RagdollRoot, true, /*bIncludeSelf*/ true);
		GetMesh()->bBlendPhysics = true;

		// 뿌리 뼈를 그 위(키네마틱으로 남는 잔여 바디)에서 떼어낸다. 구속을 두면 시뮬레이션하는 골반이 부모 바디의
		//   참조 위치 - 서 있을 때의 높이 - 로 끌려 올라가 시체가 공중에 걸린다(2026-09-18 실측: 다음 틱 6,132cm/s 가 spine_01,
		//   그 뒤 공중 정지). 충격량 0 으로 부르면 끊기만 한다.
		GetMesh()->BreakConstraint(FVector::ZeroVector, FVector::ZeroVector, RagdollRoot);

		// 잔여 키네마틱 바디의 충돌은 건드리지 않는다. 바디 하나라도 SetCollisionEnabled 로 물리 유무가 바뀌면 엔진이
		//   RecreateInstanceBody -> RecreatePhysicsState 로 바디 전부를 새로 만든다(PrimitiveComponent.cpp::
		//   UPrimitiveComponent::RecreateInstanceBody). 그러면 시뮬레이션 플래그가 전부 사라지고 끊은 구속이 되살아나
		//   래그돌이 없던 일이 된다 - 2026-09-18 실측: 다음 틱 최대 속도 0(None), 시체가 서 있는 자세로 돌아간 뒤 정리됨.
		//   게다가 순회 중인 Bodies 배열 자체가 교체되므로 그 루프는 해제된 포인터를 밟는다.
		//   그 바디가 발바닥 높이라 발이 얹힐 수 있는 문제는 자산에서 바디를 지우는 것으로 푼다(design §13.11 의 2).
		int32 RagdollBodyCount = 0;
		GetMesh()->ForEachBodyBelow(RagdollRoot, /*bIncludeSelf*/ true, /*bSkipCustomType*/ false,
		                            [&RagdollBodyCount](FBodyInstance*) { ++RagdollBodyCount; });
		VB_LOG(Log, "래그돌 범위: %s '%s' 아래 %d 바디 시뮬레이션, 그 밖 %d 바디는 키네마틱(충돌 유지)",
		       *GetName(), *RagdollRoot.ToString(), RagdollBodyCount, GetMesh()->Bodies.Num() - RagdollBodyCount);
	}
	else
	{
		if (!RagdollRoot.IsNone())
		{
			VB_LOG(Warning, "%s: RagdollRootBone '%s' 가 골격에 없다 - 바디 전부에 건다. 설정 자산의 뼈 이름을 확인하라.",
			       *GetName(), *RagdollRoot.ToString());
		}
		GetMesh()->SetAllBodiesSimulatePhysics(true);
		GetMesh()->SetSimulatePhysics(true);
	}
	GetMesh()->WakeAllRigidBodies();

	// 던져진 래그돌이면 클립이 정한 속도로 «설정» 한다. 더하지 않는 이유는 필드 주석에 있다.
	// 왜 여기인가: 시뮬레이션을 켜는 호출들이 바디마다 속도를 다시 심으므로 그 «뒤» 여야 남는다
	//   (SettleRagdollVelocity 주석이 같은 이유로 지우기를 뒤로 미룬다).
	// 곁들여 그 순간의 CMC 속도도 찍는다 - 「래그돌이 물려받을 운동이 애초에 없다」가 추측이 아니라
	//   사실인지 이 줄이 답한다. 이것이 크면 원인이 다른 데 있다는 뜻이다.
	if (bRagdollKeepsMomentum)
	{
		const FVector CmcVelocity =
			GetCharacterMovement() ? GetCharacterMovement()->Velocity : FVector::ZeroVector;
		if (!RagdollLaunchVelocity.IsNearlyZero())
		{
			GetMesh()->SetAllPhysicsLinearVelocity(RagdollLaunchVelocity);
		}
		VB_LOG(Log, "래그돌 발사: %s 클립 속도 %.0f cm/s %s (그 순간 CMC 속도 %.0f cm/s)",
		       *GetName(), RagdollLaunchVelocity.Size(),
		       *RagdollLaunchVelocity.ToCompactString(), CmcVelocity.Size());
	}

	// 속도 지우기는 «켜는 호출을 전부 끝낸 뒤» 여야 한다.
	//   시뮬레이션을 켜면 바디가 직전 애니 프레임의 운동을 초기 속도로 물려받는데, 그 심는 일을 하는 것이
	//   SetInstanceSimulatePhysics 이고 위 세 호출이 그것을 각각 한 번씩 돌린다
	//   (엔진 SkeletalMeshComponentPhysics.cpp::USkeletalMeshComponent::SetSimulatePhysics 가 바디마다 다시 부른다).
	//   그래서 중간에 지우면 뒤 호출이 다시 심는다 - 2026-09-17 에 그 자리에 두었다가 시체가 더 튀었다.
	// 왜 지우나: 사망 클립의 마지막 자세는 이미 바닥이라 남길 운동량이 없고, 남기면 그것이 곧 튀어나가는 힘이다.
	//   날려야 한다면 그건 넉백의 몫이지 사망 연출의 몫이 아니다.
	if (!ShouldPreserveRagdollMomentum())
	{
		GetMesh()->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
		GetMesh()->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	}
	VB_LOG(Log, "래그돌 진입: %s (경과 %.2f초, 운동량 %s)",
	       *GetName(), ElapsedSinceDeath,
	       ShouldPreserveRagdollMomentum() ? TEXT("남긴다") : TEXT("지운다"));

	// 지우기를 다음 틱에 한 번 더 한다. 켠 프레임에 읽은 속도는 언제나 0 이라 이 자리의 지우기만으로는
	//   «지웠다» 를 증명하지도, 실제로 막지도 못한다(2026-09-18 실측: 진입 시점 0 인데 시체는 튀었다).
	GetWorldTimerManager().SetTimerForNextTick(this, &AVBEnemyBase::SettleRagdollVelocity);
	FTimerHandle SettleReportHandle;
	GetWorldTimerManager().SetTimer(SettleReportHandle, this, &AVBEnemyBase::SettleRagdollVelocity, 0.3f, false);

	// A 단계 시작. 반드시 시뮬레이션을 켜고 «초기 속도까지 심은 뒤» 여야 한다 -
	//   첫 틱이 쫓아갈 목표가 이미 움직이고 있어야 첫 프레임에 캡슐이 뒤처지지 않는다.
	BeginDrivenRagdoll();

	ScheduleDeathCleanup(ElapsedSinceDeath);
}

void AVBEnemyBase::ScheduleDeathCleanup(double ElapsedSinceDeath)
{
	// 6. DeathCleanupDelay 뒤 제거. 사망 몽타주가 있으면 그 길이만큼 이미 흘렀으므로 남은 시간으로 잡는다 -
	//   전체를 다시 세면 시체가 자산에 적은 것보다 훨씬 오래 남는다.
	//   IsDataValid 가 지연 >= 몽타주 길이 + 디졸브 길이를 보장한다 - 2026-09-23 에 그 보장이
	//   실제로 서도록 고쳤다. 종전에는 검증기가 DeathDissolve 를 날것으로 읽어서, 그 필드가 비어
	//   프로젝트 기본값이 걸리는 «흔한 경우» 를 0 으로 세고 통과시켰다. 즉 이 줄이 주장하는 조건을
	//   정작 아무도 안 재고 있었다. 안전망이 조용한 것보다 거짓말하는 쪽이 나쁘다.
	// 이 뺄셈의 주인을 여기 하나로 둔다. 디졸브도 같은 값에서 시작을 역산하므로, 각자 다시 계산하면
	//   두 식이 어긋나는 날 시체가 다 사라지기 전에 없어지거나 투명한 채로 남는다.
	const float SecondsUntilCleanup =
		FMath::Max(0.1f, GetEnemyConfig()->DeathCleanupDelay - static_cast<float>(ElapsedSinceDeath));

	// 7. 사라지는 연출을 정리 시각에 맞춰 예약한다.
	//   권한을 가리지 않는 이유: 이것은 코스메틱이고, 클라에서도 각자 돌아야 시체가 같은 시각에 사라진다.
	//   이 함수는 몽타주가 있으면 서버는 HandleDeathCosmetic 의 타이머로, 클라는 몽타주 종료 델리게이트로
	//   도달하고, 몽타주가 없으면 양쪽 다 즉시 도달한다. 어느 경로든 한 번만 돈다(bEnteredRagdoll 래치).
	if (DeathDissolveComponent)
	{
		DeathDissolveComponent->ScheduleDissolve(SecondsUntilCleanup, GetEnemyConfig()->DeathDissolve.Get());
	}

	if (HasAuthority())
	{
		FTimerHandle DestroyTimerHandle;
		GetWorldTimerManager().SetTimer(
										DestroyTimerHandle,
										this,
										&AVBEnemyBase::DestroyAfterDeath,
										SecondsUntilCleanup,
										false
									   );
	}
}

void AVBEnemyBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bDrivenRagdollActive)
	{
		TickDrivenRagdoll(DeltaSeconds);
	}

}

bool AVBEnemyBase::StartRecoverableRagdoll(const FVector& LaunchVelocity, UAnimMontage* GetUpMontage)
{
	const UVBEnemyConfig* Config = GetEnemyConfig();
	if (!HasAuthority() || IsDead() || bEnteredRagdoll || bRecoverableRagdollActive ||
		!Config || !Config->bUseDrivenRagdoll || !Config->PoweredRagdoll || !RagdollComponent)
		return false;
	if (!Config->PoweredRagdoll->RecoverableGetUpChooser) return false;
	ReplicatedRecoverableRagdollState.Phase = EVBRecoverableRagdollPhase::Ragdoll;
	ReplicatedRecoverableRagdollState.LaunchVelocity = LaunchVelocity;
	ReplicatedRecoverableRagdollState.GetUpMontage = GetUpMontage;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	ReplicatedRecoverableRagdollState.ServerStartTime = GameState
		? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	ForceNetUpdate();
	MulticastStartRecoverableRagdoll(LaunchVelocity, GetUpMontage);
	if (!bRecoverableRagdollActive)
	{
		ReplicatedRecoverableRagdollState.Phase = EVBRecoverableRagdollPhase::None;
		ForceNetUpdate();
	}
	// 누운 동안과 기상 동안은 «엎드린 몸» 이다(설계 .sw/downed-hit 4절): 피격 반응·넘어짐·경직이 몽타주를 얹지 않고, 허브는 넉백 대신
	//   충격으로 돌리고, 처형 창은 일어날 때까지 기다린다. 떼는 곳 = ResetRecoverableRagdoll(기상 끝·실패). 누운 채 죽으면 남긴다(죽은 몸이다).
	if (bRecoverableRagdollActive && AbilitySystemComponent && !bRecoverableDownedTagGranted)
	{
		AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1, EGameplayTagReplicationState::CountToOwner);
		bRecoverableDownedTagGranted = true;
	}
	return bRecoverableRagdollActive;
}

void AVBEnemyBase::MulticastStartRecoverableRagdoll_Implementation(const FVector& LaunchVelocity, UAnimMontage* GetUpMontage)
{
	ApplyRecoverableRagdoll(LaunchVelocity, GetUpMontage);
}

void AVBEnemyBase::ApplyRecoverableRagdoll(const FVector& LaunchVelocity, UAnimMontage* GetUpMontage)
{
	if (bRecoverableRagdollActive || bEnteredRagdoll || !GetMesh() || !GetCapsuleComponent()) return;
	USkeletalMeshComponent* MeshComp = GetMesh();
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	RecoverableCapsuleProfile = Capsule->GetCollisionProfileName();
	RecoverableMeshProfile = MeshComp->GetCollisionProfileName();
	RecoverableCapsuleCollision = Capsule->GetCollisionEnabled();
	RecoverableMeshCollision = MeshComp->GetCollisionEnabled();
	RecoverableCapsuleResponses = Capsule->GetCollisionResponseToChannels();
	RecoverableMeshResponses = MeshComp->GetCollisionResponseToChannels();
	RecoverableCapsuleObjectType = Capsule->GetCollisionObjectType();
	RecoverableMeshObjectType = MeshComp->GetCollisionObjectType();
	RecoverableGetUpMontage = GetUpMontage;
	bRecoverableRagdollActive = true;
	if (AbilitySystemComponent)
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(VBGameplayTags::Ability_Attack);
		AbilitySystemComponent->BlockAbilitiesWithTags(AttackTags);
		if (HasAuthority()) AbilitySystemComponent->CancelAbilities(&AttackTags);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement()) Movement->SetMovementMode(MOVE_None);
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 누운 몸도 칼에 맞는다(user 2026-10-01, 설계 .sw/downed-hit). «Ragdoll» 프로필은 판정 채널에 응답이 없다 - 전역 프로필을 고치지 않고
	//   이 몸에만 켠다(시체·플레이어 래그돌이 같은 프로필이다). 프로필을 건 «뒤» 여야 남는다. 되돌리기는 ResetRecoverableRagdoll 의 응답 복원이 한다.
	MeshComp->SetCollisionResponseToChannel(VBCollision::MeleeTrace, ECR_Overlap);
	const FName RootBone = GetEnemyConfig()->RagdollRootBone;
	if (!RootBone.IsNone() && MeshComp->GetBoneIndex(RootBone) != INDEX_NONE)
	{
		MeshComp->SetAllBodiesBelowSimulatePhysics(RootBone, true, true);
		MeshComp->bBlendPhysics = true;
	}
	else
	{
		MeshComp->SetAllBodiesSimulatePhysics(true);
		// Keep the Character mesh attached so its authored relative offset survives recovery.
		MeshComp->bBlendPhysics = true;
	}
	MeshComp->WakeAllRigidBodies();
	if (!LaunchVelocity.IsNearlyZero()) MeshComp->SetAllPhysicsLinearVelocity(LaunchVelocity);
	BeginDrivenRagdoll();
	if (!bDrivenRagdollActive)
	{
		VB_LOG(Error, "%s: recoverable ragdoll could not start; restored animation", *GetName());
		if (HasAuthority()) FinishRecoverableGetUp();
		else ResetRecoverableRagdoll();
	}
}

void AVBEnemyBase::MulticastPlayAdditiveFlinch_Implementation(UAnimMontage* Montage)
{
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance || !Montage || Montage->SlotAnimTracks.IsEmpty())
	{
		return;
	}
	// 같은 슬롯의 앞 가산 몽타주는 손으로 멈춘다 - 아래에서 그룹 정지를 끄므로 엔진이 대신 멈춰 주지 않고, 겹치면 가산이 두 번 더해진다.
	//   가산 슬롯은 ABP 컴파일이 스켈레톤에 등록하며 DefaultGroup 에 들어간다(AnimGraphNode_Slot.cpp::BakeDataDuringCompilation ->
	//   USkeleton::RegisterSlotNode, 저장 안 된 등록도 USkeleton::GetSlotGroupName 이 DefaultGroup 으로 폴백한다).
	const FName Slot = Montage->SlotAnimTracks[0].SlotName;
	TArray<UAnimMontage*, TInlineAllocator<4>> Previous;
	for (const FAnimMontageInstance* Instance : AnimInstance->MontageInstances)
	{
		if (Instance && Instance->Montage && Instance->IsActive() && Instance->Montage->IsValidAdditive() && Instance->Montage->IsValidSlot(Slot))
		{
			Previous.AddUnique(Instance->Montage);
		}
	}
	for (UAnimMontage* Old : Previous)
	{
		AnimInstance->Montage_Stop(Old->BlendOut.GetBlendTime(), Old);
	}
	// 그룹 정지 끔(bStopAllMontages=false): 켜 두면 같은 DefaultGroup 의 넘어짐·기상 몽타주가 멈춘다
	//   (AnimInstance.cpp::UAnimInstance::Montage_PlayInternal - «한 그룹에 몽타주 하나»).
	AnimInstance->Montage_Play(Montage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, /*bStopAllMontages=*/false);
}

void AVBEnemyBase::MulticastStartRecoverableGetUp_Implementation()
{
	ApplyRecoverableGetUp();
}

void AVBEnemyBase::ApplyRecoverableGetUp()
{
	if (!bRecoverableRagdollActive || !bDrivenRagdollActive || !RagdollComponent) return;
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	const FAnimNode_PoseSearchHistoryCollector_Base* History = Anim
		? UPoseSearchLibrary::FindPoseHistoryNode(TEXT("PoseHistory"), Anim) : nullptr;
	if (!History || !History->GetPoseHistoryPtr())
	{
		VB_LOG(Error, "GASPGetUp: %s has no active PoseHistory", *GetName());
		if (HasAuthority()) FinishRecoverableGetUp();
		return;
	}
	// Save the physical mesh BEFORE disabling its pose producer or changing physics.
	Anim->SavePoseSnapshot(TEXT("Ragdoll"));
	UPoseSearchLibrary::OverridePoseHistoryFromOwningMesh(Anim, TEXT("PoseHistory"));
	if (!RagdollComponent->PrepareForGetUp())
	{
		VB_LOG(Error, "GASPGetUp: %s could not prepare physics handoff", *GetName());
		if (HasAuthority()) FinishRecoverableGetUp();
		return;
	}
	bDrivenRagdollActive = false;
	GetCapsuleComponent()->SetCollisionProfileName(RecoverableCapsuleProfile);
	GetCapsuleComponent()->SetCollisionObjectType(RecoverableCapsuleObjectType);
	GetCapsuleComponent()->SetCollisionResponseToChannels(RecoverableCapsuleResponses);
	GetCapsuleComponent()->SetCollisionEnabled(RecoverableCapsuleCollision);
	if (UCharacterMovementComponent* Movement = GetCharacterMovement()) Movement->SetMovementMode(MOVE_Walking);
	SetActorTickEnabled(true);
	if (HasAuthority() && AbilitySystemComponent)
	{
		FGameplayEventData Payload;
		Payload.EventTag = VBGameplayTags::Event_Combat_FinisherSurvived;
		Payload.Target = this;
		Payload.OptionalObject = GetEnemyConfig()->PoweredRagdoll;
		if (AbilitySystemComponent->HandleGameplayEvent(Payload.EventTag, &Payload) == 0)
		{
			VB_LOG(Error, "%s: recoverable ragdoll settled but GA_GetUp did not activate", *GetName());
			FinishRecoverableGetUp();
		}
	}
}

void AVBEnemyBase::FinishRecoverableGetUp()
{
	if (!HasAuthority() || !bRecoverableRagdollActive) return;
	ReplicatedRecoverableRagdollState.Phase = EVBRecoverableRagdollPhase::None;
	ForceNetUpdate();
	MulticastFinishRecoverableGetUp();
}

void AVBEnemyBase::MulticastFinishRecoverableGetUp_Implementation()
{
	ResetRecoverableRagdoll();
}

void AVBEnemyBase::ResetRecoverableRagdoll()
{
	if (!bRecoverableRagdollActive) return;
	const bool bKeepPhysicalAnimation = !IsDead() && !bDrivenRagdollActive && RagdollComponent &&
		GetEnemyConfig()->PoweredRagdoll &&
		GetEnemyConfig()->PoweredRagdoll->DefaultPhysicsProfile == EVBRagdollPhysicsProfile::PhysicalAnimation &&
		RagdollComponent->CompleteGetUpProfile();
	bDrivenRagdollActive = false;
	if (RagdollComponent && !bKeepPhysicalAnimation) RagdollComponent->EndPoweredRagdoll();
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		MeshComp->RemoveTickPrerequisiteActor(this);
		if (!bKeepPhysicalAnimation)
		{
			MeshComp->SetAllBodiesSimulatePhysics(false);
			MeshComp->bBlendPhysics = false;
		}
		MeshComp->SetCollisionProfileName(RecoverableMeshProfile);
		MeshComp->SetCollisionObjectType(RecoverableMeshObjectType);
		MeshComp->SetCollisionResponseToChannels(RecoverableMeshResponses);
		MeshComp->SetCollisionEnabled(RecoverableMeshCollision);
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionProfileName(RecoverableCapsuleProfile);
		Capsule->SetCollisionObjectType(RecoverableCapsuleObjectType);
		Capsule->SetCollisionResponseToChannels(RecoverableCapsuleResponses);
		Capsule->SetCollisionEnabled(RecoverableCapsuleCollision);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
		Movement->SetMovementMode(bDrivenRagdollHasFloor ? MOVE_Walking : MOVE_Falling);
	RecoverableGetUpMontage = nullptr;
	bRecoverableRagdollActive = false;
	if (AbilitySystemComponent)
	{
		FGameplayTagContainer AttackTags;
		AttackTags.AddTag(VBGameplayTags::Ability_Attack);
		AbilitySystemComponent->UnBlockAbilitiesWithTags(AttackTags);
		if (HasAuthority() && bRecoverableDownedTagGranted)
		{
			AbilitySystemComponent->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1, EGameplayTagReplicationState::CountToOwner);
			bRecoverableDownedTagGranted = false;
		}
	}
	SetActorTickEnabled(false);
}

FName AVBEnemyBase::ResolveDrivenRagdollBone() const
{
	const USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return NAME_None;
	}

	const UVBEnemyConfig* Config = GetEnemyConfig();

	// 설정 -> 래그돌 뿌리 -> 엔진 관례. 앞의 것이 비었을 때만 뒤로 간다.
	//   pelvis 를 마지막에 두는 이유: GASP 가 그 뼈를 읽고(SandboxCharacter_Mover_Ragdoll::Get_RagdollTransform)
	//   UE 마네킹 계열 골격이 전부 그 이름을 쓴다. 우리 적 셋도 그렇다.
	TArray<FName, TInlineAllocator<3>> Candidates;
	if (Config && !Config->DrivenRagdollFollowBone.IsNone())
	{
		Candidates.Add(Config->DrivenRagdollFollowBone);
	}
	if (Config && !Config->RagdollRootBone.IsNone())
	{
		Candidates.Add(Config->RagdollRootBone);
	}
	Candidates.Add(TEXT("pelvis"));

	for (const FName& Candidate : Candidates)
	{
		if (MeshComp->GetBoneIndex(Candidate) != INDEX_NONE)
		{
			return Candidate;
		}
	}
	return NAME_None;
}

void AVBEnemyBase::ConfigureDrivenRagdollCapsule()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule)
	{
		return;
	}
	// 캡슐을 스윕이 통하는 상태로 돌려놓는다. HandleDeathCosmetic 이 NoCollision 으로 꺼 뒀는데,
	//   그러면 SafeMoveUpdatedComponent 의 스윕이 아무것도 못 보고 벽을 통과한다.
	// 무엇에만 걸리게 하나: 월드(정적·동적)에만 막히고 나머지는 전부 통과다. Pawn 을 막으면
	//   시체가 플레이어를 밀고, 그것이 애초에 죽을 때 충돌을 껐던 이유다.
	// QueryOnly 인 것도 같은 이유 - 물리 충돌은 래그돌 메시가 이미 든다. 캡슐까지 물리로 두면
	//   같은 몸이 두 번 부딪힌다.
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	Capsule->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
}

bool AVBEnemyBase::IsBodyDownedInPhysics() const
{
	// 기상 중(GettingUp)은 아니다 - 몸을 애니로 넘겨받는 중이라(ApplyRecoverableGetUp 의 자세 스냅샷) 충격을 주면 넘겨받은 자세가 튄다.
	return bRecoverableRagdollActive && bDrivenRagdollActive && !IsDead();
}

float AVBEnemyBase::ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale)
{
	const UVBEnemyConfig* Config = GetEnemyConfig();
	const UVBRagdollConfig* Ragdoll = Config ? Config->PoweredRagdoll.Get() : nullptr;
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!HasAuthority() || !IsBodyDownedInPhysics() || !Ragdoll || !MeshComp || !GetWorld())
	{
		return 0.0f;
	}
	// 상한: 이 시간이 지나면 밀지 않는다 - 몸이 멎어 다음 기상 판정에서 일어난다(설정 주석).
	const double DownedFor = GetWorld()->GetTimeSeconds() - DrivenRagdollStartTime;
	if (DownedFor > Ragdoll->DownedHitWindowSeconds)
	{
		VB_LOG(Log, "쓰러진 몸 맞음[%s]: 충격 없음(창 지남 - 쓰러진 지 %.1f초, 창 %.1f초)",
		       *GetName(), DownedFor, Ragdoll->DownedHitWindowSeconds);
		return 0.0f;
	}
	const float* Horizontal = Ragdoll->DownedHitVelocity.Find(Impact);
	const float Scale = FMath::IsFinite(KnockbackScale) ? FMath::Max(0.0f, KnockbackScale) : 1.0f;
	FVector Velocity = PushDir.GetSafeNormal2D() * (Horizontal ? FMath::Max(0.0f, *Horizontal) : 0.0f) * Scale;
	if (Velocity.IsNearlyZero())
	{
		VB_LOG(Log, "쓰러진 몸 맞음[%s]: 충격 없음(등급 %s 의 값이 0 이거나 방향 없음)", *GetName(), VBHitImpactName(Impact));
		return 0.0f;
	}
	Velocity.Z += Ragdoll->DownedHitUpVelocity;

	// 맞은 자리: 판정이 준 뼈가 시뮬레이션 중인 바디면 그 접점, 아니면(구 스윕·비근접 피해·키네마틱 뿌리 바디) 쫓는 뼈(골반).
	FName Bone = Hit ? Hit->BoneName : NAME_None;
	FVector Point = Hit ? FVector(Hit->ImpactPoint) : FVector::ZeroVector;
	// 시뮬레이션 여부는 메시에 묻는다 - FBodyInstance::IsInstanceSimulatingPhysics 는 PhysicsCore 의 함수를 불러 이 모듈에서 링크가 안 된다.
	if (Bone.IsNone() || !MeshComp->GetBodyInstance(Bone) || !MeshComp->IsSimulatingPhysics(Bone) || Point.IsZero())
	{
		Bone = DrivenRagdollBone;
		Point = MeshComp->GetSocketLocation(Bone);
	}
	VB_LOG(Log, "쓰러진 몸 맞음[%s]: 등급 %s 뼈 %s 속도 %.0f cm/s / 쓰러진 지 %.1f초(창 %.1f초)",
	       *GetName(), VBHitImpactName(Impact), *Bone.ToString(), Velocity.Size(), DownedFor, Ragdoll->DownedHitWindowSeconds);
	MulticastDownedImpact(Point, Bone, Velocity);
	return Velocity.Size();
}

void AVBEnemyBase::MulticastDownedImpact_Implementation(FVector_NetQuantize Point, FName Bone, FVector_NetQuantize Velocity)
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!bRecoverableRagdollActive || !bDrivenRagdollActive || !MeshComp)
	{
		return;
	}
	// 질량과 무관한 속도 충격 - 설정 값이 곧 그 뼈가 받는 속도다. 구속으로 이어진 나머지 몸은 따라 끌린다.
	MeshComp->AddVelocityChangeImpulseAtLocation(Velocity, Point, Bone);
}

void AVBEnemyBase::BeginDrivenRagdoll()
{
	const UVBEnemyConfig* Config = GetEnemyConfig();
	if (!Config || !Config->bUseDrivenRagdoll)
	{
		return;
	}

	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule || !GetMesh())
	{
		return;
	}

	DrivenRagdollBone = ResolveDrivenRagdollBone();
	if (DrivenRagdollBone.IsNone())
	{
		// 쫓아갈 뼈가 없으면 A 를 포기한다. 조용히 포기하지 않는 이유: 이 경우 시체가 종전처럼
		//   죽은 자리에 캡슐을 남기는데, 화면에서는 「래그돌이 GASP 같지 않다」로만 보인다.
		VB_LOG(Warning, "몰아지는 래그돌: %s 쫓아갈 뼈를 못 찾아 건너뛴다 (설정 '%s' / 뿌리 '%s' / pelvis 모두 골격에 없음)",
		       *GetName(), *Config->DrivenRagdollFollowBone.ToString(), *Config->RagdollRootBone.ToString());
		return;
	}

	ConfigureDrivenRagdollCapsule();

	// Apply at runtime too: existing Blueprint defaults can retain PostPhysics.
	SetTickGroup(TG_PrePhysics);
	GetMesh()->AddTickPrerequisiteActor(this);
	bDrivenRagdollActive       = true;
	DrivenRagdollStartTime     = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	DrivenRagdollStartLocation = Capsule->GetComponentLocation();
	DrivenRagdollSettledFrames = 0;
	NextRecoverableGetUpCheckTime = DrivenRagdollStartTime +
		(Config->PoweredRagdoll ? Config->PoweredRagdoll->NPCGetUpCheckSeconds : 3.0f);
	bDrivenRagdollHasFloor = false;
	if (RagdollComponent && Config->PoweredRagdoll)
	{
		if (!RagdollComponent->BeginPoweredRagdoll(GetMesh(), Config->PoweredRagdoll))
		{
			VB_LOG(Warning, "%s: powered 래그돌 초기화 실패 (설정 '%s') - 물리 제어와 캡슐 추종을 종료하고 passive 래그돌을 유지한다",
			       *GetName(), *GetNameSafe(Config->PoweredRagdoll));
			EndDrivenRagdoll(TEXT("물리 제어 초기화 실패"));
			return;
		}
	}
	SetActorTickEnabled(true);
	VB_LOG(Log, "RagdollFollowOrder: %s actorTick=%d meshTick=%d followBeforeMesh=1 meshTeleportGuard=1",
		*GetName(), int32(PrimaryActorTick.TickGroup), int32(GetMesh()->PrimaryComponentTick.TickGroup));

	VB_LOG(Log, "몰아지는 래그돌 A 시작: %s '%s' 를 쫓는다 (초기 속도 %.0f cm/s, 상한 %.1f초)",
	       *GetName(), *DrivenRagdollBone.ToString(), RagdollLaunchVelocity.Size(), Config->DrivenRagdollMaxSeconds);
}

void AVBEnemyBase::TickDrivenRagdoll(float DeltaTime)
{
	const UVBEnemyConfig*        Config   = GetEnemyConfig();
	UCapsuleComponent*           Capsule  = GetCapsuleComponent();
	USkeletalMeshComponent*      MeshComp = GetMesh();
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Config || !Capsule || !MeshComp || DeltaTime <= 0.0f)
	{
		EndDrivenRagdoll(TEXT("전제 소실"));
		return;
	}

	// 1. Read the last finalized physics pose before changing its component frame.
	// The mesh prerequisite ensures the next animation/physics evaluation uses the new frame.
	const FTransform BoneTransform = MeshComp->GetSocketTransform(DrivenRagdollBone, RTS_World);
	const FVector    BoneLocation  = BoneTransform.GetLocation();

	// 2. 캡슐 바닥을 땅에 세운다. 뼈 위치를 그대로 쓰면 캡슐 중심이 골반 높이라 아래 절반이 땅에 묻힌다.
	//    GASP 는 여기서 SphereTraceSingle 로 바닥을 찾아 그 지점을 쓰고, 안 맞으면 뼈 위치를 그대로 쓴다.
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Radius     = Capsule->GetScaledCapsuleRadius();

	FVector TargetLocation = BoneLocation;
	bDrivenRagdollHasFloor = false;
	if (RagdollComponent) RagdollComponent->SetEnvironment(FVector::UpVector, false);
	{
		FHitResult            GroundHit;
		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(VBDrivenRagdollGround), /*bTraceComplex*/ false, this);
		TraceParams.AddIgnoredComponent(MeshComp);
		const FVector TraceEnd = BoneLocation - FVector(0.0f, 0.0f, HalfHeight * 2.0f);
		if (GetWorld() && GetWorld()->SweepSingleByChannel(
				GroundHit, BoneLocation, TraceEnd, FQuat::Identity, ECC_WorldStatic,
				FCollisionShape::MakeSphere(Radius), TraceParams))
		{
			TargetLocation.Z = GroundHit.ImpactPoint.Z + HalfHeight;
			bDrivenRagdollHasFloor = true;
			if (RagdollComponent) RagdollComponent->SetEnvironment(GroundHit.ImpactNormal, true);
		}
	}

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (bRecoverableRagdollActive && HasAuthority() && RagdollComponent && Config->PoweredRagdoll &&
		Now >= NextRecoverableGetUpCheckTime)
	{
		NextRecoverableGetUpCheckTime = Now + Config->PoweredRagdoll->NPCGetUpCheckSeconds;
		const float Speed = RagdollComponent->GetAnimationState().Speed;
		VB_LOG(Log, "GASPGetUp check: %s spineSpeed=%.1f floor=%d", *GetName(), Speed, bDrivenRagdollHasFloor);
		if (bDrivenRagdollHasFloor && Speed <= Config->PoweredRagdoll->NPCGetUpSpeed)
		{
			// SavePoseSnapshot/PoseHistory must still see the frame in which the
			// completed pose was evaluated. Do not move the capsule before capture.
			// 누운 시간 - 처형 창의 반응 대기 상한과 겨루는 값이라 PIE 에서 이 줄로 잰다(설계 .sw/downed-hit 5절).
			VB_LOG(Log, "쓰러진 몸 기상 시작[%s]: 누운 시간 %.1f초", *GetName(), Now - DrivenRagdollStartTime);
			ReplicatedRecoverableRagdollState.Phase = EVBRecoverableRagdollPhase::GettingUp;
			ForceNetUpdate();
			MulticastStartRecoverableGetUp();
			return;
		}
	}

	// 3. 스윕 이동. Mover 의 Try Safe Move And Slide 자리다 - 벽에 막히면 거기까지만 간다.
	const FVector  StartLocation  = Capsule->GetComponentLocation();
	const FQuat    StartRotation  = Capsule->GetComponentQuat();
	const FVector  MoveDelta      = TargetLocation - StartLocation;
	const FRotator TargetRotation = bRecoverableRagdollActive && RagdollComponent
		? RagdollComponent->GetTargetOrientation().Rotation()
		: Config->bDrivenRagdollFollowsRotation ? FRotator(0.0f, BoneTransform.Rotator().Yaw, 0.0f)
		: Capsule->GetComponentRotation();
	static const IConsoleVariable* PoseDiagnostics = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.Ragdoll.PoseDiagnostics"));
	const FBodyInstance* FollowBody = PoseDiagnostics && PoseDiagnostics->GetInt() != 0
		? MeshComp->GetBodyInstance(DrivenRagdollBone) : nullptr;
	const FTransform BodyBeforeFollow = FollowBody ? FollowBody->GetUnrealWorldTransform() : FTransform::Identity;
	const FTransform MeshRelativeBeforeFollow = MeshComp->GetRelativeTransform();
	FHitResult MoveHit(1.0f);
	{
		// SafeMove may resolve penetration with TeleportPhysics, including the new yaw.
		// Keep that parent teleport from moving the independently simulated mesh bodies.
		const FScopedPreventAttachedComponentMove PreventMeshMove(MeshComp);
		if (Movement)
		{
			Movement->SafeMoveUpdatedComponent(MoveDelta, TargetRotation, /*bSweep*/ true, MoveHit);
		}
		else
		{
			Capsule->MoveComponent(MoveDelta, TargetRotation, /*bSweep*/ true, &MoveHit);
		}
	}
	// Restore the component frame before mesh evaluation, without teleporting bodies.
	// The scope alone preserves world space and would otherwise accumulate relative drift.
	MeshComp->SetRelativeTransform(MeshRelativeBeforeFollow, false, nullptr, ETeleportType::None);
	if (FollowBody)
	{
		const FTransform BodyAfterFollow = FollowBody->GetUnrealWorldTransform();
		const double BodyStep = FVector::Distance(BodyBeforeFollow.GetLocation(), BodyAfterFollow.GetLocation());
		const double BodyRotationStep = FMath::RadiansToDegrees(BodyBeforeFollow.GetRotation().AngularDistance(BodyAfterFollow.GetRotation()));
		const double ActorRotationStep = FMath::RadiansToDegrees(StartRotation.AngularDistance(Capsule->GetComponentQuat()));
		if (ActorRotationStep > 45.0 || BodyStep > 1.0 || BodyRotationStep > 1.0)
		{
			VB_LOG(Log, "RagdollFollowFrame: %s frame=%llu worldTime=%.3f actorRotationStepDeg=%.2f body=%s bodyStepCm=%.3f bodyRotationStepDeg=%.3f finalPenetrating=%d",
				*GetName(), GFrameCounter, Now, ActorRotationStep, *DrivenRagdollBone.ToString(), BodyStep, BodyRotationStep,
				MoveHit.bStartPenetrating ? 1 : 0);
		}
	}

	// 4. 실제로 간 만큼을 속도로 심는다. 목표 델타가 아니라 «간 것» 이어야 한다 - 벽에 막힌 프레임에
	//    목표 델타를 심으면 아무도 안 움직였는데 속도만 큰 상태가 된다.
	//    왜 필요한가: 이 값을 0 으로 두면 위에 얹힌 것들(카메라 랙·AI 인지·사운드)이
	//    「이 액터는 안 움직인다」로 읽는다. 위치만 바뀌고 속도가 없는 상태는 CMC 에게 거짓말이다.
	if (Movement)
	{
		Movement->Velocity = (Capsule->GetComponentLocation() - StartLocation) / DeltaTime;
	}

	// A living ragdoll keeps simulating until the get-up check succeeds.
	if (bRecoverableRagdollActive) return;

	// 5. 전이 판정. 바디 «전부» 중 최고 속도를 본다 - 골반이 멎어도 팔다리가 튀면 아직 쓰러지는 중이다.
	float MaxBodySpeed = 0.0f;
	for (const FBodyInstance* Body : MeshComp->Bodies)
	{
		if (Body)
		{
			MaxBodySpeed = FMath::Max(MaxBodySpeed, static_cast<float>(Body->GetUnrealWorldVelocity().Size()));
		}
	}

	DrivenRagdollSettledFrames = (MaxBodySpeed < Config->DrivenRagdollSettleSpeed)
		                             ? DrivenRagdollSettledFrames + 1
		                             : 0;

	if (DrivenRagdollSettledFrames >= Config->DrivenRagdollSettleFrames)
	{
		EndDrivenRagdoll(TEXT("멎음"));
		return;
	}
	if (Now - DrivenRagdollStartTime >= Config->DrivenRagdollMaxSeconds)
		EndDrivenRagdoll(TEXT("상한"));
}

void AVBEnemyBase::EndDrivenRagdoll(const TCHAR* Reason)
{
	if (!bDrivenRagdollActive)
	{
		return;
	}
	bDrivenRagdollActive = false;
	if (bRecoverableRagdollActive)
	{
		ResetRecoverableRagdoll();
		return;
	}
	if (RagdollComponent) RagdollComponent->EndPoweredRagdoll();
	SetActorTickEnabled(false);

	UCapsuleComponent*      Capsule  = GetCapsuleComponent();
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (MeshComp) MeshComp->RemoveTickPrerequisiteActor(this);

	// B 단계 = 지금까지의 동작. 캡슐은 다시 아무것도 아니다.
	if (Capsule)
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->Velocity = FVector::ZeroVector;
	}

	// 되는지 아는 법(설계 .sw/ragdoll/design.md §5 의 2): 캡슐과 몸이 갈라졌는지를 숫자로 낸다.
	//   A 가 일을 했다면 이 값이 캡슐 반높이 언저리고, 안 했다면 죽은 자리까지의 거리만큼 크다.
	const double  Now       = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FVector CapsuleAt = Capsule ? Capsule->GetComponentLocation() : FVector::ZeroVector;
	const FVector BodyAt    = (MeshComp && !DrivenRagdollBone.IsNone())
		                          ? MeshComp->GetSocketLocation(DrivenRagdollBone)
		                          : CapsuleAt;
	VB_LOG(Log, "몰아지는 래그돌 A->B: %s 사유 %s, %.2f초 동안 %.0fcm 이동, 캡슐-몸 거리 %.0fcm",
	       *GetName(), Reason, Now - DrivenRagdollStartTime,
	       FVector::Dist(DrivenRagdollStartLocation, CapsuleAt), FVector::Dist(CapsuleAt, BodyAt));
}

void AVBEnemyBase::SettleRagdollVelocity()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		return;
	}
	// 최대값을 보는 이유: GetPhysicsLinearVelocity 는 루트 바디(보통 골반) 하나만 답한다. 골반이 멈춰 있어도
	//   팔다리가 튀면 시체는 날아간다 - 어느 뼈가 튀는지가 곧 원인의 방향이다.
	float MaxSpeed = 0.0f;
	FName FastestBone = NAME_None;
	for (const FBodyInstance* Body : MeshComp->Bodies)
	{
		if (!Body)
		{
			continue;
		}
		const float Speed = static_cast<float>(Body->GetUnrealWorldVelocity().Size());
		if (Speed > MaxSpeed)
		{
			MaxSpeed    = Speed;
			FastestBone = Body->BodySetup.IsValid() ? Body->BodySetup->BoneName : NAME_None;
		}
	}
	// 운동량을 남기는 래그돌은 «재기만» 한다. 던져진 것은 그 운동량이 곧 나머지 비행이라 지우면
	//   공중에서 멈추고, 보통 사망도 GASP 처럼 쓰러지려면 지우면 안 된다(설정 bPreserveRagdollMomentum).
	//   계측은 남긴다 - 끄는 것과 안 보이는 것은 다르고, 숫자가 있어야 다음에 판정할 수 있다.
	if (ShouldPreserveRagdollMomentum())
	{
		VB_LOG(Log, "래그돌 안정화: %s 최대 바디 속도 %.0f cm/s (%s) - %s 라 지우지 않는다",
		       *GetName(), MaxSpeed, *FastestBone.ToString(),
		       bRagdollKeepsMomentum ? TEXT("던져진 래그돌") : TEXT("운동량 보존 설정"));
		return;
	}

	VB_LOG(Log, "래그돌 안정화: %s 최대 바디 속도 %.0f cm/s (%s) -> 0",
	       *GetName(), MaxSpeed, *FastestBone.ToString());
	MeshComp->SetAllPhysicsLinearVelocity(FVector::ZeroVector);
	MeshComp->SetAllPhysicsAngularVelocityInRadians(FVector::ZeroVector);
}

void AVBEnemyBase::DestroyAfterDeath()
{
	Destroy();
}
