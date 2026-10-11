// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Character/VBCharacter.h"
#include "CharacterCommon/VBRagdollComponent.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Character/VBSlideMovementComponent.h"
#include "Animation/VBLookAtPOIComponent.h"
#include "Animation/VBPlayerAttentionComponent.h"
#include "Components/PostProcessComponent.h"  // 처형 준비 외곽선을 화면에 건다

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h" // 사망 훅 — Health 어트리뷰트 변경 델리게이트
#include "Player/VBPlayerState.h"
#include "Player/VBPlayerController.h"                     // 사망 연출(슬로모/게임오버) 위임 대상
#include "Components/CapsuleComponent.h"                   // 사망 시 캡슐 콜리전 해제
#include "ContextualAnimSceneActorComponent.h"              // 처형 장면 참가 자리 — 생성자에서 만든다

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/Abilities/VBGA_Crouch.h"   // 자세 관문 묶음(GetPostureBlockingTags)
#include "Interaction/VBGA_Interact.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "Camera/VBEnvironmentCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Character/VBTargetLockComponent.h"
#include "Data/VBTargetLockConfig.h"
#include "Data/VBMovementConfig.h"
#include "Data/VBTurnInPlaceConfig.h"
#include "Data/VBCameraConfig.h"
#include "Data/VBParryConfig.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"   // UAnimMontage 완전 타입 — 가드 몽타주 캐시(TObjectPtr) / Montage_* 호출
                                     // (섹션명 계약 검증은 UVBParryConfig::IsDataValid 로 이관됨 — CHORE-031)
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Input/VBInputDeviceManager.h"
#include "Net/UnrealNetwork.h"
#include "Vowbound/Vowbound.h"

#include "MotionWarpingComponent.h"
#include "Animation/VBAnimInstance.h"
#include "PoseSearch/PoseSearchDatabase.h" // 진단 로그가 선택된 MM 풀 이름을 찍는다 - GetName() 에 완전 타입 필요
#include "Character/VBCharacterTrajectoryComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Character/VBContextEffectComponent.h"
#include "Character/VBFinisherComponent.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"                 // 카메라 감도/Y반전 설정 읽기(HandleLook)
#include "Traversal/VBTraversalComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Curves/CurveFloat.h"
// PlayAnimation(UAnimationAsset*) 에 UAnimSequence* 를 넘기려면 완전 타입이 필요하다(사망 애니).
#include "Animation/AnimSequence.h"
#include "AI/VBAIController.h"                              // 사망 시 적 AI 타겟 해제
#include "Animation/VBMotionWarpingNames.h"
#include "Character/VBTargetable.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"


AVBCharacter::AVBCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVBSlideMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// GASP RotationRate 토글 (Grounded/Falling) 을 위해 Tick 활성.
	// 그 외 로직은 여전히 이벤트 기반 — Tick 내부는 movement mode 감지 + CMC RotationRate 설정만 수행.
	PrimaryActorTick.bCanEverTick          = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	// 네트워크
	bReplicates = true;
	SetReplicatingMovement(true);

	// CharacterMovement 기본 설정
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		// 물리 baseline(감속/가속/마찰/점프/에어컨트롤/crouch/회전율)은 BeginPlay 의 ApplyMovementConfigToCMC() 가
		// UVBMovementConfig 로부터 적용한다(생성자 시점엔 MovementConfig BP 배선 미해석).
		// MaxWalkSpeed 는 여기서 초기화하지 않는다 — tick 이 매 프레임 UpdateMaxSpeed() 로 덮으므로
		// 사전값이 무의미한데, 리터럴을 두면 "이 500 은 어디서 오나"를 추적하는 헛수고를 만들고
		// UnarmedProfile.RunSpeeds.X 와 갈라질 수 있는 두 번째 홈이 된다(2026-08-09 제거).
		// GASP CDO 정렬 (gasp_full_reference.md:1319-1321): 둘 다 default false.
		// 실제 회전 분기는 매 Tick ApplyRotationMode() 에서 bStrafe 기준 토글 (GASP UpdateRotation Branch 1).
		// Strafe 시: bUseControllerDesiredRotation=true, bOrientRotationToMovement=false (capsule이 ControlRotation 추종).
		// Free 시: bUseControllerDesiredRotation=false, bOrientRotationToMovement=true (capsule이 Velocity 추종).
		MoveComp->bOrientRotationToMovement             = false;
		MoveComp->bUseControllerDesiredRotation         = false;
		MoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
	}


	// SpringArm 생성 - 3인칭 카메라 붐
	// 수치는 CDO 디폴트로 깔아둔다 — 생성자 시점엔 CameraConfig(BP 배선)가 아직 해석되지 않아 자산값을 못 읽는다.
	// 배선된 자산 반영은 PostInitializeComponents 의 ApplyCameraConfig() 가 담당(P1.1).
	// 여기서도 값을 넣는 이유: 에디터 프리뷰/미배선 상태에서도 카메라가 0 길이로 붕괴하지 않게.
	const UVBCameraConfig* CamDefaults = GetDefault<UVBCameraConfig>();
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArmComponent->SetupAttachment(RootComponent);
	SpringArmComponent->TargetArmLength         = CamDefaults->TargetArmLength;
	SpringArmComponent->SocketOffset            = CamDefaults->SocketOffset;
	SpringArmComponent->bUsePawnControlRotation = true; // 마우스로 카메라 회전 (구조 — 튜닝 노브 아님)
	SpringArmComponent->bEnableCameraLag        = true; // 부드러운 카메라 추적 (구조 — lag 세기는 config)
	SpringArmComponent->CameraLagSpeed          = CamDefaults->CameraLagSpeed;
	SpringArmComponent->CameraLagMaxDistance    = CamDefaults->CameraLagMaxDistance;

	// Camera 생성
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	CameraComponent->SetupAttachment(SpringArmComponent, USpringArmComponent::SocketName);
	CameraComponent->FieldOfView = CamDefaults->FieldOfView;

	GameplayCameraComponent = CreateDefaultSubobject<UGameplayCameraComponent>(TEXT("GameplayCamera"));
	// GASP rigs add their own standing height above the mesh origin (feet).
	GameplayCameraComponent->SetupAttachment(GetMesh());
	GameplayCameraComponent->SetAutoActivate(false);
	GameplayCameraComponent->DefaultPlayer = EAutoReceiveInput::Disabled;
	EnvironmentCameraComponent = CreateDefaultSubobject<UVBEnvironmentCameraComponent>(TEXT("EnvironmentCamera"));
	RagdollComponent = CreateDefaultSubobject<UVBRagdollComponent>(TEXT("RagdollComponent"));
	PlayerRagdollComponent = CreateDefaultSubobject<UVBPlayerRagdollComponent>(TEXT("PlayerRagdollComponent"));
	ContextEffectComponent = CreateDefaultSubobject<UVBContextEffectComponent>(TEXT("ContextEffectComponent"));
	LookAtPOI = CreateDefaultSubobject<UVBLookAtPOIComponent>(TEXT("LookAtPOI"));
	PlayerAttention = CreateDefaultSubobject<UVBPlayerAttentionComponent>(TEXT("PlayerAttention"));

	// 캐릭터가 카메라 방향으로 즉시 회전하지 않게
	// 이동 방향으로 회전하게 설정
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;

	// InputDeviceManager 컴포넌트 생성
	InputDeviceManager = CreateDefaultSubobject<UVBInputDeviceManager>(TEXT("InputDeviceManager"));

	// LockComponent 생성
	TargetLockComponent = CreateDefaultSubobject<UVBTargetLockComponent>(TEXT("TargetLockComponent"));

	// MotionWarping 생성
	MotionWarpingComponent = CreateDefaultSubobject<UMotionWarpingComponent>(TEXT("MotionWarpingComponent"));
	
	// WeaponState 생성
	WeaponStateComponent = CreateDefaultSubobject<UVBWeaponStateComponent>(TEXT("WeaponStateComponent"));
	MeleeTraceComponent = CreateDefaultSubobject<UVBMeleeTraceComponent>(TEXT("MeleeTraceComponent"));
	FinisherComponent = CreateDefaultSubobject<UVBFinisherComponent>(TEXT("FinisherComponent"));
	// 플러그인이 두 액터를 묶을 때 공격자에도 이것이 있어야 한다. 시작 순간에 붙이지 않는 이유는
	// AVBEnemyBase::FinisherSceneComponent 주석에 있다(원격 클라 도착 순서).
	FinisherSceneComponent =
		CreateDefaultSubobject<UContextualAnimSceneActorComponent>(TEXT("FinisherSceneComponent"));

	// 화면 후처리. 무한 범위라 플레이어가 어디 있든 자기 화면에 걸린다.
	//   레벨 볼륨에 두면 레벨마다 다시 깔아야 하고 빠뜨리면 그 레벨에서만 조용히 안 나온다.
	PostProcessComponent = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcessComponent"));
	PostProcessComponent->SetupAttachment(RootComponent);
	PostProcessComponent->bUnbound = true;
	
	// MotionTrajectory 생성
	TrajectoryComponent = CreateDefaultSubobject<UVBCharacterTrajectoryComponent>(TEXT("TrajectoryComponent"));

	// 트래버설(맨틀) 생성 — IA_Jump 시 TryTraversalAction 우선. 자산(Config/CHT) 미할당 시 false 반환 → 기존 점프(안전 폴백).
	TraversalComponent = CreateDefaultSubobject<UVBTraversalComponent>(TEXT("TraversalComponent"));

	// 세이브 위치 복원(WP 스트리밍-안전). 평소 무동작 — GameMode 가 Continue 로드 시에만 BeginRestore 호출.
	PlayerRestoreComponent = CreateDefaultSubobject<UVBPlayerRestoreComponent>(TEXT("PlayerRestoreComponent"));

	InteractorComponent = CreateDefaultSubobject<UVBInteractorComponent>(TEXT("InteractorComponent"));
	SmartObjectInteraction = CreateDefaultSubobject<UVBGASPSmartObjectComponent>(TEXT("SmartObjectInteraction"));
}

void AVBCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (IsValid(NewController) && NewController->IsPlayerController() && !PlayerReadinessComponent)
	{
		PlayerReadinessComponent = FindComponentByClass<UVBPlayerReadinessComponent>();
		if (!PlayerReadinessComponent)
		{
			PlayerReadinessComponent = NewObject<UVBPlayerReadinessComponent>(this, NAME_None, RF_Transient);
			AddInstanceComponent(PlayerReadinessComponent);
			PlayerReadinessComponent->RegisterComponent();
		}
	}

	// 서버: PlayerState의 ASC를 가져와서 ActorInfo 초기화
	if (AVBPlayerState* PS = GetPlayerState<AVBPlayerState>())
	{
		CachedASC = PS->GetVBAbilitySystemComponent();
		if (CachedASC.IsValid())
		{
			CachedASC->InitAbilityActorInfo(PS, this);
			if (SmartObjectInteraction) SmartObjectInteraction->RefreshAbilityState();
			if (PlayerRagdollComponent) PlayerRagdollComponent->RefreshAbilityState();

			// 서버에서 기본 Ability 부여
			if (HasAuthority())
			{
				if (!PS->AreStartupAbilitiesGranted())
				{
					for (const TSubclassOf<UGameplayAbility>& AbilityClass : PS->DefaultAbilities)
					{
						if (!AbilityClass)
						{
							continue;
						}

						FGameplayAbilitySpec Spec(AbilityClass, 1, INDEX_NONE, this);
						CachedASC->GiveAbility(Spec);
						VB_LOG(Warning, "GiveAbility: %s", *AbilityClass->GetName());
					}

					PS->MarkStartupAbilitiesGranted();
				}

				if (!PS->AreStartupEffectsApplied())
				{
					for (const TSubclassOf<UGameplayEffect>& EffectClass : PS->DefaultEffects)
					{
						if (!EffectClass)
							continue;

						FGameplayEffectContextHandle Context = CachedASC->MakeEffectContext();
						FGameplayEffectSpecHandle    Spec    = CachedASC->MakeOutgoingSpec(
						 EffectClass, 1, Context);
						if (Spec.IsValid())
						{
							CachedASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
						}
					}

					PS->MarkStartupEffectsApplied();
				}

				// 사망 훅 바인드는 반드시 이 위치 — HasAuthority 블록의 '끝', DefaultEffects(GE_InitStats) 적용 뒤.
				// 왜: GE_InitStats 는 Health 를 0 → MaxHealth 로 끌어올리는데, 그 과정에서 어트리뷰트 변경
				//   델리게이트가 발화한다. 바인드를 위 루프보다 앞에 두면 초기화 도중의 값(0)이 사망 핸들러를
				//   타 스폰하자마자 죽는다.
				// 전제: PossessedBy 는 서버에서만 호출되므로 클라 오발화가 구조적으로 불가능하다
				//   (HasAuthority 가드는 그 위의 이중화). 폰마다 새로 possess 되므로 중복 구독도 없다.
				// 부작용: 구독자 1 추가뿐. 이 델리게이트의 기존 C++ 구독자는 UI(UVBHealthBarWidget) 하나이며
				//   BP 바인딩은 0건으로 실측 확인됨 — 기존 바인딩과의 충돌 없음.
				CachedASC->GetGameplayAttributeValueChangeDelegate(
					UVBHealthAttributeSet::GetHealthAttribute()).AddUObject(this, &AVBCharacter::OnHealthChanged);
			}
		}
	}
}

// ──────────────────────────────────────────────────────────────────────────────
// 사망 (FIND-062) — AVBEnemyBase 의 검증된 사망 구현을 1:1 대칭 이식.
// 적과의 유일한 의도적 차이는 Destroy 타이머가 없다는 점. 플레이어 폰이 사라지면 카메라 뷰타겟이 소실된다.
// ──────────────────────────────────────────────────────────────────────────────

bool AVBCharacter::IsDead() const
{
	// 판정은 UVBAbilitySystemStatics 가 소유한다 - 여기는 "어느 ASC 인가"만 정한다.
	// CachedASC 는 TWeakObjectPtr — operator bool 이 delete 라 .Get() 으로 원시 포인터를 넘긴다(C2280 회피).
	return UVBAbilitySystemStatics::IsDead(CachedASC.Get());
}

void AVBCharacter::OnHealthChanged(const struct FOnAttributeChangeData& Data)
{
	// 조건 3종은 적(AVBEnemyBase::OnHealthChanged)과 동일: 0 이하 + 아직 안 죽음 + 서버 권위.
	// !IsDead() 가드가 같은 프레임 다중 킬링 GE 로 HandleDeath 가 2회 도는 것을 막는다
	// (HandleDeath 가 태그를 최우선으로 부여하므로 2회차는 여기서 걸린다).
	if (Data.NewValue <= 0.0f && !IsDead())
	{
		VB_LOG(Warning, "Player 사망! %s (Health: %.1f -> %.1f)",
		       *GetName(), Data.OldValue, Data.NewValue);
		// 서버에서만 HandleDeath 호출. 래그돌/카메라는 Multicast 로 클라에 전파됨(타이밍 동기화).
		if (HasAuthority())
		{
			HandleDeath();
		}
	}
}

void AVBCharacter::HandleDeath()
{
	// 1. State.Dead 태그 부여 + 활성 능력 취소.
	if (CachedASC.IsValid())
	{
		// CountToOwner 는 태그를 모든 연결에 복제하고 카운트만 소유자에게 보낸다
		// (엔진 정의 GameplayEffectTypes.h::EGameplayTagReplicationState::CountToOwner, 직렬화 GameplayEffectTypes.cpp::FMinimalReplicationTagCountMap::NetSerialize). owning connection 유무와 무관하다.
		CachedASC->AddLooseGameplayTag(VBGameplayTags::State_Dead, 1, EGameplayTagReplicationState::CountToOwner);
		// CancelAllAbilities 는 이미 활성인 GA 만 끈다(예: VBGA_Guard 의 EndAbility → EndGuard).
		// 신규 활성화 차단은 각 GA 의 ActivationBlockedTags(State.Dead) 몫 — FIND-067.
		CachedASC->CancelAllAbilities();
	}

	// 2. 락온 해제는 반드시 MulticastHandleDeathCosmetic 보다 먼저 한다.
	// 왜: UVBTargetLockComponent 는 tick 마다 SetControlRotation + 카메라 Lerp 로 컨트롤 회전을 덮어쓴다.
	//   락온이 살아 있으면 사망 카메라가 매 프레임 적 조준으로 되돌려져 쓰러지는 내 몸을 못 비춘다.
	//   이 순서 자체가 유일한 보장 수단이라 코드 순서로 못박는다.
	// 전제: 서버 권위 경로 — ClearLockOn 은 서버면 즉시 처리, 클라면 RPC 라우팅(여기선 항상 서버).
	if (TargetLockComponent)
	{
		TargetLockComponent->ClearLockOn();
	}

	// 3. 나를 노리던 적 AI 의 타겟 해제. 지각 시스템은 사망을 모르므로(시체도 계속 보인다) 이걸 안 하면
	//    적이 시체를 계속 때린다. 데미지는 공격 GA 가 State.Dead 대상을 걸러 0이지만 연출이 남는다.
	AVBAIController::NotifyActorDied(this, this);

	// 4. 전투 이탈 타이머 정지. 게임오버 화면이 pause 를 하지 않으므로 방치하는 동안에도 계속 돌고,
	//    만료되면 시체가 Unarmed 로 전이하며 손에 수납 VFX 를 터뜨린다(FIND-072).
	if (WeaponStateComponent)
	{
		WeaponStateComponent->StopCombatTimer();
	}

	// 5. 사망 애니/입력차단을 모든 Net Role 에 동시 적용.
	MulticastHandleDeathCosmetic();
}

void AVBCharacter::MulticastHandleDeathCosmetic_Implementation()
{
	// 적(AVBEnemyBase)은 여기서 원격 클라에 State_Dead 를 로컬 보완하지만 여기선 하지 않는다.
	// 근거: CountToOwner 는 태그를 모든 연결에 복제한다(카운트만 소유자 한정) — 즉 보완이 애초에 불필요하다.
	//   "owning connection 이 있어서"가 아니다. 그 서술은 2026-07-29 에 엔진 정의로 반증됐다.
	// 게다가 플레이어의 State.Dead 를 읽는 소비처는 전부 서버에서 돈다(AI 지각·공격 트레이스·넉백 억제).
	//   AVBCharacter 는 IVBTargetable 을 구현하지 않아 락온 후보도 아니다.
	// 클라에서 도는 시각 필터(시체 아웃라인 등)를 추가하거나 IVBTargetable 을 구현하게 되면 재검토할 것.
	HandleDeathCosmetic();
}

UAnimMontage* AVBCharacter::ResolveDeathMontage() const
{
	// 무기별 몽타주가 있으면 그것, 없으면 기본 몽타주. 가드와 달리 폴백이 필수다 -
	// 어떤 무기를 들었든 시체는 반드시 쓰러져야 하고, 폴백이 없으면 선 채로 굳는다.
	if (WeaponStateComponent)
	{
		const EVBWeaponType CurrentWeapon = WeaponStateComponent->GetCurrentWeaponType();
		if (const TObjectPtr<UAnimMontage>* Found = WeaponDeathMontages.Find(CurrentWeapon))
		{
			if (*Found)
			{
				return *Found;
			}
		}
	}
	return DeathMontage;
}

void AVBCharacter::HandleDeathCosmetic()
{
	if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(GetCharacterMovement()))
		SlideCMC->CancelSlide();
	if (SmartObjectInteraction) SmartObjectInteraction->AbortInteraction();
	if (PlayerRagdollComponent) PlayerRagdollComponent->CancelRagdoll();
	SetWantsToAim(false);
	// 1. 시체가 폰끼리 밀고 밀리지 않게 한다. 단 월드 콜리전은 남긴다.
	// SetCollisionEnabled(NoCollision) 을 쓰지 말 것. 그건 래그돌 시절 유산이다(적은 래그돌 메쉬가 물리를
	//   넘겨받아 캡슐이 필요 없었다). 애니메이션 사망에서는 캡슐이 시체를 떠받치는 유일한 지지대라,
	//   통째로 끄면 바닥을 못 찾아 공중에서 죽거나 지면 아래로 빠진다.
	// Pawn 채널만 무시하면 적은 시체를 통과하면서도 시체는 바닥에 정상적으로 착지한다.
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	// 2. 사망 애니메이션 재생.
	// 래그돌을 쓰지 않는 이유(2026-07-28 user 판정): 물리는 정상 작동했으나(진단 실측 simulating=1,
	//   bodies=23) PA_UEFN_Mannequin 의 관절이 끝까지 풀리지 않아 뻣뻣한 자세로 정착했다. 물리 에셋을
	//   튜닝하는 대신 저작된 사망 클립을 쓴다 — GoW 류 AAA 의 표준 선택이고 연출 통제권이 확실하다.
	// PlayAnimation 이 아니라 몽타주인 이유(2026-07-29 전환): 루트모션이 캡슐에 전달되려면 몽타주여야 한다.
	//   RootMotionMode 기본값이 RootMotionFromMontagesOnly(AnimInstance.cpp::UAnimInstance 생성자)라 AnimSequence 를
	//   PlayAnimation 으로 틀면 루트 변위가 소비되지 않고 포즈로만 적용돼 몸만 캡슐 밖으로 미끄러진다
	//   (실측 카타나 220cm / 대검 230cm). 세그먼트 클립의 bEnableRootMotion 도 true 여야 한다.
	// 종전 우려였던 "몽타주는 끝나면 블렌드 아웃돼 시체가 다시 일어선다" 는 Loop 섹션 자기링크로 해소된다 —
	//   auto blend out 은 NextSectionIndex == INDEX_NONE 일 때만 발화하므로(AnimMontage.cpp::FAnimMontageInstance::Advance)
	//   자기루프면 그 조건에 영원히 도달하지 않는다. Loop 클립은 root in-place 라 미끄러지지도 않는다.
	// 대가: SingleNode 전환이 사라져 메인 ABP 가 계속 돈다. 그래서 ①TIP 이 사망 몽타주를 끊지 않도록
	//   UVBAnimInstance::UpdateTurnInPlaceState 에 사망 게이트를 넣었고 ②메인 ABP LegIK 가 사망 클립에
	//   걸리므로 다리가 뒤틀리면 bake_ik_to_fk 가 필요하다(가드 클립 전례).
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (MeshComp)
	{
		UAnimInstance* AnimInst = MeshComp->GetAnimInstance();
		UAnimMontage* DeathMont = ResolveDeathMontage();
		if (AnimInst && DeathMont)
		{
			// 몽타주로 재생해야 루트모션이 캡슐에 전달된다. RootMotionMode 기본값이
			// RootMotionFromMontagesOnly 라 AnimSequence 를 PlayAnimation 으로 틀면 루트 변위가
			// 포즈로만 적용돼 몸만 캡슐 밖으로 미끄러진다(엔진 AnimInstance.cpp::UAnimInstance 생성자(RootMotionMode 기본값) + ::Montage_PlayInternal).
			if (AnimInst->Montage_Play(DeathMont) > 0.0f)
			{
				// 마지막 섹션을 자기 자신으로 링크해 시체 자세를 영구 유지한다.
				// 엔진 근거: auto blend out 은 NextSectionIndex == INDEX_NONE 일 때만 발화한다
				// (AnimMontage.cpp::FAnimMontageInstance::Advance). 자기루프면 그 조건에 영원히 도달하지 않는다.
				// Loop 섹션 클립은 root in-place 라서 무한 반복해도 시체가 미끄러지지 않는다.
				// 첫 섹션 -> Loop -> Loop 로 사슬을 런타임에 강제한다.
				// 에셋의 next section 이 비어 있으면 첫 섹션 끝에서 몽타주가 종료되고
				// 블렌드 아웃으로 로코모션이 포즈를 되찾아 시체가 일어선다(2026-07-29 실측).
				if (DeathMont->GetNumSections() > 0)
				{
					const FName FirstSection = DeathMont->GetSectionName(0);
					if (FirstSection != DeathLoopSection)
					{
						AnimInst->Montage_SetNextSection(FirstSection, DeathLoopSection, DeathMont);
					}
				}
				AnimInst->Montage_SetNextSection(DeathLoopSection, DeathLoopSection, DeathMont);
			}
			else
			{
				VB_LOG(Warning, "[VBChar] 사망 몽타주 재생 실패(0 반환): %s", *DeathMont->GetName());
			}
		}
		else
		{
			// 조용한 실패 금지. 미할당이면 시체가 선 채로 굳어 원인이 안 보인다.
			VB_LOG(Warning, "[VBChar] 사망 몽타주 미할당 — 사망 자세가 재생되지 않습니다: %s", *GetName());
		}
	}

	// 3. 속도만 0 으로 만들고 이동 모드는 살려 둔다.
	// DisableMovement 를 쓰지 말 것. MOVE_None 은 시체를 있던 자리에 얼린다 — 공중에서 죽으면 공중에
	//   뜬 채로 사망 애니가 재생된다(2026-07-29 user 실측). 중력이 필요하다.
	// StopMovementImmediately 로 속도와 가속을 지우면, 입력은 이미 DisableInput 으로 죽어 있으므로
	//   CMC 가 하는 일은 중력으로 시체를 착지시키는 것뿐이다. 착지 후에는 가속이 0 이라 표류하지 않는다.
	// 이 블록이 사망 몽타주 재생 뒤에 있는 이유: Montage_Play 는 기본값 bStopAllMontages=true 라
	//   진행 중이던 몽타주를 끊고 그 종료 델리게이트가 같은 스택에서 즉시 발화하는데(AnimInstance.cpp::UAnimInstance::QueueMontageBlendingOutEvent),
	//   그 콜백인 UVBTraversalComponent::RecoverFromTraversal 이 SetMovementMode 로 이동 모드를 바꾼다
	//   (VBTraversalComponent.cpp:578). 그게 남긴 속도를 여기서 마지막으로 지운다.
	//   개별 컴포넌트에 사망 가드를 심는 방식은 규율 의존형 방어라 채택하지 않았다(FIND-071 과 같은 판단).
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();

		// 자동 회전도 끈다. 이동 모드를 살려 두면 CMC 가 계속 도는데, strafe 기본값
		// (bUseControllerDesiredRotation=true)이면 사망 순간 액터 회전과 컨트롤 회전이 어긋나 있을 때
		// 시체가 그쪽으로 천천히 돌아간다. 시체는 죽은 자세 그대로 있어야 한다.
		MoveComp->bUseControllerDesiredRotation = false;
		MoveComp->bOrientRotationToMovement     = false;
	}

	// 4. 로컬 조종 중이면 입력 차단 + PC 연출 진입(슬로모/HUD/게임오버 화면).
	// 카메라는 건드리지 않는다. 래그돌을 버리면서 시체가 캡슐 위치에 그대로 쓰러지므로, 스프링암을
	//   pelvis 로 옮길 이유가 사라졌다(옮기면 오히려 사망 애니의 골반 움직임을 따라 화면이 출렁인다).
	if (IsLocallyControlled())
	{
		if (AVBPlayerController* VBPC = Cast<AVBPlayerController>(GetController()))
		{
			// AActor::DisableInput 은 이 폰의 InputComponent 를 PC 입력 스택에서 pop 한다 → 폰이 바인드한
			//   Enhanced Input 전량(이동/시점/점프/무기슬롯 1·2·3/락온)이 한 번에 죽는다.
			// GA 게이트만으로는 부족한 이유: HandleWeaponSlot 은 GA 를 안 거치고 WSC->SummonWeapon 직행이고
			//   (FIND-063) HandleLockOnPressed/Move/Look/Jump 도 GA 밖이다. 시체가 락온하면 TargetLock tick 이
			//   컨트롤 회전을 매 프레임 덮어써 사망 카메라가 적을 계속 조준한다.
			//   GA 의 State.Dead 게이트(FIND-067)는 이중화로 유지.
			// 이것만으로는 PC 가 바인드한 액션이 안 죽는다 — AVBPlayerController::OnPlayerDeath 가
			//   DisableInput(this) 로 그쪽을 맡고 bGameOverActive 가 이중화한다(FIND-071).
			// 비가역 주의: 같은 레벨 부활을 나중에 추가하면 EnableInput(VBPC) + HUD SetVisibility(Visible) 가 필요하다.
			//   현재는 두 출구가 모두 OpenLevel(월드 재생성)이라 무해하다.
			DisableInput(VBPC);
			// 사망 클립 길이를 넘겨 게임오버 화면이 '애니가 끝난 뒤'에 뜨게 한다(실시간 고정 지연이 아님).
			// 화면 지연은 '쓰러지는 동작이 끝난 시점' 이므로 몽타주 전체 길이가 아니라 첫 섹션 길이다.
			// 전체를 넘기면 Loop 섹션까지 더해져 화면이 늦게 뜬다.
			float DieSeconds = 0.0f;
			if (const UAnimMontage* Mont = ResolveDeathMontage())
			{
				const int32 LoopIdx = Mont->GetSectionIndex(DeathLoopSection);
				DieSeconds = (LoopIdx != INDEX_NONE) ? Mont->GetSectionLength(0) : Mont->GetPlayLength();
			}
			VBPC->OnPlayerDeath(DieSeconds);
		}
	}
}

void AVBCharacter::BeginPlay()
{
	// 외곽선 머티리얼을 화면에 건다. 자산이 비면 조용히 넘어가는 것이 아니라 말한다 -
	//   「외곽선이 안 나온다」의 원인이 여기인지 스텐실 쪽인지 가려야 하기 때문이다.
	if (PostProcessComponent)
	{
		if (FinisherOutlineMaterial)
		{
			PostProcessComponent->AddOrUpdateBlendable(FinisherOutlineMaterial, 1.0f);
		}
		else
		{
			VB_LOG(Log, "%s: FinisherOutlineMaterial 미할당 - 처형 준비 외곽선이 안 나온다 "
			            "(BP 기본값에서 M_VB_FinisherOutline3 계열을 지정해라)", *GetName());
		}
	}

	Super::BeginPlay();

	// 팀이 None 이면 적 AI 에게 투명해진다(지각이 중립을 안 본다) - 증상은 'AI 가 안 쫓아온다' 하나뿐이라
	//   원인을 찾기 어렵다. 배선 실수를 조용히 넘기지 않는다.
	if (Team == EVBTeam::None)
	{
		VB_LOG(Warning, "%s: Team 이 None - 적 AI 가 이 캐릭터를 감지하지 못한다. BP 의 Team 확인", *GetName());
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->SetControlRotation(GetActorRotation());
	}

	// WSC.OnLocomotionStateChanged 구독 — LocomotionState 전환 시 CMC 속도 세트 갱신.
	// 서버와 클라 양쪽 모두 등록해야 각 인스턴스의 CMC가 올바른 속도로 전환된다.
	if (WeaponStateComponent)
	{
		WeaponStateComponent->OnLocomotionStateChanged.AddDynamic(this, &AVBCharacter::OnLocomotionStateChanged);
	}

	// TargetLock on/off 시 Strafe/Free 전환 트리거.
	if (TargetLockComponent)
	{
		TargetLockComponent->OnTargetLocked.AddDynamic(this, &AVBCharacter::OnTargetLocked);
		TargetLockComponent->OnTargetUnlocked.AddDynamic(this, &AVBCharacter::OnTargetUnlocked);
	}

	// CMC 물리 baseline 을 MovementConfig(배선 자산 or CDO)로부터 적용. 생성자 대신 여기서 — 자산 배선 반영.
	ApplyMovementConfigToCMC();

	// 초기 회전 모드 적용 (생성자의 Free 기본값 위에 LocomotionState/LockedOn 정책 덧씌움).
	ApplyRotationMode();

	// 초기 RotationRate 설정 — 지면 기본값(-1) 을 명시적으로 적용.
	UpdateRotationRateForMovementMode();
}

const UVBMovementConfig* AVBCharacter::GetMovementConfig() const
{
	// P0 config 패턴 동형(VBTargetLockComponent.cpp): 배선 자산 우선, 미할당이면 CDO 디폴트(현행값).
	return MovementConfig ? MovementConfig : GetDefault<UVBMovementConfig>();
}

const UVBTurnInPlaceConfig* AVBCharacter::GetTurnInPlaceConfig() const
{
	return TurnInPlaceConfig ? TurnInPlaceConfig : GetDefault<UVBTurnInPlaceConfig>();
}

const UVBCameraConfig* AVBCharacter::GetCameraConfig() const
{
	return CameraConfig ? CameraConfig : GetDefault<UVBCameraConfig>();
}

const UVBParryConfig* AVBCharacter::GetParryConfig() const
{
	return ParryConfig ? ParryConfig : GetDefault<UVBParryConfig>();
}

// 반격 창을 연다. 창은 State.Combat.RiposteWindow 태그로 표현되고 UVBGA_Riposte 가 그것을 요구한다.
// 왜 태그인가: 경공격이 State.Combat.Guarding 으로 이미 차단돼 있어(VBGA_LightAttack.cpp:30),
//   두 능력이 같은 입력을 써도 활성 조건이 겹치지 않는다. 입력 경합을 코드로 푸는 대신 구조로 없앤다.
void AVBCharacter::OpenRiposteWindow(float Duration, AActor* Attacker)
{
	if (!HasAuthority() || !CachedASC.IsValid() || Duration <= 0.0f)
	{
		return;
	}

	// 연속 패링 시 창이 겹치지 않게 항상 갱신한다. 태그를 중복으로 쌓으면
	// 먼저 만료된 타이머가 아직 유효한 창을 지운다.
	GetWorldTimerManager().ClearTimer(RiposteWindowHandle);
	if (!CachedASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_RiposteWindow))
	{
		CachedASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow, 1,
		                               EGameplayTagReplicationState::CountToOwner);
	}

	// 패링 성립 순간 공격자로 자동 락온 (user 요구 "패링 하는 순간 적한테 락온").
	// 서버에서 직접 거는 이유: 대상이 이미 확정된 액터라 카메라 탐색이 필요 없다.
	//   클라-카메라-우선 계약은 탐색에만 걸린다 - 히트 자동락온과 같은 경로다.
	// 플래그는 UVBParryConfig 가 소유한다(2026-08-09 이관) - 패링 튜닝이 자산 두 개로 갈라져 있었다.
	// GetParryConfig() 는 미할당 시 CDO 를 돌려주므로 null 폴백을 두지 않는다.
	if (GetParryConfig()->bAutoLockOnParry && TargetLockComponent)
	{
		TargetLockComponent->TryAutoLockOnParryTarget(Attacker);
	}

	GetWorldTimerManager().SetTimer(
		RiposteWindowHandle,
		FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (CachedASC.IsValid())
			{
				CachedASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
			}
		}),
		Duration, false);
}

// 가드 결과에 따른 스태미나 증감. 서버 권위 — 어트리뷰트 복제가 클라를 따라온다.
// GE 가 아닌 이유: 능력 코스트(CommitAbility)가 아니라 피격에 대한 반응이다.
void AVBCharacter::ApplyGuardStaminaDelta(float Delta)
{
	if (!HasAuthority() || !CachedASC.IsValid() || FMath::IsNearlyZero(Delta))
	{
		return;
	}

	const float Current = CachedASC->GetNumericAttribute(UVBCombatAttributeSet::GetStaminaAttribute());
	const float Max     = CachedASC->GetNumericAttribute(UVBCombatAttributeSet::GetMaxStaminaAttribute());
	const float Next    = FMath::Clamp(Current + Delta, 0.0f, Max);

	CachedASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetStaminaAttribute(), Next);
}

void AVBCharacter::BeginGuard()
{
	// GuardStartServerTime 은 데미지 hub 가 읽는 서버 진실이라 권한에서만 쓴다. 지금 유일한 호출자
	// (UVBGA_Guard::ExecuteAbility)가 IsNetAuthority 로 감싸고 있어 이 가드는 중복이지만,
	// public 함수가 규율에만 기대는 것은 결함이라는 판단을 VBAIController.cpp 에서 이미 내렸다(FIND-071).
	if (!HasAuthority())
	{
		return;
	}

	// 멱등 보장: 이미 가드 중이면(GA 재활성/중복 호출) 재시작하지 않는다 — 몽타주가 Start 로 되돌아가 Loop 못 감,
	//  그리고 GuardStartServerTime 리셋으로 패링 윈도우가 매번 갓 시작이 돼 블록이 영영 안 뜨는 문제(06-26 로그 실증) 방지.
	if (IsGuarding())
	{
		return;
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// ── 가드 세트 확정 + 캐시 ──
	// 진입 시점의 무기로 몽타주·섹션명을 한 번만 해석하고 고정한다.
	// 캐시는 반드시 락아웃 조기 반환보다 먼저 한다: 탭 패링(락아웃 중 재진입) 경로에서도 PlayGuardAccept 는
	//  받아넘김 몽타주를 필요로 한다. 락아웃 뒤에 캐시하면 그 경로에서 Accept 가 통째로 사라져 현행이 회귀한다.
	// GetParryConfig() 는 null 을 반환하지 않는다(미할당 시 CDO = IVBGuardable 계약 1). FIND-056① 방침대로
	//  여기서도 폴백을 만들지 않는다.
	const UVBParryConfig* Cfg = GetParryConfig();
	const EVBWeaponType CurrentWeapon =
		WeaponStateComponent ? WeaponStateComponent->GetCurrentWeaponType() : EVBWeaponType::None;
	const FVBGuardSet* Set = Cfg->FindGuardSet(CurrentWeapon);

	// 미등록 무기면 권위 상태를 커밋하지 않고 반환한다 (2026-07-26 2차 verify 정정).
	//  종전엔 GuardStartServerTime 을 먼저 세팅한 뒤 캐시만 null 로 두었는데, 그러면 IsGuarding()==true 인 채
	//  몽타주만 없는 상태가 된다. BeginGuard 는 public 이고 GA 밖 호출을 상정한 방어인데 그 경로엔 종료시킬
	//  GA 인스턴스가 없어 가드가 영구 고착되고 데미지 hub 가 영원히 블록/패링을 부여한다.
	//  ("몽타주 없이도 가드 성립"은 세트가 등록돼 있고 GuardMontage 만 빈 경우의 이야기다 — 그건 아래에서 처리한다.)
	if (!Set)
	{
		return;
	}

	// 여기부터 가드 성립. 패링 윈도우 판정의 기준시각을 이 시점에 커밋한다(서버 권위 — VBGA_Guard 가 보장).
	GuardStartServerTime = Now;

	ActiveGuardMontage       = Set->GuardMontage;
	ActiveParryAcceptMontage = Set->ParryAcceptMontage;
	ActiveGuardEndSection    = Set->GuardEndSection;

	// Accept 락아웃: 방금 받아넘김(Accept+End)을 재생 중이면 가드 몽타주를 재생하지 않는다 — 탭 패링 재입력이 Accept 를 자르지 않게.
	//  가드 상태(GuardStartServerTime)는 위에서 이미 세팅됐으니 패링/블록 판정엔 지장 없다(06-26 로그로 확정된 탭-패링 컷 방지).
	if (Now < GuardMontageLockUntil)
	{
		return;
	}

	// 몽타주가 없어도 가드는 성립한다(기존 설계 — 판정은 정상이고 화면만 없다).
	if (!ActiveGuardMontage)
	{
		return;
	}

	UAnimInstance* AI = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AI)
	{
		return;
	}

	// 재생 실패도 무음이다 (2026-07-26 2차 verify): Montage_Play 는 실패 시 0 을 돌려주고 로그를 안 남긴다.
	//  실패하면 아래 Montage_SetNextSection 3건이 전부 no-op 이 되고, GuardStartServerTime 은 이미 위에서
	//  커밋됐으므로 애니메이션 없는 권위 가드 상태가 된다(판정은 되는데 화면엔 아무것도 없음).
	//  가드 자체는 몽타주 없이도 성립하는 설계라(GuardMontage 미할당 허용) 여기서 중단하지 않고 경고만 남긴다 —
	//  "몽타주를 걸어놨는데 안 나온다"와 "애초에 안 걸었다"를 로그로 구분할 수 있게 하는 게 목적.
	if (AI->Montage_Play(ActiveGuardMontage) <= 0.0f)
	{
		VB_LOG(Warning, "Guard montage '%s' failed to play (returned 0) - guard state is authoritative but has no animation.",
			   *GetNameSafe(ActiveGuardMontage));
	}

	// 섹션명은 몽타주와 같은 FVBGuardSet 에서 읽는다 — "A 무기 몽타주 + B 무기 섹션명" 교차가 구조적으로 불가능.
	//  (섹션 존재 검증은 UVBParryConfig::IsDataValid 로 이관 — CHORE-031. 옛 런타임 래치는 첫 무기에서 닫혀
	//   두 번째 세트를 영영 못 봤다: 인스턴스 스코프 래치 ⊥ 에셋 스코프 계약.)
	// 홀드 가드 무한 유지: Start 진입 → Loop(자기루프).
	//  몽타주 에셋에 self-loop 를 굽는 python API 가 없어(get_num_sections 등 getter 만) 런타임에 섹션 링크를 건다.
	//  Start 재생 후 Loop 로 넘어가 EndGuard 의 End 점프까지 무한 반복.
	AI->Montage_SetNextSection(Set->GuardStartSection, Set->GuardLoopSection, ActiveGuardMontage);
	AI->Montage_SetNextSection(Set->GuardLoopSection,  Set->GuardLoopSection, ActiveGuardMontage);
	// (구 "Accept"→"Loop" 링크는 삭제됨, FIND-056② — 아무도 그 섹션으로 점프하지 않는 죽은 배선이었다.
	//  받아넘김은 별도 몽타주 ParryAcceptMontage 를 덮어 재생하는 쪽으로 확정(PlayGuardAccept, 06-26 PIE).
	//  인-몽타주 방식으로 되돌린다면 그때 FVBGuardSet 에 GuardAcceptSection 필드를 함께 신설한다.)
	// End 는 반드시 next=None 이어야 재생 후 자연 블렌드아웃된다. 명시하지 않으면 엔진 기본 링크로 Loop 로
	//  되돌아가 가드가 안 풀린다. EndGuard 가 이 섹션으로 점프해 "물러난 만큼 복귀"(root +18) 를 재생한다.
	AI->Montage_SetNextSection(ActiveGuardEndSection, NAME_None, ActiveGuardMontage);
}

void AVBCharacter::EndGuard()
{
	// BeginGuard 와 같은 이유의 권한 가드. 클라에서 GuardStartServerTime 을 -1 로 만들면 서버 진실과 갈라진다.
	// 2026-08-17 정정 - 종전 주석의 "몽타주 End 점프는 서버에서 나가고 복제로 클라에 도달한다"는 거짓이다.
	//   ACharacter 에는 몽타주를 실어 나르는 복제 프로퍼티가 없다(Character.h 에 ReplicatedAnimMontage 0건).
	//   유일한 통로인 RepRootMotion 은 COND_SimulatedOnly 인 데다 활성 조건이
	//   IsPlayingNetworkedRootMotionMontage 라, 소유 클라는 구조적으로 못 받고 비-루트모션 구간은 sim proxy 도 못 받는다.
	//   여기 가드/패링 몽타주는 ASC 경로가 아니라 raw UAnimInstance 호출이라 그 경로조차 안 탄다.
	//   따라서 MP 에서 소유 클라는 가드 연출을 보지 못한다(판정은 서버 진실이라 게임플레이는 정상 = 연출 발산).
	//   고칠 때의 정본은 이 코드베이스가 이미 가진 MulticastJumpToMontageSection 문법이다.
	if (!HasAuthority())
	{
		return;
	}

	GuardStartServerTime = -1.0f;

	// config 를 다시 조회하지 않는다. 진입 때 캐시한 몽타주만 본다.
	//  가드 중 무기가 바뀌었어도 진입 때 재생한 그 몽타주의 End 로 점프해야 백스텝이 복귀된다.
	//  '지금 무기'로 재해석하면 Montage_IsActive 가 false 가 돼 End 점프가 통째로 스킵되고,
	//  교체할 때마다 물러난 채 누적된다(user 06-26 "복귀 안되던데" 의 재발 경로).
	if (ActiveGuardMontage)
	{
		if (UAnimInstance* AI = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
		{
			// Montage_Stop 이 아니라 "End" 섹션 점프다. 가드 진입 때 Start 의 루트모션으로 −18cm 물러났으므로,
			//  해제 시 End(+18cm 복귀)를 반드시 재생해야 제자리로 돌아온다. 그냥 Stop 하면 물러난 채 남아
			//  가드할 때마다 뒤로 누적된다. End 는 next=None 이라 재생 후 자연 블렌드아웃.
			//  가드 몽타주가 이미 꺼졌으면(패링 시 ParryAccept 가 덮음) 점프는 무시 — 그 경로는 ParryAccept 가
			//  자체 End 세그먼트로 복귀를 이미 포함한다.
			if (AI->Montage_IsActive(ActiveGuardMontage))
			{
				// 섹션명 = 몽타주와 같은 세트에서 캐시한 값. BeginGuard 가 건 next=None 링크와 짝이 깨질 수 없다.
				AI->Montage_JumpToSection(ActiveGuardEndSection, ActiveGuardMontage);
			}
		}
	}

	// 캐시 해제 — 남겨두면 ①불필요한 GC 참조 ②"가드 중이 아닌데 활성 가드 몽타주가 있다"는 모순 상태.
	//  PlayGuardAccept 는 hub 가 IsGuarding() 확인 후에만 부르므로 여기서 지워도 받아넘김이 유실되지 않는다.
	ActiveGuardMontage = nullptr;
	ActiveParryAcceptMontage = nullptr;
	ActiveGuardEndSection = NAME_None;
}

float AVBCharacter::GetGuardElapsed() const
{
	if (GuardStartServerTime < 0.0f || !GetWorld()) return TNumericLimits<float>::Max();
	return GetWorld()->GetTimeSeconds() - GuardStartServerTime;
}

void AVBCharacter::PlayGuardAccept()
{
	// 받아넘김 = ParryAcceptMontage(Accept→End, 전신 DefaultSlot)를 재생한다. 가드 Start 를 덮어 깔끔한 받아넘김+복귀 연출.
	//  두 컷 방지책(둘 다 06-26 PIE 로그로 확정한 탭-패링 증상):
	//   ① 몽타주 블렌드인을 0.06s 로 짧게(에셋) — 0.25s 기본이면 짧은 Accept 클립이 페이드인만 하다 끝나 '건너뛴' 것처럼 보였다.
	//   ② 재생 길이만큼 GuardMontageLockUntil 을 걸어 BeginGuard 의 가드 Start 재생을 잠근다 — 다음 탭이 Accept 를 덮지 않게.
	//  EndGuard 는 가드 몽타주가 활성일 때만 그 몽타주의 "End" 섹션으로 점프하므로, RMB 를 떼도 이 별도 몽타주는 안 잘린다
	//  (이 경로의 복귀 루트모션은 ParryAcceptMontage 자체의 End 세그먼트가 담당한다).
	// 캐시를 쓴다: 가드 진입 시점 무기의 받아넘김이다. 교체했다면 새 무기를 든 채 옛 받아넘김이 나오지만
	//  판정은 정상이고, 무엇보다 지금 재생 중인 가드 포즈와 짝이 맞는다. '지금 무기'로 재해석하면
	//  가드 포즈와 받아넘김이 서로 다른 무기가 돼 더 크게 어긋난다.
	if (!ActiveParryAcceptMontage)
	{
		return;
	}

	if (UAnimInstance* AI = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
	{
		const float L = AI->Montage_Play(ActiveParryAcceptMontage);
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		// 하한을 config 로 이관(FIND-056③) — Montage_Play 가 0(재생 실패)을 돌려줘도 락아웃이 즉시
		//  풀려 다음 탭이 Accept 를 자르는 것을 막는 안전장치이자, 탭 패링 체감 튜닝 노브다.
		//  하한은 root 필드다. 소비처가 데미지 hub 라 무기별로 쪼개지 않는다(헤더 클래스 주석).
		GuardMontageLockUntil = Now + FMath::Max(L, GetParryConfig()->MinGuardAcceptLockout);
	}
}

void AVBCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Existing Blueprint templates may still serialize the old capsule attachment.
	if (GameplayCameraComponent && GetMesh() && GameplayCameraComponent->GetAttachParent() != GetMesh())
	{
		GameplayCameraComponent->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	// BeginPlay 가 아니라 여기여야 한다. AVBCharacter::BeginPlay 는 Super::BeginPlay() 로 컴포넌트를 먼저 시작시키는데,
	// VBTargetLockComponent::BeginPlay 가 그 시점에 SpringArm/Camera 의 "평상시 값"을 캡처해 전투 카메라 Lerp 의
	// 시작점으로 쓴다. BeginPlay 에서 적용하면 락온이 이미 생성자 상수를 캐시한 뒤라, 전투→평상 복귀가
	// config 값이 아닌 옛 상수로 돌아간다. PostInitializeComponents 는 BP 배선 해석 후·BeginPlay 전이라 안전하다.
	ApplyCameraConfig();
}

void AVBCharacter::ApplyCameraConfig()
{
	const UVBCameraConfig* Cfg = GetCameraConfig();
	if (!Cfg) return; // GetDefault 는 null 을 주지 않지만, CDO 미로드 엣지에서 카메라를 0 으로 만들지 않도록 가드.

	if (SpringArmComponent)
	{
		SpringArmComponent->TargetArmLength      = Cfg->TargetArmLength;
		SpringArmComponent->SocketOffset         = Cfg->SocketOffset;
		SpringArmComponent->CameraLagSpeed       = Cfg->CameraLagSpeed;
		SpringArmComponent->CameraLagMaxDistance = Cfg->CameraLagMaxDistance;
	}
	if (CameraComponent)
	{
		CameraComponent->SetFieldOfView(Cfg->FieldOfView);
	}
}

void AVBCharacter::ApplyMovementConfigToCMC()
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC) return;

	// 미배선이면 strafe 방향별 속도 매핑이 통째로 0 존으로 떨어져 8방향 이동이 전부 같은 속도가 된다.
	// 소비처(UpdateMaxSpeed)는 매 프레임이라 거기서 경고하면 스팸이 되므로 BeginPlay 경유인 여기서 1회만 알린다.
	if (!StrafeSpeedMapCurve)
	{
		VB_LOG(Warning, "StrafeSpeedMapCurve 미배선 - strafe 방향별 속도 구분이 사라진다");
	}

	const UVBMovementConfig* Cfg = GetMovementConfig();
	CMC->MaxAcceleration            = Cfg->MaxAcceleration;
	CMC->BrakingDecelerationWalking = Cfg->BrakingDecelerationWalking;
	CMC->GroundFriction             = Cfg->GroundFriction;
	CMC->BrakingFrictionFactor      = Cfg->BrakingFrictionFactor;
	CMC->JumpZVelocity              = Cfg->JumpZVelocity;
	CMC->AirControl                 = Cfg->AirControl;
	CMC->MaxWalkSpeedCrouched       = Cfg->NormalCrouchMaxWalkSpeed; // tick 이 무장 시 프로파일 CrouchSpeed 로 덮음
	CMC->RotationRate               = Cfg->DefaultRotationRate;      // tick 이 매 프레임 갱신(baseline 만 확정)
	if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(CMC))
	{
		SlideCMC->SlideStartSpeed = Cfg->SlideStartSpeed;
		SlideCMC->SlideEndSpeed = Cfg->SlideEndSpeed;
		SlideCMC->SlideMaxDuration = Cfg->SlideMaxDuration;
		SlideCMC->SlideSettings = Cfg->SlideSettings;
		SlideCMC->SlideMaxSpeed = Cfg->SlideMaxSpeed;
		VB_CLOG(Cfg->SlideEndSpeed >= Cfg->SlideStartSpeed, Warning,
			"이동 설정 %s: SlideEndSpeed(%.0f) >= SlideStartSpeed(%.0f) - 슬라이드를 시작하지 않는다", *GetNameSafe(Cfg), Cfg->SlideEndSpeed, Cfg->SlideStartSpeed);
	}

	// bAllowPhysicsRotationDuringAnimRootMotion 은 여기서 세우지 않는다 — 워프 타겟 유무에 따라
	// 매 프레임 갈리므로 Tick 이 단독으로 소유한다(근거는 그쪽 주석).
}

bool AVBCharacter::IsWarpOwningRotation() const
{
	// UpdateWarpTargetLifetime 이 끊어야 할 타겟을 이미 지웠으므로, 남아 있다는 것이 곧 주인이라는 뜻이다.
	// 종전에는 그 함수가 bDrop 을 계산하며 같은 판정을 내부에서 겸했다 - 정리와 질의를 갈라도
	// 결과가 같은 이유가 이것이다(정리 후 존재 == 존재했고 && 끊지 않았다).
	const UMotionWarpingComponent* MWC = GetMotionWarpingComponent();
	return MWC && MWC->FindWarpTarget(VBMotionWarpingNames::AttackTarget) != nullptr;
}

bool AVBCharacter::IsCapsuleRotationOwnedByFeature() const
{
	// Paired montage alignment owns facing through the whole interaction, including
	// the section after its short warp window. Camera-facing rotation would undo it.
	if (UVBFinisherComponent::IsActorInScene(this)) return true;
	if (PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive()) return true;
	if (SmartObjectInteraction && SmartObjectInteraction->OwnsCapsuleFacing()) return true;
	// 환경 상호작용의 설 자리 이동은 루트모션 소스다. 그 사이 PhysicsRotation 이 돌면 소품을 등진다.
	if (InteractorComponent && InteractorComponent->OwnsCapsuleFacing()) return true;
	// 워프: SkewWarp 의 Facing 이 캐릭터를 적 쪽으로 돌리는데 PhysicsRotation 은 컨트롤 회전(카메라) 쪽으로
	//   돌린다. 둘 다 켜면 주도권이 갈린다. 근접에서는 두 방향이 거의 같아 안 드러나지만, 멀리서 돌진하면
	//   차이가 커져 회전이 눈에 띄게 어긋난다(2026-08-03 user FEEL). 도는 동안은 워프가 유일한 주인이어야 한다.
	if (IsWarpOwningRotation())
	{
		return true;
	}

	// 트래버설(맨틀/볼트/허들): FrontLedge 워프가 캐릭터를 장애물 정면으로 돌리는 동안 PhysicsRotation 이
	//   같이 돌면 옆으로 기어오르는 그림이 된다(FIND-083). 트래버설은 MOVE_Flying 이지만 엔진 게이트는
	//   PerformMovement 안이라 이동모드와 무관하게 통과한다.
	// 워프 타겟 유무가 아니라 IsDoingTraversal 을 보는 이유: 워프 윈도우는 몽타주의 일부만 덮지만
	//   회전 소유는 몽타주 전 구간이어야 한다. 윈도우 밖 꼬리에서 다시 돌면 같은 증상이 남는다.
	if (TraversalComponent && TraversalComponent->IsDoingTraversal())
	{
		return true;
	}

	return false;
}

void AVBCharacter::UpdateWarpTargetLifetime()
{
	// 죽었거나 사라진 적을 워프 타겟으로 계속 물고 있지 않게 매 프레임 끊는다.
	// 왜 매 프레임인가: 워프 타겟은 공격 시작/콤보 창 같은 노티 시점에만 갱신된다.
	//   Fighter 처럼 노티 간격이 0.5초면 그 사이에 적이 죽어도 타겟이 그대로 남고,
	//   SkewWarp 의 Facing 이 유효하지 않은 트랜스폼을 향해 캐릭터를 돌린다
	//   (2026-08-02 실측: 적 사망 직후 캡슐이 0.1초에 114도 점프 후 0.5초 고정 - 그 0.5초가 노티 간격이다).
	// 제거만 한다 - 새 타겟 선정은 기존 경로(SetWarpTargetLockedEnemy)의 책임이다.
	if (UMotionWarpingComponent* MWC = GetMotionWarpingComponent())
	{
		if (const FMotionWarpingTarget* WT = MWC->FindWarpTarget(VBMotionWarpingNames::AttackTarget))
		{
			const USceneComponent* Comp = WT->Component.Get();
			const AActor* TargetActor    = Comp ? Comp->GetOwner() : nullptr;
			const IVBTargetable* Targetable = Cast<const IVBTargetable>(TargetActor);
			// 컴포넌트가 사라졌거나(파괴) 대상이 더 이상 타겟 자격이 없으면(사망) 끊는다.
			bool bDrop = (!Comp || !TargetActor || (Targetable && !Targetable->IsTargetable()));

			// 접근이 끝났으면 끊는다. 접근 워프의 임무는 간격을 좁히는 것이고, 좁혀진 뒤에도 붙잡고 있으면
			// sync point 가 퇴화한다 — 오프셋은 (적->플레이어) 방향으로 적 중심에서 밀어낸 점이라,
			// 플레이어가 오프셋보다 가까워지는 순간 그 점이 플레이어 뒤에 생기고 Facing 이 180도 뒤집는다
			// (2026-08-03 실측: 적각 25.3 고정인데 sync각만 25.3 -> -154.7, 캡슐이 sync각을 그대로 추종.
			//  적거리 149 < 오프셋 150 인 프레임에서 발생). 노티 시점 1회 clamp 로는 못 막는다 —
			// bFollowComponent=true 라 sync point 는 매 프레임 다시 계산되기 때문이다.
			// 오프셋을 읽지 않고 방향만 보는 이유: 오프셋은 GA 의 config 소유라 여기서 알 수 없고,
			// 알 필요도 없다. sync 가 적 쪽을 안 가리키면 그 자체로 접근이 끝났거나 퇴화한 것이다.
			if (!bDrop && Comp)
			{
				const FVector SelfLoc = GetActorLocation();
				FVector ToSync  = WT->GetLocation() - SelfLoc;            ToSync.Z  = 0.0f;
				FVector ToEnemy = Comp->GetComponentLocation() - SelfLoc; ToEnemy.Z = 0.0f;
				if (!ToEnemy.IsNearlyZero() && FVector::DotProduct(ToSync, ToEnemy) <= 0.0f)
				{
					bDrop = true;
				}
			}

			if (bDrop)
			{
				MWC->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
			}
		}
	}
}

void AVBCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// CMC PhysicsRotation 직전에도 같은 정책을 갱신한다. 회전이 보류된 프레임의 상태도 여기서 유지한다.
	UpdateRotationRateForMovementMode();

	// 방향(Forward/Strafe/Backward) + Gait/Sprint/Armed 조합으로 MaxWalkSpeed 갱신.
	// 매 tick 호출 — velocity 방향이 매 frame 변할 수 있어 단발적 setter 로는 부족.
	UpdateMaxSpeed();
	if ((PendingSlideJumpUntil > 0.f || bSlideJumpAwaitingMovement) && !IsSlideJumpSceneAllowed())
	{
		PendingSlideJumpUntil = 0.f;
		bSlideJumpAwaitingMovement = false;
		bSlideJumpReleaseRequested = false;
		bJumpInputHeld = false;
		StopJumping();
	}
	if (bSlideJumpAwaitingMovement && (bWasJumping || !bPressedJump
		|| !GetCharacterMovement() || !GetCharacterMovement()->IsMovingOnGround()))
	{
		bSlideJumpAwaitingMovement = false;
		if (bSlideJumpReleaseRequested && !bJumpInputHeld) StopJumping();
		bSlideJumpReleaseRequested = false;
	}
	if (PendingSlideJumpUntil > 0.f)
	{
		const UCharacterMovementComponent* JumpCMC = GetCharacterMovement();
		if (GetWorld()->GetTimeSeconds() >= PendingSlideJumpUntil || !JumpCMC || !JumpCMC->IsMovingOnGround())
		{
			PendingSlideJumpUntil = 0.f;
			if (bSlideJumpReleaseRequested && !bJumpInputHeld) StopJumping();
			bSlideJumpReleaseRequested = false;
		}
		else if (!bIsCrouched)
		{
			PendingSlideJumpUntil = 0.f;
			bSlideJumpAwaitingMovement = true;
			Jump();
		}
	}

	// 공격 캔슬로 버린 이동이 되살아나지 않게 누른다(StopResidualAttackMotion 이 마감 시각을 세운다).
	// 이동 입력이 들어오면 즉시 그만둔다 - 그 순간부터는 플레이어가 원하는 이동이다.
	if (ResidualMotionSuppressUntil > 0.0)
	{
		UCharacterMovementComponent* SuppressCMC = GetCharacterMovement();
		const FAnimMontageInstance* RootMotionOwner = GetRootMotionAnimMontageInstance();
		UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
		const FAnimMontageInstance* RecoveryInstance = Anim ? Anim->GetMontageInstanceForID(ResidualMotionMontageInstanceId) : nullptr;
		const bool bOwnerChanged = RootMotionOwner && RootMotionOwner->GetInstanceID() != ResidualMotionMontageInstanceId;
		const bool bDone = !SuppressCMC
			|| !RecoveryInstance || bOwnerChanged || !SuppressCMC->IsMovingOnGround() || SuppressCMC->HasRootMotionSources()
			|| GetWorld()->GetTimeSeconds() >= ResidualMotionSuppressUntil
			|| SuppressCMC->GetCurrentAcceleration().Size2D() > KINDA_SMALL_NUMBER;
		if (bDone)
		{
			ResidualMotionSuppressUntil = 0.0;
			ResidualMotionMontageInstanceId = INDEX_NONE;
			// 새 몽타주의 Task 가 설정한 배율은 이전 공격의 정리 대상이 아니다.
			if (!bOwnerChanged && GetAnimRootMotionTranslationScale() == 0.0f)
				SetAnimRootMotionTranslationScale(1.0f);
		}
		else
		{
			SetAnimRootMotionTranslationScale(0.0f);
			SuppressCMC->Velocity.X = 0.0f;
			SuppressCMC->Velocity.Y = 0.0f;
		}
	}

	// 순서가 계약이다 - 정리를 먼저 돌려야 IsWarpOwningRotation 이 "남아 있는 타겟"만 보고 판정한다.
	UpdateWarpTargetLifetime();

	// 루트모션 중 캡슐 회전 허용 여부 — 회전 소유자가 하나라도 있는 동안에는 끈다.
	// 누가 왜 주인인지는 IsCapsuleRotationOwnedByFeature 가 소유한다. 여기 적는 것은 "왜 기본이 켜짐인가"뿐이다.
	// 엔진 기본값은 false 다(CharacterMovementComponent.cpp::UCharacterMovementComponent 생성자 "Old default behavior").
	//   false 면 PhysicsRotation 이 루트모션 재생 내내 스킵된다(PerformMovement 안의 게이트가 이 플래그를 본다).
	// 왜 뒤집어 켜는가: 그 사이 락온이 컨트롤 회전을 계속 적으로 보간하므로 캡슐과 컨트롤의 격차가 쌓이고,
	//   몽타주가 끝나는 순간 RotationRate 로 한 번에 풀려 "적을 죽이면 휙 돈다"로 보인다
	//   (2026-08-02 실측 77.6도/0.43초). 격차를 만드는 건 돌진 거리다.
	// 회전이 꺼진 동안 쌓인 회전 빚은 종료 프레임에 캡슐 1프레임 스냅으로 풀리는데, 이는 OffsetRootBone 이
	//   흡수한다(2026-06-04 b4a436e + ABP rev43 — 흡수가 깨지면 "포즈 탁" 컷으로 드러난다).
	if (UCharacterMovementComponent* CMC = GetCharacterMovement())
	{
		CMC->bAllowPhysicsRotationDuringAnimRootMotion = !IsCapsuleRotationOwnedByFeature();
	}

	// GASP JustLanded clear 는 VBAnimInstance::NativePostEvaluateAnimation 에서 self-clear.
	// 이전(2026-05-09 까지): Character::Tick 즉시 clear → Tick 순서 race 로 AnimInstance 가
	// chooser 평가 시 JustLanded=false 로 보임 → Lands DB 호출 안 됨 → "Land 가 잘 안됨" 증상.
	// 수정(2026-05-10): clear 책임을 AnimInstance 로 이전. 자기 evaluate 후 clear → Lands DB 1 frame 보장.

	// 공중 맨틀(Vowbound 확장 — GASP 는 IsMovingOnGround 게이트로 공중 트래버설 없음. user 승인 2026-06-04):
	// 점프 키 홀드 중(처음부터 홀드 or 공중 재입력) 공중(Falling)이면 매 tick 전방 맨틀 체크 —
	// 벽이 범위에 들어오는 즉시 잡고 올라간다. Mantle/Climb 만 허용(bAirMantleOnly), Vault/Hurdle 제외.
	// bJumpInputHeld 는 입력 이벤트로만 set → 소유 클라에서만 true → sim proxy/서버 원격 캐릭터는 자연 차단.
	// 미발견 시 전방 capsule sweep 1회 조기 종료라 per-tick 비용 미미. 트래버설 중엔 Flying 이라 IsFalling=false.
	if (bJumpInputHeld && TraversalComponent)
	{
		if (const UCharacterMovementComponent* CMC = GetCharacterMovement(); CMC && CMC->IsFalling())
		{
			TraversalComponent->TryTraversalAction(/*bAirMantleOnly=*/true);
		}
	}
}

void AVBCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// GASP JustLanded/LandVelocity 세팅 — AnimInstance 의 Land 애니 분기에 사용.
	// JustLandedClearTime = NowSeconds + GraceDuration (0.3s GASP-default) — NativePostEvaluate 에서
	// 시각 비교로 clear. Retriggerable: 동일 grace 안에 다시 Land 하면 ClearTime 재연장.
	// JustLanded_Heavy 판정은 LandVelocity.Z 임계치 기반.
	if (UVBAnimInstance* Anim = Cast<UVBAnimInstance>(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr))
	{
		Anim->JustLanded   = true;
		Anim->LandVelocity = GetVelocity();

		if (const UWorld* World = GetWorld())
		{
			Anim->JustLandedClearTime = World->GetTimeSeconds() + Anim->JustLandedGraceDuration;
		}
	}
}

void AVBCharacter::UpdateRotationRateForMovementMode()
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC) return;

	// 무입력/legacy 무장은 유한 회전율로 제자리 회전의 남은 각을 보존한다.
	// GASP 이동 중에는 캡슐을 목표 방향으로 쓰고 OffsetRootBone/Steering이 화면 회전을 만든다.
	// 캡슐까지 늦게 돌리면 카메라 기준 W 가속과 궤적 facing이 벌어져 옆 이동으로 질의된다.
	const bool bArmedGrounded = !CMC->IsFalling()
		&& WeaponStateComponent
		&& WeaponStateComponent->IsInCombat();
	const FVector InputAcceleration = CMC->GetCurrentAcceleration();
	const bool bWantsToMove = !InputAcceleration.IsNearlyZero();
	const UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr);
	const UVBAnimInstance* AnimSettings = VBAnim;
	if (!AnimSettings && GetMesh() && GetMesh()->GetAnimClass())
	{
		// 서버/초기화 경계에서도 같은 AnimClass 설정을 사용한다. 런타임 포즈 상태는 CDO에서 읽지 않는다.
		AnimSettings = Cast<UVBAnimInstance>(GetMesh()->GetAnimClass()->GetDefaultObject());
	}
	const bool bGASPMoving = CMC->IsMovingOnGround()
		&& !FVector(InputAcceleration.X, InputAcceleration.Y, 0.f).IsNearlyZero()
		&& AnimSettings && AnimSettings->bUseGASPLocomotion
		&& !(VBAnim && (VBAnim->IsAnyMontagePlaying() || VBAnim->ShouldReleaseGASPRootOffset()))
		&& !IsPlayingRootMotion() && !CMC->HasRootMotionSources()
		&& !IsCapsuleRotationOwnedByFeature();
	const UVBMovementConfig* Cfg = GetMovementConfig();
	FRotator Target = CMC->IsFalling()
		? Cfg->AirborneRotationRate
		: (bArmedGrounded && !bGASPMoving ? Cfg->ArmedGroundedRotationRate : Cfg->DefaultRotationRate);

	// ── 제자리 회전이 회전을 소유한 동안 «캡슐을 세워 둔다» (FIND-105, 2026-08-28) ──
	// 왜: 그 조합의 회전은 루트모션 몽타주가 만든다. 캡슐이 카메라를 계속 따라가 버리면 애니가
	// 돌 각이 남지 않고, 애니와 캡슐이 같은 회전을 두 번 만들어 과회전한다(그 증상을 다섯 번 봤다).
	// 왜 «입력» 으로 판정하나: 회전 몽타주의 루트모션은 속도를 낳으므로 속도로 판정하면 회전 도중에
	// 보류가 풀린다. 이동 «의도» 가 없는 동안만 세워 두는 것이 안정적이다.
	// 소유권 판정을 여기서 다시 하지 않는다 - 애님 인스턴스가 답한다(`IsRootMotionTurnActive`).
	// 같은 판정이 두 곳에 있으면 한쪽이 낡고, 그 순간 «애니는 도는데 캡슐도 도는» 이중 회전이 된다.
	// 못 내는 요청을 만나 폴백 중이면 그 답이 거짓이 되어 캡슐이 회전을 돌려받는다.
	if (!CMC->IsFalling() && !bWantsToMove)
	{
		if (VBAnim)
		{
			if (VBAnim->IsRootMotionTurnActive())
			{
				Target.Yaw = 0.0f;
			}
		}
	}

	// 중복 대입 회피 — 매 tick PropertyAccess 호출 비용 절감.
	if (!CMC->RotationRate.Equals(Target, 0.01f))
	{
		CMC->RotationRate = Target;
	}
}

void AVBCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// 클라이언트: PlayerState 복제 후 ASC 캐시
	if (AVBPlayerState* PS = GetPlayerState<AVBPlayerState>())
	{
		CachedASC = PS->GetVBAbilitySystemComponent();
		if (CachedASC.IsValid())
		{
			CachedASC->InitAbilityActorInfo(PS, this);
			if (SmartObjectInteraction) SmartObjectInteraction->RefreshAbilityState();
			if (PlayerRagdollComponent) PlayerRagdollComponent->RefreshAbilityState();
		}
	}
}

void AVBCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	// Enhanced Input 바인딩
	if (UEnhancedInputComponent* EnhancedInput =
			Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (RagdollAction)
			EnhancedInput->BindAction(RagdollAction, ETriggerEvent::Started, this, &AVBCharacter::HandleRagdollPressed);
		if (AimAction)
		{
			EnhancedInput->BindAction(AimAction, ETriggerEvent::Started, this, &AVBCharacter::HandleAimPressed);
			EnhancedInput->BindAction(AimAction, ETriggerEvent::Completed, this, &AVBCharacter::HandleAimReleased);
			EnhancedInput->BindAction(AimAction, ETriggerEvent::Canceled, this, &AVBCharacter::HandleAimReleased);
		}
		// 이동
		if (MoveAction)
		{
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AVBCharacter::HandleMove);
			// 뗀 순간과 취소된 순간 둘 다 묶는다. 매달림이 마지막 입력값을 상태로 들고 있어서
			//   누가 0 을 알려주지 않으면 키를 떼도 계속 옆걸음한다.
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Completed, this, &AVBCharacter::HandleMoveCompleted);
			EnhancedInput->BindAction(MoveAction, ETriggerEvent::Canceled, this, &AVBCharacter::HandleMoveCompleted);
		}

		// 마우스 룩
		if (LookAction)
		{
			EnhancedInput->BindAction(LookAction, ETriggerEvent::Triggered, this, &AVBCharacter::HandleLook);
		}

		// 점프
		if (JumpAction)
		{
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Started, this, &AVBCharacter::HandleJumpPressed);
			EnhancedInput->BindAction(JumpAction, ETriggerEvent::Completed, this, &AVBCharacter::HandleJumpReleased);
		}

		if (AttackLightAction)
		{
			EnhancedInput->BindAction(AttackLightAction, ETriggerEvent::Started, this,
			                          &AVBCharacter::HandleAttackLight);
		}

		// DEC-009: RMB(AttackHeavyAction) 를 강공격에서 가드/패링으로 재배선했다 (홀드 패턴 = Sprint 동형).
		//   Started=누름(가드 시작), Completed=뗌(가드 종료). 강공격은 DEC-009 phase-1b 에서 완전 제거됐고
		//   IA 에셋명(IA_Attack_Heavy)만 재사용한다 — 에셋 rename 은 참조 깨짐 위험 대비 이득이 없어 이름만 잔존.
		if (AttackHeavyAction)
		{
			EnhancedInput->BindAction(AttackHeavyAction, ETriggerEvent::Started, this,
			                          &AVBCharacter::HandleGuardPressed);
			EnhancedInput->BindAction(AttackHeavyAction, ETriggerEvent::Completed, this,
			                          &AVBCharacter::HandleGuardReleased);
		}

		if (DodgeAction)
		{
			EnhancedInput->BindAction(DodgeAction, ETriggerEvent::Started, this, &AVBCharacter::HandleDodge);
		}

		if (PurifyAction)
		{
			EnhancedInput->BindAction(PurifyAction, ETriggerEvent::Started, this,
			                          &AVBCharacter::HandlePurify);
		}

		if (ExecuteAction)
		{
			EnhancedInput->BindAction(ExecuteAction, ETriggerEvent::Started, this,
			                          &AVBCharacter::HandleExecute);
		}

		if (CrouchAction)
		{
			EnhancedInput->BindAction(CrouchAction, ETriggerEvent::Started, this,
			                          &AVBCharacter::HandleCrouchPressed);
		}

		if (SprintAction)
		{
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Started, this, &AVBCharacter::HandleSprintPressed);
			EnhancedInput->BindAction(SprintAction, ETriggerEvent::Completed, this,
			                          &AVBCharacter::HandleSprintReleased);
		}

		if (InteractAction)
		{
			EnhancedInput->BindAction(InteractAction, ETriggerEvent::Started, this, &AVBCharacter::HandleInteractPressed);
			EnhancedInput->BindAction(InteractAction, ETriggerEvent::Completed, this,
			                          &AVBCharacter::HandleInteractReleased);
		}

		if (WalkToggleAction)
		{
			// Walk는 Press-toggle (Sprint는 Hold). Started 한 번에 켜짐/꺼짐 전환.
			EnhancedInput->BindAction(WalkToggleAction, ETriggerEvent::Started, this, &AVBCharacter::HandleWalkToggle);
		}

		if (LockOnAction)
		{
			EnhancedInput->BindAction(LockOnAction, ETriggerEvent::Started, this, &AVBCharacter::HandleLockOnPressed);
		}

		if (SwitchTargetAction)
		{
			EnhancedInput->BindAction(SwitchTargetAction, ETriggerEvent::Triggered, this, &AVBCharacter::HandleSwitchTarget);
			EnhancedInput->BindAction(SwitchTargetAction, ETriggerEvent::Completed, this, &AVBCharacter::HandleSwitchTargetReleased);
			EnhancedInput->BindAction(SwitchTargetAction, ETriggerEvent::Canceled, this, &AVBCharacter::HandleSwitchTargetReleased);
		}
		
		if (WeaponSummonAction)
		{
			EnhancedInput->BindAction(WeaponSummonAction, ETriggerEvent::Started, this,
				&AVBCharacter::HandleSummonWeaponPressed);
		}
		
		if (WeaponSlot1Action)
		{
			EnhancedInput->BindAction(WeaponSlot1Action, ETriggerEvent::Started, this,
				&AVBCharacter::HandleWeaponSlot, EVBWeaponType::Katana);
		}
		
		if (WeaponSlot2Action)
		{
			EnhancedInput->BindAction(WeaponSlot2Action, ETriggerEvent::Started, this,
				&AVBCharacter::HandleWeaponSlot, EVBWeaponType::BigSword);
		}
		
		if (WeaponSlot3Action)
		{
			EnhancedInput->BindAction(WeaponSlot3Action, ETriggerEvent::Started, this,
				&AVBCharacter::HandleWeaponSlot, EVBWeaponType::Fighter);
		}
	}
}

// UE 네트워크 시스템이 어떤 변수를 복제할지 설정해주는 곳
void AVBCharacter::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// (bIsCombatMode 복제는 DEC-006 에서 제거 — 전투 여부는 WSC.LocomotionState 에서 파생, 이중 진실원 금지)
	DOREPLIFETIME_CONDITION_NOTIFY(
	                               AVBCharacter,		// 클래스명
	                               bIsSprinting,		// 변수명
	                               COND_None,			// 조건 : 모든 클라이언트에 복제
	                               REPNOTIFY_Always);	// 값이 같아도 항상 OnRep 호출

	DOREPLIFETIME_CONDITION_NOTIFY(
	                               AVBCharacter,
	                               Gait,
	                               COND_None,
	                               REPNOTIFY_Always);
	DOREPLIFETIME(AVBCharacter, bWantsToAim);
	DOREPLIFETIME(AVBCharacter, bIsSliding);
	DOREPLIFETIME(AVBCharacter, WantsToStrafe);
}

void AVBCharacter::OnSlideMovementStateChanged(bool bNewSliding)
{
	if (bIsSliding == bNewSliding) return;
	bIsSliding = bNewSliding;
	OnRep_Sliding(); // Authority and predicted owner need the same visual transition as remote OnRep.
}

void AVBCharacter::OnRep_Sliding()
{
	if (ActiveSlideMontage)
	{
		if (UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr)
			Anim->Montage_Stop(0.2f, ActiveSlideMontage);
		ActiveSlideMontage = nullptr;
	}
}

void AVBCharacter::OnRep_Sprint()
{
	// CMC만 동기화 (bIsSprinting 대입은 서버가 한 것을 복제로 받음)
	ApplySprintCMCState(bIsSprinting);
}

void AVBCharacter::HandleMove(const FInputActionValue& Value)
{
	// 카메라 기준으로 이동
	const FVector2D MoveValue = Value.Get<FVector2D>();
	if (UVBFinisherComponent::IsActorInScene(this)) return;
	if (InteractorComponent && InteractorComponent->GetPushable())
	{
		InteractorComponent->SetPushInput(MoveValue.Y);
		return;
	}
	// 정렬/접촉 동작 중 입력으로 발이 미끄러지지 않게 한다. 종료 뒤 홀드 중인 입력은 다음 틱에 재개된다.
	if (InteractorComponent && InteractorComponent->OwnsCapsuleFacing()) return;
	if (!MoveValue.IsNearlyZero() && SmartObjectInteraction && SmartObjectInteraction->IsInteracting())
		SmartObjectInteraction->RequestExit();
	if (PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive())
	{
		const FRotator Yaw(0, Controller ? Controller->GetControlRotation().Yaw : GetActorRotation().Yaw, 0);
		PlayerRagdollComponent->SetRollInput(Yaw.RotateVector(FVector(MoveValue.Y, MoveValue.X, 0)));
		return;
	}

	// 매달림 중에는 이동 입력의 주인이 트래버설 컴포넌트다(좌우=옆걸음, 앞=올라서기, 뒤=놓기).
	//   CMC 에 넘기면 Flying 가속이 붙어 스플라인이 정한 자리에서 밀려난다.
	if (TraversalComponent && TraversalComponent->IsHanging())
	{
		TraversalComponent->SetHangMoveInput(MoveValue);
		return;
	}
	// 등반 중에는 두 축이 그대로 벽 위의 이동이다. X=좌우, Y=위아래.
	if (TraversalComponent && TraversalComponent->IsClimbing())
	{
		TraversalComponent->SetClimbMoveInput(MoveValue);
		return;
	}
	// 사다리는 위아래 하나뿐이다. 좌우는 버린다 - 옆으로 갈 데가 없다.
	if (TraversalComponent && TraversalComponent->IsOnLadder())
	{
		TraversalComponent->SetLadderMoveInput(MoveValue.Y);
		return;
	}

	if (Controller)
	{
		// 카메라가 바라보는 방향 기준으로 이동 방향 계산
		const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const FVector  ForwardDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector  RightDir   = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		AddMovementInput(ForwardDir, MoveValue.Y);		// 전 후
		AddMovementInput(RightDir, MoveValue.X);		// 좌 우
	}
}

void AVBCharacter::HandleMoveCompleted()
{
	if (InteractorComponent) InteractorComponent->SetPushInput(0.0f);
	if (PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive())
	{
		PlayerRagdollComponent->SetRollInput(FVector::ZeroVector);
		return;
	}
	// 매달림·등반만 영향을 받는다. 지상 이동은 입력이 멈추면 CMC 가 알아서 멎으므로 여기서 할 일이 없다.
	if (TraversalComponent && TraversalComponent->IsHanging())
	{
		TraversalComponent->SetHangMoveInput(FVector2D::ZeroVector);
	}
	else if (TraversalComponent && TraversalComponent->IsClimbing())
	{
		TraversalComponent->SetClimbMoveInput(FVector2D::ZeroVector);
	}
	else if (TraversalComponent && TraversalComponent->IsOnLadder())
	{
		TraversalComponent->SetLadderMoveInput(0.0f);
	}
}

void AVBCharacter::HandleLook(const FInputActionValue& Value)
{
	// 마우스로 카메라 회전
	const FVector2D LookValue = Value.Get<FVector2D>();
	if (TargetLockComponent)
		TargetLockComponent->NotifyManualCameraInput(LookValue.X, LookValue.Y);

	// 감도/Y반전. 홈은 설정 세이브 하나이고 여기서 직접 읽는다(캐시 없음 = 설정 변경 즉시 반영).
	const FVBSettingsSaveData& Settings = UVBSaveGameSubsystem::GetSettingsData(this);
	const float LookYaw = LookValue.X * Settings.LookSensitivity;
	const float LookPitch = LookValue.Y * Settings.LookSensitivity * (Settings.bInvertYAxis ? -1.0f : 1.0f);

	if (TargetLockComponent && TargetLockComponent->CanTrackCamera())
	{
		// 패드 X는 전환 액션만 소비한다. 마우스는 대상 변경 없이 구도 오프셋만 준다.
		TargetLockComponent->AddLookInput(LookYaw, LookPitch, InputDeviceManager && InputDeviceManager->IsUsingGamepad());
		return;
	}

	AddControllerYawInput(LookYaw);		// 좌 우 회전
	AddControllerPitchInput(LookPitch);	// 상 하 회전
}

void AVBCharacter::HandleAbilityInputPressed(FGameplayTag InputTag)
{
	// 매달린 동안은 능력 입력을 통째로 막는다. 공격·회피 몽타주가 DefaultSlot 을 타고 매달림 포즈를 덮고,
	//   루트모션이 스플라인이 정한 자리에서 캡슐을 끌어낸다. 매달림의 입력 셋(옆걸음·올라서기·놓기)은
	//   HandleMove/HandleJumpPressed/HandleCrouchPressed 가 이 함수 앞에서 가로챈다.
	if (TraversalComponent && TraversalComponent->IsHanging())
	{
		return;
	}
	// 등반도 같다. 벽에 붙은 동안 능력 입력을 막지 않으면 몽타주가 자세를 덮고 캡슐을 벽에서 끌어낸다.
	if (TraversalComponent && TraversalComponent->IsClimbing())
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return;
	}

	// 순회하는 동안 목록이 바뀌지 않게 잠근다 (2026-09-23, FIND-087 종결).
	//
	// 왜: 이 루프 안의 TryActivateAbility 가 다시 목록을 건드리는 실경로가 있다 -
	//   경공격 -> EnsureWeaponSummoned -> CancelAbilities 가 그것이다. 그 사이에 스펙 배열이
	//   재할당되면 지금 들고 있는 참조가 가리키던 자리가 사라진다.
	//   게다가 이 루프는 활성화에 실패해도 break 하지 않고 다음 후보로 계속 가므로(아래 주석 참조)
	//   «목록을 바꾼 뒤에도 계속 도는» 시간이 길다 - 노출이 큰 모양이다.
	// 엔진의 같은 일을 하는 함수들도 이 잠금을 건다. 잠금은 실제 변경을 막는 것이 아니라
	//   순회가 끝날 때까지 «미뤄» 배열이 그 자리에서 재할당되지 않게 한다.
	// 매크로(ABILITYLIST_SCOPE_LOCK)를 안 쓰는 이유: 그 매크로는 `*this` 를 넘기므로
	//   ASC 자신의 멤버 함수 안에서만 성립한다. 여기는 캐릭터라 대상을 명시해야 한다.
	FScopedAbilityListLock ActiveScopeLock(*ASC);

	// ASC에 등록된 모든 Ability 순회
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UVBGameplayAbility* VBAbility = Cast<UVBGameplayAbility>(Spec.Ability);
		if (!VBAbility || !VBAbility->InputTag.MatchesTagExact(InputTag)) continue;

		if (VBAbility->bIsToggleAbility && Spec.IsActive())
		{
			ASC->CancelAbilityHandle(Spec.Handle);		// 토글 OFF
			break;
		}

		// 이미 도는 능력이 입력을 소비한다. 여기서 다음 후보로 넘어가면 같은 입력으로
		// 두 능력이 동시에 도는 창이 열린다.
		if (Spec.IsActive())
		{
			if (InputTag == VBGameplayTags::Input_Interact && InteractorComponent)
			{
				InteractorComponent->RequestRepeatInteraction();
			}
			break;
		}

		if (ASC->TryActivateAbility(Spec.Handle))
		{
			break;
		}

		// 활성화 실패 = 태그 게이트에 막혔다는 뜻이다. 같은 InputTag 를 가진 다음 후보를 계속 시도한다.
		// 왜 필요한가: 하나의 입력에 상호배타적인 능력을 여러 개 매다는 것이 이 프로젝트의 설계다.
		//   경공격(State.Combat.Guarding 차단) 과 반격(State.Combat.RiposteWindow 요구)이 LMB 를 공유하는데,
		//   종전 코드는 첫 후보를 시도하고 성공 여부와 무관하게 break 해서 뒤 후보가 영원히 도달 불가였다
		//   (2026-07-29 실측 - 반격이 발동하지 않던 원인).
		// GAS 자체는 태그로 후보를 걸러 주지만 그 판정은 TryActivateAbility 안에서 일어난다.
		//   실패를 무시하고 끊으면 GAS 의 필터링 결과를 버리는 셈이다.
	}
}

void AVBCharacter::HandleAbilityInputReleased(FGameplayTag InputTag)
{
	// VBGameplayAbility 베이스가 ServerInitiated (2026-04-20 전환) → 소유자 클라에도 GA 인스턴스 존재.
	// 클라 Spec.IsActive()가 true → 로컬 CancelAbilityHandle이 작동.
	// 내부적으로 ReplicateEndOrCancelAbility가 ServerCancelAbility RPC를 엔진이 자동 송신 (AbilitySystemComponent_Abilities.cpp::UAbilitySystemComponent::ReplicateEndOrCancelAbility).
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC) return;

	// 여기도 잠근다. 지금은 CancelAbilityHandle 직후 무조건 break 라서 잠금 없이도 안전하지만,
	//   그 안전을 지탱하는 것이 break 한 줄뿐이다 - 누가 그 줄을 지우거나 조건을 달면
	//   순회 중 목록 변경이 조용히 되살아난다. 두 순회가 같은 규칙을 따르게 둔다.
	FScopedAbilityListLock ActiveScopeLock(*ASC);

	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		const UVBGameplayAbility* VBAbility = Cast<UVBGameplayAbility>(Spec.Ability);
		if (!VBAbility || !VBAbility->InputTag.MatchesTagExact(InputTag)) continue;

		if (Spec.IsActive())
		{
			const UVBGA_Interact* Interaction = Cast<UVBGA_Interact>(Spec.GetPrimaryInstance());
			if (Interaction && !Interaction->ShouldCancelOnInputRelease()) break;
			ASC->CancelAbilityHandle(Spec.Handle);
		}
		break;
	}
}

void AVBCharacter::HandleAttackLight()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC) return;

	// 이미 공격 중이면 콤보 입력 — 로컬 GA가 로컬 몽타주 section 전환하도록 이벤트를 먼저 로컬 송신.
	// UE 5.7 OnRep_ReplicatedAnimMontage는 IsLocallyControlled() 가드로 소유 클라 스킵 →
	// 서버의 JumpToSection은 소유 클라 몽타주에 복제 안 됨. 클라 GA가 자기 Event_Combo_InputReceived
	// 를 수신해 로컬 몽타주를 직접 전환해야 콤보 3타+ 가 보인다.
	// 서버는 ServerHandleAttackInput RPC에서 동일 이벤트를 서버측에 송신 (중복 아님, 경로 분리).
	if (!HasAuthority() && UVBAbilitySystemStatics::IsAttacking(ASC))
	{
		FGameplayEventData EventData;
		EventData.EventTag = VBGameplayTags::Event_Combo_InputReceived;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this, VBGameplayTags::Event_Combo_InputReceived, EventData);
	}

	ServerHandleAttackInput();
}

bool AVBCharacter::ServerHandleAttackInput_Validate()
{
	// 페이로드가 없으므로 검사할 형식도 없다. 상태 조건을 여기서 보지 않는 것은 의도다 -
	// WithValidation 계약상 false 는 "거부"가 아니라 연결 종료라, 평범한 랙으로도 플레이어가 끊긴다.
	return true;
}

void AVBCharacter::ServerHandleAttackInput_Implementation()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC) return;

	if (UVBAbilitySystemStatics::IsAttacking(ASC))
	{
		// 공격 몽타주 재생 중 → 콤보 입력으로 처리
		FGameplayEventData EventData;
		EventData.EventTag = VBGameplayTags::Event_Combo_InputReceived;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(
			this, VBGameplayTags::Event_Combo_InputReceived, EventData);
	}
	else
	{
		// 회피 커밋 중(design §7): 경공격이 태그로 막혀 있으니 입력을 버리지 않고 저장한다. 커밋이 풀리면 EndDodgeCommit 이 꺼내 내보낸다.
		if (ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_DodgeCommit))
		{
			// 안전망(규칙 27): 커밋 타이머가 안 살아 있는데 태그만 남았으면 배선 결함이다. 공격을 영영 못 하게 두지 말고 태그를 걷고 진행하되 소리낸다.
			if (!GetWorldTimerManager().IsTimerActive(DodgeCommitTimerHandle))
			{
				VB_LOG(Warning, "[Combo] DodgeCommit 태그가 타이머 없이 남아 있다 - 걷어내고 공격 진행 (배선 결함)");
				ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 0);
			}
			else
			{
				PendingAttackInputTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
				VB_LOG(Log, "[Combo] 회피 커밋 중 공격 입력 - 저장");
				return;
			}
		}
		// 공격 중 아님 → 새 GA 활성화
		HandleAbilityInputPressed(VBGameplayTags::Input_Attack_Light);
	}
}

void AVBCharacter::BeginDodgeCommit(float CancelSeconds, float BufferSeconds)
{
	if (!HasAuthority() || CancelSeconds <= 0.0f) return;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 1);
	}
	GetWorldTimerManager().SetTimer(DodgeCommitTimerHandle,
	                                FTimerDelegate::CreateUObject(this, &AVBCharacter::EndDodgeCommit, BufferSeconds),
	                                CancelSeconds, false);
}

void AVBCharacter::EndDodgeCommit(float BufferSeconds)
{
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 0);
	}
	// 커밋 중 눌린 공격이 있으면 지금 내보낸다 - 회피 오프셋(VBGA_MeleeAttackBase)이 콤보 자리를 이어 준다.
	ConsumePendingAttackInput(BufferSeconds);
}

void AVBCharacter::CancelDodgeCommit()
{
	if (!HasAuthority()) return;
	GetWorldTimerManager().ClearTimer(DodgeCommitTimerHandle);
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_DodgeCommit, 0);
	}
	ClearPendingAttackInput();
}

void AVBCharacter::ConsumePendingAttackInput(float BufferSeconds)
{
	if (!HasAuthority() || PendingAttackInputTime < 0.0) return;
	const double Age = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) - PendingAttackInputTime;
	PendingAttackInputTime = -1.0;
	if (Age <= BufferSeconds)
	{
		VB_LOG(Log, "[Combo] 회피 커밋 해제 - 저장 입력(나이 %.2f초) 내보냄", Age);
		HandleAbilityInputPressed(VBGameplayTags::Input_Attack_Light);
	}
	else
	{
		VB_LOG(Log, "[Combo] 회피 커밋 해제 - 저장 입력 나이 %.2f초 > %.2f, 버림", Age, BufferSeconds);
	}
}

void AVBCharacter::ClearPendingAttackInput()
{
	PendingAttackInputTime = -1.0;
}

void AVBCharacter::MulticastJumpToMontageSection_Implementation(UAnimMontage* Montage, FName SectionName)
{
	// Sim proxy 전용 경로. 서버와 소유 클라는 자기 OnComboInputReceived에서 이미 JumpToSection 실행.
	// Host(authority): skip
	if (HasAuthority()) return;
	// 소유 클라(owning client): skip — 자기 GA로 이미 local jump 완료
	if (IsLocallyControlled()) return;

	if (!Montage) return;
	UAnimInstance* AnimInstance = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInstance) return;

	// 현재 같은 몽타주가 재생 중일 때만 jump (stale Multicast 방어)
	if (AnimInstance->Montage_IsPlaying(Montage))
	{
		AnimInstance->Montage_JumpToSection(SectionName, Montage);
	}
}

void AVBCharacter::HandleDodge()
{
	HandleAbilityInputPressed(VBGameplayTags::Input_Dodge);
}

void AVBCharacter::HandlePurify()
{
	HandleAbilityInputPressed(VBGameplayTags::Input_Purify);
}

void AVBCharacter::HandleExecute()
{
	HandleAbilityInputPressed(VBGameplayTags::Input_Execute);
}

bool AVBCharacter::IsSlideJumpSceneAllowed() const
{
	return !(InteractorComponent && InteractorComponent->OwnsCapsuleFacing())
		&& !(SmartObjectInteraction && SmartObjectInteraction->IsInteracting())
		&& !(PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive())
		&& !UVBAbilitySystemStatics::IsActorFinishing(this)
		&& !UVBAbilitySystemStatics::IsActorDead(this)
		&& !(TraversalComponent && (TraversalComponent->IsDoingTraversal() || TraversalComponent->OwnsCapsuleFacing()));
}

bool AVBCharacter::CanJumpInternal_Implementation() const
{
	return (!bSlideJumpAwaitingMovement || IsSlideJumpSceneAllowed()) && Super::CanJumpInternal_Implementation();
}

void AVBCharacter::HandleJumpPressed()
{
	if (InteractorComponent && InteractorComponent->OwnsCapsuleFacing()) return;
	if (SmartObjectInteraction && SmartObjectInteraction->IsInteracting()) return;
	if (PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive())
	{
		PlayerRagdollComponent->RequestGetUp();
		return;
	}
	// 처형을 거는 중에는 물러난다. 여기서 몽타주가 나가면 처형 애니가 그 자리에서 끊기고
	//   플러그인이 그것을 「장면 이탈」로 읽어 장면을 통째로 끝낸다(user 결정: 처형은 안 끊긴다).
	// GA 인 입력(공격·회피·가드)은 UVBGA_Execute 의 차단 목록이 막는다. 이 경로는 GA 를 안 거쳐
	//   컴포넌트를 직접 부르므로 여기서 스스로 묻는다 - 질의는 공용 한 곳을 쓴다.
	if (UVBAbilitySystemStatics::IsActorFinishing(this))
	{
		return;
	}
	bool bJumpedOutOfSlide = false;
	if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(GetCharacterMovement()))
	{
		bJumpedOutOfSlide = SlideCMC->IsSliding();
		SlideCMC->CancelSlide();
	}

	// 매달린 채 점프 = 올라서기. 홀드 플래그는 세우지 않는다 - 올라선 뒤 Walking 이라 공중 체크와 무관하지만,
	//   놓기로 이어질 때 쥔 채 남은 플래그가 같은 렛지를 다시 물게 한다(재잡기 유예는 컴포넌트가 따로 건다).
	if (TraversalComponent && TraversalComponent->IsHanging())
	{
		TraversalComponent->RequestHangClimbUp();
		return;
	}
	// 등반 중 점프는 아직 할 일이 없다. 벽차기와 꼭대기 올라서기가 여기 붙을 자리다(단계 2 남은 분).
	//   그냥 두면 일반 점프가 돌아 벽에서 캡슐이 튀어 나가므로 여기서 끊는다.
	if (TraversalComponent && TraversalComponent->IsClimbing())
	{
		return;
	}

	// 공중 맨틀 홀드 게이트 — 점프부터 계속 홀드하거나 공중에서 재입력하면 Tick 의 공중 체크가 이어받는다.
	bJumpInputHeld = true;
	bSlideJumpReleaseRequested = false;

	// GASP-exact 지상 게이트 (2026-06-04 라이브 검증): GASP IA_Jump 는
	//   !DoingTraversalAction && CMC.IsMovingOnGround → TryTraversalAction → 실패 시 Jump
	// 로 공중 입력을 전부 무시한다. 게이트 없이는 공중 재입력 시 Vault/Hurdle 까지 발동 가능(GASP 불일치).
	const UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC || !CMC->IsMovingOnGround())
	{
		return;
	}

	// 트래버설(맨틀/볼트/허들) 우선 — 전방 장애물 트레이스 성공 시 점프 대신 트래버설 수행.
	// 컴포넌트 미부착 / 자산(Config·CHT) 미할당 / 장애물 없음 → TryTraversalAction 이 false → 기존 ACharacter::Jump.
	if (TraversalComponent && TraversalComponent->TryTraversalAction())
	{
		return;
	}
	if (bJumpedOutOfSlide && bIsCrouched)
	{
		PendingSlideJumpUntil = GetWorld()->GetTimeSeconds() + 0.25f;
		return;
	}
	Jump();
}

void AVBCharacter::HandleJumpReleased()
{
	bJumpInputHeld = false;
	if (PendingSlideJumpUntil > 0.f || bSlideJumpAwaitingMovement)
	{
		bSlideJumpReleaseRequested = true;
		return;
	}
	StopJumping();
}

void AVBCharacter::HandleCrouchPressed()
{
	if (InteractorComponent && InteractorComponent->OwnsCapsuleFacing()) return;
	// 매달린 채 크라우치 = 놓기. 점프 홀드도 함께 푼다 - 쥔 채 떨어지면 공중 체크가 곧바로 같은 렛지를 다시 잡는다.
	if (TraversalComponent && TraversalComponent->IsHanging())
	{
		bJumpInputHeld = false;
		TraversalComponent->RequestHangDrop();
		return;
	}
	// 등반 중 크라우치 = 벽에서 놓기. 매달림과 같은 규약이다.
	if (TraversalComponent && TraversalComponent->IsClimbing())
	{
		bJumpInputHeld = false;
		TraversalComponent->RequestClimbDrop();
		return;
	}
	// 자세 관문(설계 .sw/crouch-montage 2.1~2.2) - 앉기·일어서기 양쪽. 이 능력은 캡슐만 바꾸고 몽타주를 멈추지 않아서,
	//   몸통을 쓰는 동작 도중에 바꾸면 «서기 몽타주 + 앉은 캡슐»(또는 반대)이 된다. 일어서기(토글 끄기)는 능력의 막기 태그를 안 타서 여기서 같은 묶음을 묻는다.
	//   태그 없는 몽타주(제자리 회전·트래버설·상호작용)도 같은 이유로 막는다. 막으면 한 줄 남긴다 - 조용히 버리면 «C 가 안 먹는다» 와 구별이 안 된다.
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	    ASC && ASC->HasAnyMatchingGameplayTags(UVBGA_Crouch::GetPostureBlockingTags()))
	{
		FGameplayTagContainer Owned;
		ASC->GetOwnedGameplayTags(Owned);
		VB_LOG(Log, "자세 전환 막음(%s) - %s", bIsCrouched ? TEXT("일어서기") : TEXT("앉기"),
			*Owned.Filter(UVBGA_Crouch::GetPostureBlockingTags()).ToStringSimple());
		return;
	}
	if (const UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr; Anim && Anim->IsAnyMontagePlaying())
	{
		const UAnimMontage* Playing = Anim->GetCurrentActiveMontage();
		VB_LOG(Log, "자세 전환 막음(%s) - 몽타주 %s 재생 중", bIsCrouched ? TEXT("일어서기") : TEXT("앉기"), *GetNameSafe(Playing));
		return;
	}
	if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(GetCharacterMovement());
		SlideCMC && SlideCMC->IsSliding())
	{
		SlideCMC->CancelSlide(true);
		return;
	}
	// Sprint+Crouch: the existing input starts a predicted CMC slide. Ordinary crouch still uses GAS.
	if (bIsSprinting)
	{
		if (UVBSlideMovementComponent* SlideCMC = Cast<UVBSlideMovementComponent>(GetCharacterMovement());
			SlideCMC && SlideCMC->CanRequestSlide() &&
			(!GetMesh() || !GetMesh()->GetAnimInstance() || !GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()))
		{
			Crouch();
			SlideCMC->RequestSlide();
			return;
		}
	}
	HandleAbilityInputPressed(VBGameplayTags::Input_Crouch);
}

void AVBCharacter::HandleGuardPressed()
{
	// 가드 활성화 요청 — 카타나 게이트/서버 권위는 VBGA_Guard 가 담당. 여기선 입력 dispatch 만.
	HandleAbilityInputPressed(VBGameplayTags::Input_Combat_Guard);
}

void AVBCharacter::HandleGuardReleased()
{
	// 홀드 해제 → VBGA_Guard EndAbility (Sprint 해제 패턴 동형).
	HandleAbilityInputReleased(VBGameplayTags::Input_Combat_Guard);
}

void AVBCharacter::HandleSprintPressed()
{
	HandleAbilityInputPressed(VBGameplayTags::Input_Sprint);
}

void AVBCharacter::HandleSprintReleased()
{
	HandleAbilityInputReleased(VBGameplayTags::Input_Sprint);
}

void AVBCharacter::HandleInteractPressed()
{
	if (InteractorComponent && InteractorComponent->OwnsCapsuleFacing())
	{
		HandleAbilityInputPressed(VBGameplayTags::Input_Interact);
		return;
	}
	if (SmartObjectInteraction && SmartObjectInteraction->IsInteracting())
	{
		SmartObjectInteraction->RequestExit();
		return;
	}
	if ((!InteractorComponent || !InteractorComponent->FindBestInteractable()) &&
		SmartObjectInteraction && SmartObjectInteraction->TryInteract()) return;
	HandleAbilityInputPressed(VBGameplayTags::Input_Interact);
}

void AVBCharacter::HandleInteractReleased()
{
	// 홀드만 취소한다. 탭의 정렬/몽타주는 키를 뗀 뒤에도 이어진다.
	HandleAbilityInputReleased(VBGameplayTags::Input_Interact);
}

void AVBCharacter::HandleSummonWeaponPressed()
{
	HandleAbilityInputPressed(VBGameplayTags::Input_WeaponSummon);
}

void AVBCharacter::HandleWeaponSlot(EVBWeaponType RequestedType)
{
	// 처형을 거는 중에는 물러난다. 여기서 몽타주가 나가면 처형 애니가 그 자리에서 끊기고
	//   플러그인이 그것을 「장면 이탈」로 읽어 장면을 통째로 끝낸다(user 결정: 처형은 안 끊긴다).
	// GA 인 입력(공격·회피·가드)은 UVBGA_Execute 의 차단 목록이 막는다. 이 경로는 GA 를 안 거쳐
	//   컴포넌트를 직접 부르므로 여기서 스스로 묻는다 - 질의는 공용 한 곳을 쓴다.
	if (UVBAbilitySystemStatics::IsActorFinishing(this))
	{
		return;
	}

	if (!WeaponStateComponent) return;

	if (WeaponStateComponent->GetCurrentWeaponType() == RequestedType &&
		WeaponStateComponent->IsWeaponSummoned())
	{
		WeaponStateComponent->SheatheWeapon();
	}
	else
	{
		WeaponStateComponent->SummonWeapon(RequestedType);
	}
}

void AVBCharacter::HandleLockOnPressed()
{
	if (!TargetLockComponent) return;
	TargetLockComponent->ToggleLockOn();
}

void AVBCharacter::HandleSwitchTarget(const FInputActionValue& Value)
{
	if (!TargetLockComponent) return;
	float Axis = Value.Get<float>();
	TargetLockComponent->HandleSwitchInput(Axis, InputDeviceManager && InputDeviceManager->IsUsingGamepad());
}

void AVBCharacter::HandleSwitchTargetReleased()
{
	if (TargetLockComponent) TargetLockComponent->ResetSwitchInput();
}

void AVBCharacter::ApplySprintState(bool bNewSprinting)
{
	// 서버 진입점 (RPC 내부 또는 서버 코드에서 직접 호출)
	if (!HasAuthority()) return;

	bIsSprinting = bNewSprinting;

	// Walk 해제 (DEC-007 user 모델 2026-06-09): Walk 토글 중 Sprint 누르면 Walk 가 풀린다 — Gait 를 Run 으로 복귀.
	// Sprint 종료 후 Walk 로 되돌아가지 않게 토글 상태 자체를 해제(애님측 sprint→Gait=Run 가드와 별개의 영속 상태 변경).
	if (bNewSprinting && Gait == EVBGait::Walk)
	{
		Gait = EVBGait::Run;
	}

	ApplySprintCMCState(bNewSprinting);
    
	// LockOn 해제는 서버 권한
	if (bNewSprinting && TargetLockComponent && TargetLockComponent->IsLockedOn())
	{
		TargetLockComponent->ClearLockOn();
	}
}

void AVBCharacter::ApplySprintCMCState(bool /*bNewSprinting*/)
{
	// MaxWalkSpeed 는 Tick 의 UpdateMaxSpeed() 가 매 프레임 (Sprint/Gait + 방향 조합) 으로 갱신.
	// 여기선 회전 모드만 갱신: Sprint 진입/종료 즉시 Strafe ↔ Free 전환이 필요 (다음 tick 까지 기다리면 시각 lag).
	ApplyRotationMode();
}

void AVBCharacter::UpdateMaxSpeed()
{
	// GASP CalculateMaxSpeed 이식. velocity vs ActorForward 각도(0~180°) 를 curve 로 zone(0/1/2) 매핑 후
	// gait_speeds.(X=Forward | Y=Strafe | Z=Backward) 사이 lerp 로 MaxWalkSpeed 갱신.
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC) return;

	// 1) 방향 각도 — velocity 가 0 이면 Forward (zone=0) 로 처리.
	float DirectionDeg = 0.0f;
	const FVector Vel = GetVelocity();
	const FVector Vel2D(Vel.X, Vel.Y, 0.0f);
	if (!Vel2D.IsNearlyZero())
	{
		const FVector ActorFwd = GetActorForwardVector();
		const float Dot = FVector::DotProduct(ActorFwd, Vel2D.GetSafeNormal());
		DirectionDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Dot, -1.0f, 1.0f)));
	}

	// 2) Curve 샘플링 — 0(Fwd) / 1(Strafe) / 2(Back). Curve 미할당 시 zone=0 → 항상 Forward 속도.
	const float Zone = StrafeSpeedMapCurve ? StrafeSpeedMapCurve->GetFloatValue(DirectionDeg) : 0.0f;

	// 3) gait_speeds 선택 — 무기타입·무장여부로 프로파일 해석(UVBMovementConfig::ResolveProfile).
	//    무장 기본 순항은 저작된 8-way jog 속도에 맞춘다. CapsLock=Walk(165), Sprint는 2단 램프.
	//    BigSword 특례는 프로파일(bSprintRampEnabled=false)로 흡수한다 — 무기별 속도는 데이터가 소유한다.
	//    게이트는 MM 풀이다(2026-08-14). 종전 IsInCombat 은 상태2(납도)를 전투 프로파일로 몰았는데,
	//      납도 클립이 비무장 풀로 바뀌자 보폭과 클립의 출처가 갈렸다 — 대검 납도에서 속도 270 에
	//      500 기준 클립이 재생돼 워프비 0.54(하한 0.85 클램프), user FEEL "MM Loco 가 엄청 이상하다".
	//      풀을 정하는 값이 속도도 정하면 그 어긋남이 구조적으로 불가능해진다. 풀 None = 비무장 프로파일.
	const UVBMovementConfig* Cfg = GetMovementConfig();
	const EVBWeaponType MMPool = WeaponStateComponent
		? WeaponStateComponent->GetMMPool() : EVBWeaponType::None;
	const FVBMovementProfile& P = Cfg->ResolveProfile(MMPool, MMPool != EVBWeaponType::None);

	// Sprint rising-edge 로 홀드 시작시각 기록 → 경과시간으로 1·2단계(SprintRun/Sprint) 판정.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	if (bIsSprinting && !bWasSprinting) { SprintHoldStartTime = Now; }
	bWasSprinting = bIsSprinting;
	FVector GaitSpeeds;
	if (bIsSprinting)
	{
		if (P.bSprintRampEnabled)
		{
			// 2단 램프: 0~SprintRampDuration = SprintRunSpeeds(1단) → 이후 SprintSpeeds(2단).
			const float Held = Now - SprintHoldStartTime;
			GaitSpeeds = (Held < Cfg->SprintRampDuration) ? P.SprintRunSpeeds : P.SprintSpeeds;
		}
		else
		{
			// 램프 없음 — SprintRunSpeeds 에 캡(BigSword 585) 또는 단일 sprint(Unarmed 650). 옛 특례분기 대체.
			GaitSpeeds = P.SprintRunSpeeds;
		}
	}
	else if (Gait == EVBGait::Walk) GaitSpeeds = P.WalkSpeeds;
	else                            GaitSpeeds = P.RunSpeeds;

	// 4) Zone lerp — 0..1: Fwd→Strafe (X→Y), 1..2: Strafe→Back (Y→Z).
	float NewMaxSpeed = (Zone < 1.0f)
		? FMath::Lerp(GaitSpeeds.X, GaitSpeeds.Y, Zone)
		: FMath::Lerp(GaitSpeeds.Y, GaitSpeeds.Z, Zone - 1.0f);
	// 45/135도 jog은 별도 저작 클립이다. 축 속도 곡선만 쓰면 Fighter 45도가 약245cm/s인데
	// 해당 FL/FR45 루트 이동은 약225cm/s라 반복 방향 전환 때 접지한 발이 밀린다.
	// 0/90/180도에는 영향을 주지 않고 대각선 정점에서만 데이터의 클립 속도에 수렴시킨다.
	if (MMPool != EVBWeaponType::None && !bIsSprinting && Gait == EVBGait::Run && P.RunDiagonalSpeed > 0.f)
	{
		const float NearestDiagonal = DirectionDeg <= 90.f ? 45.f : 135.f;
		const float DiagonalWeight = 1.f - FMath::Clamp(FMath::Abs(DirectionDeg - NearestDiagonal) / 45.f, 0.f, 1.f);
		NewMaxSpeed = FMath::Lerp(NewMaxSpeed, P.RunDiagonalSpeed, DiagonalWeight);
	}

	CMC->MaxWalkSpeed = NewMaxSpeed;

	// P5 crouch 보폭 (2026-06-10): 프로파일 CrouchSpeed — 무장(전투 MM)=165(클립 워프 윈도우 정합),
	// 비무장=150(UnarmedProfile.CrouchSpeed, SM/BS 경로). ResolveProfile 이 무장 여부로 이미 분기.
	CMC->MaxWalkSpeedCrouched = P.CrouchSpeed;
}

void AVBCharacter::ApplyRotationMode()
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC) return;

	// Strafe 결정 (GASP 패턴). Sprint 중엔 무조건 Free(전방 Run549/Sprint636). 그 외엔 WantsToStrafe || LockedOn || 전투.
	// DEC-007 user 모델 (2026-06-09): 기본 순항 = Jog238 8-way strafe(카메라/타겟 페이싱이라야 FR45/BR45/R90
	// 8방향 클립이 MM 직접 매칭 + AO/Lean facing 정합). Sprint 만 Free(전방). free-vs-strafe[락온 분기] 철회 —
	// sprint 이 락온 자동해제하므로 기본은 항상 strafe 면 됨. (WantsToStrafe 기본 true = D 사이드스텝/헤드룩/TIP, FIND-019.)
	const bool bLockedOn = TargetLockComponent && TargetLockComponent->IsLockedOn();
	const bool bInCombat = WeaponStateComponent && WeaponStateComponent->IsInCombat();
	const bool bStrafe = !bIsSprinting && (WantsToStrafe || bWantsToAim || bLockedOn || bInCombat);

	// 트래버설 컴포넌트가 캡슐 «얼굴» 을 매 틱 쓰는 상태(매달림·등반·사다리)에서는 둘 다 끈다.
	//   이들은 MOVE_Flying 이라 루트모션 게이트(IsCapsuleRotationOwnedByFeature)가 안 막는 경로 -
	//   PhysicsRotation 은 루트모션이 없는 틱에도 돌아 컨트롤 회전(카메라) 쪽으로 캡슐을 돌린다.
	//   복구가 이 함수를 다시 불러 되돌린다.
	// 목록을 여기 나열하지 않고 술어를 부르는 이유(2026-09-20): 종전에는 이 줄이 매달림·등반 둘을
	//   손으로 나열했고, 사다리가 들어오면서 그 목록에 안 들어갔다. 그래서 사다리에서 캡슐이 카메라를
	//   따라 돌아 캐릭터가 옆으로 붙었고, 손발 IK 가 깊이 자리에서 좌우 오프셋을 읽어 63cm 로 튀었다.
	//   «누가 얼굴을 쓰는가» 를 아는 것은 그 컴포넌트이므로 판단을 그쪽에 둔다.
	const bool bFacingOwnedByFeature = (TraversalComponent && TraversalComponent->OwnsCapsuleFacing()) ||
		UVBFinisherComponent::IsActorInScene(this) ||
		(PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive()) ||
		(SmartObjectInteraction && SmartObjectInteraction->OwnsCapsuleFacing()) ||
		(InteractorComponent && InteractorComponent->OwnsCapsuleFacing());

	CMC->bOrientRotationToMovement     = !bStrafe && !bFacingOwnedByFeature;
	// GASP UpdateRotation Branch 1 (gasp_full_reference.md:589-598): Strafe → true, Free → false.
	// Strafe 시 capsule 이 ControlRotation 방향으로 부드럽게 회전해야 trajectory 가 facing 과 일치 →
	// MM 이 forward anim 선택. false 강제 시 capsule yaw 고정 → trajectory backward → backward anim 회귀(2026-05-23).
	CMC->bUseControllerDesiredRotation = bStrafe && !bFacingOwnedByFeature;

	// RotationRate 는 UpdateRotationRateForMovementMode() 가 매 tick 갱신 (Grounded=-1, Falling=(0,500,0)).
	// OffsetRootBone (Accumulate) + RootMotion 이 시각 회전을 담당하므로 instant snap 허용.
}

void AVBCharacter::SetWantsToStrafe(bool bNewValue)
{
	if (WantsToStrafe == bNewValue) return;
	WantsToStrafe = bNewValue;
	ApplyRotationMode();
}

void AVBCharacter::OnTargetLocked(AActor* /*TargetActor*/)
{
	ApplyRotationMode();
}

void AVBCharacter::OnTargetUnlocked(AActor* /*TargetActor*/)
{
	ApplyRotationMode();
}

void AVBCharacter::OnLocomotionStateChanged(EVBLocomotionState /*NewState*/, EVBWeaponType /*NewWeaponType*/)
{
	// WSC의 LocomotionState 전환 시 CMC 속도 재계산.
	// 현재 Sprint 상태 그대로 유지하면서 새 LocomotionState에 맞는 속도 세트로 전환.
	ApplySprintCMCState(bIsSprinting);
}

void AVBCharacter::ServerApplySprintState_Implementation(bool bNewSprinting)
{
	ApplySprintState(bNewSprinting);
} 

bool AVBCharacter::ServerApplySprintState_Validate(bool bNewSprinting)
{
	return true;
}

// 회복 캔슬 직후 이동 상태 추적. 0 = 조용(기본). 1 = 캔슬 시점부터 0.05초 간격 8표본을 [ComboTrace] 로 찍는다.
// 왜 남기나: 2026-09-07 user 관측 「카타나 3타 뒤 Idle 이 아니라 Run 갔다가 Idle」의 원인을 가르는 유일한 수단이다.
//   정적으로 잰 것 - 3타 창 닫힘이 클립의 루트모션 203cm/s 구간에 걸린다(1/2/4타는 85/12/87). 그러나
//   그 속도가 캡슐에 «남는지» 는 런타임에서만 보이고, 이 프로젝트에서 PIE 는 user 몫이다.
// 2026-09-07 조사 종료로 기본값을 1 에서 0 으로 되돌렸다(약속대로). 계측 자체는 남긴다 -
//   이 두 줄이 「캔슬 뒤 미끄러짐」을 가른 유일한 수단이었고, 같은 부류가 다른 무기·다른 타에서 또 나올 수 있다.
//   켜는 법: 콘솔 `vb.Combat.RecoveryCancelTrace 1`. 캔슬 한 번에 8줄이라 상시로 두기에는 시끄럽다.
static TAutoConsoleVariable<int32> CVarRecoveryCancelTrace(
	TEXT("vb.Combat.RecoveryCancelTrace"),
	0,
	TEXT("Combo recovery-cancel trace: 0=off, 1=log [ComboTrace] speed/root-motion for 0.4s after each cancel."),
	ECVF_Default
);

void AVBCharacter::StopResidualAttackMotion(float RestoreAfterSeconds, const UAnimMontage* SourceMontage)
{
	UCharacterMovementComponent* CMC = GetCharacterMovement();
	if (!CMC || !CMC->IsMovingOnGround() || CMC->HasRootMotionSources()
		|| CMC->GetCurrentAcceleration().Size2D() > KINDA_SMALL_NUMBER)
	{
		return;
	}
	const FAnimMontageInstance* RootMotionOwner = GetRootMotionAnimMontageInstance();
	if (RootMotionOwner && RootMotionOwner->Montage != SourceMontage) return;
	const FAnimMontageInstance* RecoveryInstance = RootMotionOwner;
	if (!RecoveryInstance && GetMesh() && GetMesh()->GetAnimInstance())
	{
		const auto& Instances = GetMesh()->GetAnimInstance()->MontageInstances;
		for (int32 Index = Instances.Num() - 1; Index >= 0; --Index)
			if (Instances[Index] && Instances[Index]->Montage == SourceMontage
				&& Instances[Index]->GetWeight() > ZERO_ANIMWEIGHT_THRESH)
			{
				RecoveryInstance = Instances[Index];
				break;
			}
	}

	// 이미 쌓인 속도를 버린다. 한 번으로는 안 된다 - 이 함수는 몽타주 노티 처리 중에 불리는데
	//   그 프레임의 이동 적용은 아직 오지 않았다. 그래서 지운 속도를 같은 프레임의 루트모션이 도로 채운다.
	//   2026-09-07 [ComboTrace] 실측이 그것을 보여줬다: 캔슬 278 -> 다음 프레임 272 -> 관성으로 0.2초 감속.
	//   아래 마감 시각을 세워 Tick 이 그 구간 내내 누르게 한다.
	CMC->StopMovementImmediately();
	// 이미 완료된 몽타주는 속도만 버린다. 이후 회피/피격/새 공격까지 누르는 타이머를 만들지 않는다.
	if (!RecoveryInstance) return;
	ResidualMotionMontageInstanceId = RecoveryInstance->GetInstanceID();
	if (const UWorld* W = GetWorld())
	{
		ResidualMotionSuppressUntil = W->GetTimeSeconds() + FMath::Max(0.01f, RestoreAfterSeconds + RecoveryInstance->GetBlend().GetBlendTimeRemaining());
	}

	// 그래서 남은 구간의 루트모션 «이동» 자체를 0 으로 누른다. 회전과 자세는 건드리지 않는다.
	SetAnimRootMotionTranslationScale(0.0f);

	// 복구의 주인은 Tick 하나다. 종전에는 타이머도 같은 값을 되돌려 주인이 둘이었다 - 지금은 무해하지만
	//   한쪽이 조건을 얻으면(예: 입력이 들어오면 즉시 복구) 두 경로가 갈린다. 마감 시각 하나만 남긴다.
	if (ResidualMotionSuppressUntil <= 0.0)
	{
		SetAnimRootMotionTranslationScale(1.0f);   // 마감 시각을 못 세웠으면 즉시 복구 - 0 으로 굳는 것이 최악이다
	}
}

void AVBCharacter::BeginRecoveryCancelTrace()
{
	if (CVarRecoveryCancelTrace.GetValueOnGameThread() == 0)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	RecoveryCancelTraceLeft = 8;
	World->GetTimerManager().ClearTimer(RecoveryCancelTraceTimer);
	World->GetTimerManager().SetTimer(RecoveryCancelTraceTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		const UCharacterMovementComponent* CMC = GetCharacterMovement();
		if (!CMC)
		{
			return;
		}
		// 어느 풀에서 골랐는지까지 찍는다 - 속도가 0 인데도 정지 클립이 나오면 원인은 «현재 속도» 가 아니라
		//   궤적 «이력» 이다(엔진이 과거 30 표본을 질의에 함께 쓴다). 그 둘은 이 한 줄로만 갈린다.
		FString Pool = TEXT("(없음)");
		if (const UVBAnimInstance* VBAnim = Cast<UVBAnimInstance>(GetMesh() ? GetMesh()->GetAnimInstance() : nullptr))
		{
			if (const UPoseSearchDatabase* Db = VBAnim->GetCurrentSelectedDatabase())
			{
				Pool = Db->GetName();
			}
		}
		VB_LOG(Log, "[ComboTrace] 남은표본 %d | Speed2D %.0f | 루트모션 %s | 가속 %.0f | MM풀 %s",
		       RecoveryCancelTraceLeft, CMC->Velocity.Size2D(),
		       IsPlayingRootMotion() ? TEXT("있음") : TEXT("없음"),
		       CMC->GetCurrentAcceleration().Size2D(), *Pool);
		if (--RecoveryCancelTraceLeft <= 0)
		{
			if (UWorld* W = GetWorld())
			{
				W->GetTimerManager().ClearTimer(RecoveryCancelTraceTimer);
			}
		}
	}), 0.05f, true, 0.0f);
}

void AVBCharacter::ApplyGait(EVBGait NewGait)
{
	// 서버 권한 진입점. Walk 또는 Run만 허용. Sprint 값은 거부 (Sprint는 bIsSprinting로 관리).
	if (!HasAuthority()) return;
	if (NewGait == EVBGait::Sprint) return;

	Gait = NewGait;

	// Sprint 중이면 Gait 변화는 즉시 체감 안 되지만 CMC 재계산(Sprint 해제 시 바로 반영되도록).
	ApplySprintCMCState(bIsSprinting);
}

void AVBCharacter::ServerApplyGait_Implementation(EVBGait NewGait)
{
	ApplyGait(NewGait);
}

bool AVBCharacter::ServerApplyGait_Validate(EVBGait NewGait)
{
	// Walk/Run만 허용. Sprint enum 값은 RPC로 받지 않음 (치트 방지).
	return NewGait == EVBGait::Walk || NewGait == EVBGait::Run;
}

void AVBCharacter::HandleWalkToggle()
{
	// Walk↔Run 토글. 현재 Gait 기반으로 반전해 서버에 전달.
	// 소유 클라의 Gait는 복제로 받으므로 즉시 읽고 반전.
	const EVBGait NewGait = (Gait == EVBGait::Walk) ? EVBGait::Run : EVBGait::Walk;
	ServerApplyGait(NewGait);
}

void AVBCharacter::OnRep_Gait()
{
	// 클라 복제 수신 — CMC 속도 갱신 (orient 플래그는 Sprint 관리).
	ApplySprintCMCState(bIsSprinting);
}

void AVBCharacter::StartSprint()
{
	ServerApplySprintState(true);
}

void AVBCharacter::StopSprint()
{
	ServerApplySprintState(false);
}

UAbilitySystemComponent* AVBCharacter::GetAbilitySystemComponent() const
{
	return CachedASC.Get();
}

void AVBCharacter::NotifyCombatStimulus()
{
	// 전투 자극 (공격/회피/의료 GA + 락온) — 라우팅만 하고 판단은 WSC 단일 진실원에 위임:
	// 평상이면 상태2(전투·무기Off) 진입, 이미 전투면 이탈 타이머 리셋 (DEC-006 ①).
	if (!HasAuthority()) return;
	if (!WeaponStateComponent) return;
	WeaponStateComponent->EnterCombat();
}

bool AVBCharacter::IsUsingGameplayCamera() const
{
	return GameplayCameraComponent && GameplayCameraComponent->CameraReference.IsValid();
}

void AVBCharacter::SetGASPCameraStyle(FName Style)
{
	// Names verified against the original GASP E_CameraStyle asset.
	if (Style == TEXT("Close") || Style == TEXT("Medium") || Style == TEXT("Far") || Style == TEXT("Debug"))
		GASPCameraStyle = Style;
}

void AVBCharacter::SetWantsToAim(bool bAim)
{
	if (!HasAuthority() && !IsLocallyControlled()) return;
	bAim = bAim && !(PlayerRagdollComponent && PlayerRagdollComponent->IsRagdollActive()) &&
		!(CachedASC.IsValid() && (CachedASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead) ||
			CachedASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Finishing)));
	bWantsToAim = bAim;
	if (bAim) StopSprint();
	ApplyRotationMode();
	if (!HasAuthority()) ServerSetWantsToAim(bAim);
}

void AVBCharacter::ServerSetWantsToAim_Implementation(bool bAim)
{
	SetWantsToAim(bAim);
}

void AVBCharacter::OnRepGASPRotationIntent()
{
	ApplyRotationMode();
}

void AVBCharacter::SetGASPSmartObjectWalkState(bool bWalk)
{
	if (!HasAuthority()) return;
	StopSprint();
	UnCrouch();
	SetWantsToAim(false);
	SetWantsToStrafe(false);
	ApplyGait(bWalk ? EVBGait::Walk : EVBGait::Run);
	ForceNetUpdate();
}

void AVBCharacter::RestoreGASPSmartObjectInputState(EVBGait PreviousGait, bool bPreviousStrafe, bool bPreviousAim)
{
	if (!HasAuthority()) return;
	ApplyGait(PreviousGait == EVBGait::Walk ? EVBGait::Walk : EVBGait::Run);
	SetWantsToStrafe(bPreviousStrafe);
	SetWantsToAim(bPreviousAim);
	ForceNetUpdate();
}

void AVBCharacter::HandleAimPressed() { SetWantsToAim(true); }
void AVBCharacter::HandleAimReleased() { SetWantsToAim(false); }
void AVBCharacter::HandleRagdollPressed()
{
	if (PlayerRagdollComponent) PlayerRagdollComponent->RequestRagdoll();
}

void AVBCharacter::RefreshGameplayCamera()
{
	if (!GameplayCameraComponent) return;
	APlayerController* Player = Cast<APlayerController>(GetController());
	if (Player && Player->IsLocalController() && IsUsingGameplayCamera())
	{
		CameraComponent->Deactivate();
		if (!GameplayCameraComponent->IsActive())
			GameplayCameraComponent->ActivateCameraForPlayerController(Player, true);
	}
	else
	{
		if (EnvironmentCameraComponent) EnvironmentCameraComponent->ReleaseCollisionOwnership();
		if (GameplayCameraComponent->IsActive()) GameplayCameraComponent->Deactivate();
	}
}

void AVBCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	RefreshGameplayCamera();
}

void AVBCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	RefreshGameplayCamera();
}
