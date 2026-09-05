// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Enemy/VBEnemyBase.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Character/VBHitFlashComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBEnemyConfig.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Vowbound/Vowbound.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AI/VBAIController.h"


AVBEnemyBase::AVBEnemyBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;

	// ASC 생성 — replication 은 UVBAbilitySystemComponent 생성자가 자체 설정(SetIsReplicatedByDefault)하므로 여기선 호출 불요
	AbilitySystemComponent = CreateDefaultSubobject<UVBAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	// ASC 기본값은 Mixed(플레이어용)이나, NPC는 Minimal로 다시 재설정한다.
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);


	// Attribute 생성
	HealthAttributeSet = CreateDefaultSubobject<UVBHealthAttributeSet>(TEXT("HealthAttributeSet"));
	CombatAttributeSet = CreateDefaultSubobject<UVBCombatAttributeSet>(TEXT("CombatAttributeSet"));

	// 피격 플래시 컴포넌트 — 모든 적이 자동으로 갖는다. BP 배선 누락으로 특정 적만 안 빛나는 사고를 구조적으로 막는다.
	HitFlashComponent = CreateDefaultSubobject<UVBHitFlashComponent>(TEXT("HitFlashComponent"));

	// 전투 조율 컴포넌트 — 위와 같은 이유로 C++ 생성. 이 컴포넌트가 없으면 BTService 가 조용히 아무것도
	// 하지 않아 그 적만 그룹에 합류하지 않는다(로그 없는 결함).
	CombatComponent = CreateDefaultSubobject<UVBEnemyCombatComponent>(TEXT("CombatComponent"));

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
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		const UVBEnemyConfig* Cfg = GetEnemyConfig();
		MoveComp->MaxWalkSpeed = Cfg->MaxWalkSpeed;
		MoveComp->RotationRate = Cfg->RotationRate;
	}

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
	return UVBAbilitySystemStatics::IsDead(AbilitySystemComponent);
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
	// 0. 일반 사망 명성 처리 (의료 행위가 아닌 경우만)
	ApplyDefaultDeathReputation();

	// 1. State.Dead 태그 부여
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Dead, 1, EGameplayTagReplicationState::CountToOwner);
		AbilitySystemComponent->CancelAllAbilities();
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

	// 3. 래그돌/콜리전 전환을 모든 Net Role에 동시 적용 (Multicast로 타이밍 동기화)
	MulticastHandleDeathCosmetic();
}

void AVBEnemyBase::MulticastHandleDeathCosmetic_Implementation()
{
	// 원격 클라에 State_Dead 를 로컬 부여한다. FindBestTarget 이 시체를 조준하는 것을 막는 타겟팅 필터 정합용.
	// 정정(2026-07-29): 이 보완의 원래 근거였던 "CountToOwner 는 소유 연결 없는 AI라 미복제"는 사실이 아니다.
	//   엔진 정의는 "Tag will replicate to all and Count will replicate to owner"(GameplayEffectTypes.h::EGameplayTagReplicationState::CountToOwner)이고
	//   직렬화도 비-소유 연결에 항목은 보내고 Count 만 1 로 대체한다(GameplayEffectTypes.cpp::FMinimalReplicationTagCountMap::NetSerialize).
	//   HasMatchingGameplayTag 는 Count>0 판정이므로 태그는 어차피 전원에게 간다.
	// 따라서 이 블록은 UE5.7 기준 중복이다. 남겨 두는 이유는 무해하고(!HasAuthority 가드), 복제 도달 전
	//   한두 틱의 창을 즉시 메워 주기 때문이다. 지울 거라면 그 타이밍 창을 먼저 확인할 것.
	// 같은 이유로 플레이어(AVBCharacter)에는 이 보완이 없다 — "owning connection 유무" 때문이 아니다.
	if (!HasAuthority() && AbilitySystemComponent)
	{
		AbilitySystemComponent->AddLooseGameplayTag(VBGameplayTags::State_Dead, 1, EGameplayTagReplicationState::CountToOwner);
	}
	HandleDeathCosmetic();
}

void AVBEnemyBase::HandleDeathCosmetic()
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

	// 5. 래그돌 전환
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	// 래그돌 메쉬가 카메라 프로브 채널만 통과하게 한다. 엔진 "Ragdoll" 프로파일은 Pawn/Visibility 만 Ignore 하고
	// Camera 는 기본 Block 이라, 플레이어 스프링암(bDoCollisionTest, ProbeChannel=ECC_Camera)이 쓰러진 시체에 충돌해
	// 카메라를 캐릭터 쪽으로 확 당긴다(급줌인). 반드시 SetCollisionProfileName 뒤에 둘 것 — 프로파일 설정이 채널 응답을 리셋한다.
	// 물리(QueryAndPhysics/시뮬레이션/월드 충돌)는 그대로 유지 — 오직 카메라 프로브만 통과.
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	GetMesh()->SetAllBodiesSimulatePhysics(true);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->WakeAllRigidBodies();

	// 6. 5초 후 제거
	if (HasAuthority())
	{
		FTimerHandle DestroyTimerHandle;
		GetWorldTimerManager().SetTimer(
										DestroyTimerHandle,
										this,
										&AVBEnemyBase::DestroyAfterDeath,
										GetEnemyConfig()->DeathCleanupDelay,
										false
									   );
	}
}

void AVBEnemyBase::DestroyAfterDeath()
{
	Destroy();
}
