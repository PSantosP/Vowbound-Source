// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"

// Enhanced Input
#include "InputActionValue.h"
#include "GameplayTagContainer.h"
#include "Types/VBWeaponStateTypes.h"
#include "Animation/VBAnimationTypes.h"
#include "Character/VBGuardable.h"     // IVBGuardable — 데미지 hub 탈구체화(FIND-057a)
#include "Types/VBTeamTypes.h"     // IGenericTeamAgentInterface + EVBTeam
#include "VBCharacter.generated.h"



// 전방선언
class UInputAction;
class UInputMappingContext;
class USpringArmComponent;
class UCameraComponent;
class UGameplayCameraComponent;
class UVBEnvironmentCameraComponent;
class UVBAbilitySystemComponent;
class UVBInputDeviceManager;
class UVBTargetLockComponent;
class UMotionWarpingComponent;
class UVBCharacterTrajectoryComponent;
class UVBWeaponStateComponent;
class UVBMeleeTraceComponent;
class UVBFinisherComponent;
class UContextualAnimSceneActorComponent;
class UVBTraversalComponent;
class UVBPlayerRestoreComponent;
class UVBPlayerReadinessComponent;
class UVBInteractorComponent;

class UCharacterTrajectoryComponent;
class UCurveFloat;
class UAnimMontage;
class UAnimSequence;
class UVBMovementConfig;
class UVBTurnInPlaceConfig;
class UVBCameraConfig;
class UVBParryConfig;
class UVBPlayerRagdollComponent;
class UVBRagdollComponent;
class UVBContextEffectComponent;
class UVBLookAtPOIComponent;
class UVBPlayerAttentionComponent;
class UVBSlideMovementComponent;
class UVBGASPSmartObjectComponent;

// 플레이어가 조종하는 실제 Pawn
// ACharacter가 직접 상속
UCLASS()
class VOWBOUND_API AVBCharacter : public ACharacter, public IAbilitySystemInterface, public IVBGuardable,
                                  public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	//~ Begin IGenericTeamAgentInterface
	// 지각의 적대 판정이 이 값을 읽는다. 폰에 구현하는 이유: 엔진이 액터에서 먼저 찾고
	//   없을 때만 컨트롤러로 내려가므로, 폰이 답하면 AI/플레이어 컨트롤러 유무와 무관하게 일관된다.
	virtual FGenericTeamId GetGenericTeamId() const override { return VBTeam::ToGenericId(Team); }
	//~ End IGenericTeamAgentInterface
	AVBCharacter(const FObjectInitializer& ObjectInitializer);
	
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// 전투 자극 통지 (공격/회피/의료 GA + 락온) — WSC 가 단일 진실원: 평상이면 상태2 진입, 전투면 타이머 리셋.
	// 구 bIsCombatMode/SetCombatMode 이중 상태는 DEC-006 에서 제거 (실소비처 0 — 회전 플래그와 동일한 이중 작성자 정리).
	void NotifyCombatStimulus();
	void StartSprint();
	void StopSprint();
	FORCEINLINE bool GetIsSprinting() const { return bIsSprinting; }
	void OnSlideMovementStateChanged(bool bNewSliding);
	bool GetIsSliding() const { return bIsSliding; }
	FORCEINLINE EVBGait GetGait() const { return Gait; }
	FORCEINLINE UVBInputDeviceManager* GetInputDeviceManager() const { return InputDeviceManager; }
	FORCEINLINE USpringArmComponent* GetSpringArm() const { return SpringArmComponent; }
	FORCEINLINE UCameraComponent* GetCamera() const { return CameraComponent; }
	bool IsUsingGameplayCamera() const;
	UFUNCTION(BlueprintPure, Category="Vowbound|Camera")
	FName GetGASPCameraStyle() const { return GASPCameraStyle; }
	UFUNCTION(BlueprintCallable, Category="Vowbound|Camera")
	void SetGASPCameraStyle(FName Style);
	UFUNCTION(BlueprintPure, Category="Vowbound|Camera")
	bool WantsToAim() const { return bWantsToAim; }
	UFUNCTION(BlueprintCallable, Category="Vowbound|Camera")
	void SetWantsToAim(bool bAim);
	void SetGASPSmartObjectWalkState(bool bWalk);
	void RestoreGASPSmartObjectInputState(EVBGait PreviousGait, bool bPreviousStrafe, bool bPreviousAim);
	UVBPlayerRagdollComponent* GetPlayerRagdollComponent() const { return PlayerRagdollComponent; }
	virtual void PawnClientRestart() override;
	virtual void NotifyControllerChanged() override;
	FORCEINLINE UVBTargetLockComponent* GetTargetLockComponent() const { return TargetLockComponent; }
	FORCEINLINE UMotionWarpingComponent* GetMotionWarpingComponent() const { return MotionWarpingComponent;}
	FORCEINLINE UVBCharacterTrajectoryComponent* GetTrajectoryComponent() const { return TrajectoryComponent;}
	FORCEINLINE UVBWeaponStateComponent* GetWeaponStateComponent() const { return WeaponStateComponent;}
	FORCEINLINE UVBTraversalComponent* GetTraversalComponent() const { return TraversalComponent;}
	// HUD 프롬프트가 «이 행동에 매핑된 키» 를 묻는 데 쓴다.
	FORCEINLINE const UInputAction* GetInteractAction() const { return InteractAction; }
	
protected:
	// 소속 세력. 지각의 적대 판정이 여기서 시작한다 - 다른 팀이면 Hostile 이라 AI 가 본다.
	// None 으로 두면 모두에게 Neutral 이 되고, 지각이 중립을 안 보므로 AI 에게 투명해진다
	//   (BeginPlay 가 경고를 남긴다 - 조용히 장님이 되는 것을 막는다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Team")
	EVBTeam Team = EVBTeam::Player;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Sprint, Category = "Vowbound|Locomotion")
	bool bIsSprinting;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Sliding, Category="Vowbound|Locomotion")
	bool bIsSliding = false;

	// 플레이어가 선택한 보행 모드(Walk 또는 Run만). Walk 토글로 Run↔Walk 전환.
	// Sprint은 별도 bool(bIsSprinting)로 처리 — Sprint 활성 시 이 값은 무시되고 Gait=Sprint로 간주.
	// AnimInstance.Gait는 bIsSprinting ? Sprint : VBChar.Gait 로 결정.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Gait, Category = "Vowbound|Locomotion")
	EVBGait Gait = EVBGait::Run;
	virtual void PossessedBy(AController* NewController) override;
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool CanJumpInternal_Implementation() const override;
	virtual void Landed(const FHitResult& Hit) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	// 공격 워프 타겟의 수명 관리 — 죽거나 사라진 적, 또는 이미 좁혀져 퇴화한 sync point 를 끊는다.
	// 정리만 한다(부수효과). 종전에는 "회전 주도권을 쥐었는가"를 함께 반환했는데, 이름이 말하는 일과
	//   반환값이 말하는 일이 달라 소유권 판정이 이 함수 안에 숨어 있었다 - 질의는 아래로 분리했다.
	void UpdateWarpTargetLifetime();

	// 워프가 캡슐 회전을 쥐고 있는가.
	// 호출 순서가 계약이다 - 같은 틱에서 UpdateWarpTargetLifetime() 뒤에 불러야 한다.
	//   정리가 끊어야 할 타겟을 이미 지운 뒤라야 "타겟이 남아 있다 == 워프가 주인이다"가 성립한다.
	bool IsWarpOwningRotation() const;

	// 루트모션 중 캡슐 회전 소유자 목록의 유일한 홈.
	// 소유자가 하나라도 있으면 CMC 의 bAllowPhysicsRotationDuringAnimRootMotion 을 끈다.
	// 새 소유자는 여기에만 더한다 - Tick 이나 호출처에 조건을 흩지 말 것.
	// 구조 부채(DEBT-011): 이 목록은 블랙리스트다. 엔진 기본값(false)을 전역 true 로 뒤집고
	//   예외를 빼는 방식이라, 새 루트모션 기능이 스스로를 여기 등록하지 않으면 무증상으로 샌다
	//   (트래버설이 첫 사례 = FIND-083, 9일 잠복). 근본 해법은 화이트리스트로 뒤집는 것
	//   ("회전이 필요한 기능만 켠다")인데 그건 회피/히트리액트/처형까지 동작이 바뀌므로 FEEL 검증이 필요하다.
	//   그 전까지는 이 함수가 단일 방어선이다.
	bool IsCapsuleRotationOwnedByFeature() const;
	
	

	
	UFUNCTION()
	void OnRep_Sprint();
	UFUNCTION()
	void OnRep_Sliding();
	UFUNCTION()
	void OnRep_Gait();
		
	
	// 3인칭 카메라
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Camera")
	TObjectPtr<USpringArmComponent> SpringArmComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Camera")
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Camera")
	TObjectPtr<UGameplayCameraComponent> GameplayCameraComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Camera")
	TObjectPtr<UVBEnvironmentCameraComponent> EnvironmentCameraComponent;

	void RefreshGameplayCamera();
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> MoveAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> LookAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> JumpAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> AttackLightAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> AttackHeavyAction;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> DodgeAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> PurifyAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> ExecuteAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> CrouchAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> SprintAction;

	// Walk 토글 입력 (기본 CapsLock 제안 — BP에서 할당). Sprint가 Hold라면 Walk는 Press-toggle 방식.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> WalkToggleAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> LockOnAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> WeaponSummonAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> SwitchTargetAction;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> WeaponSlot1Action;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> WeaponSlot2Action;
	
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> WeaponSlot3Action;

	// 환경 상호작용. 누름(Started)과 뗌(Completed)을 둘 다 묶는다 - 홀드 대상은 떼면 취소된다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> InteractAction;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> RagdollAction;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Input")
	TObjectPtr<UInputAction> AimAction;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Camera")
	// GASP 카메라 거리 단계(Close/Medium/Far). 기본값은 GASP SandboxCharacter_CMC 와 같은 Medium.
	FName GASPCameraStyle = TEXT("Medium");
	UPROPERTY(ReplicatedUsing=OnRepGASPRotationIntent, BlueprintReadOnly, Category="Vowbound|Camera")
	bool bWantsToAim = false;
	UFUNCTION() void OnRepGASPRotationIntent();
	UFUNCTION(Server, Reliable) void ServerSetWantsToAim(bool bAim);
	void HandleAimPressed();
	void HandleAimReleased();
	void HandleRagdollPressed();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Ragdoll")
	TObjectPtr<UVBRagdollComponent> RagdollComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Ragdoll")
	TObjectPtr<UVBPlayerRagdollComponent> PlayerRagdollComponent;
	// 애니 순간 이펙트(발 딛음·착지 등). 발소리의 유일한 길 - 옛 GASP Foley 컴포넌트를 흡수하고 뗐다(design .sw/context-effects/design.md §10).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Effects")
	TObjectPtr<UVBContextEffectComponent> ContextEffectComponent;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Animation")
	TObjectPtr<UVBLookAtPOIComponent> LookAtPOI;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Animation")
	TObjectPtr<UVBPlayerAttentionComponent> PlayerAttention;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UVBGASPSmartObjectComponent> SmartObjectInteraction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Input")
	TObjectPtr<UVBInputDeviceManager> InputDeviceManager;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|MotionWarping")
	TObjectPtr<UMotionWarpingComponent> MotionWarpingComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|MotionWarping")
	TObjectPtr<UVBCharacterTrajectoryComponent> TrajectoryComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Weapon")
	TObjectPtr<UVBWeaponStateComponent> WeaponStateComponent;

	// 무기 궤적 적중 판정(.sw/weapon-trace). 공격 GA 가 판정 창을 열면 칼이 지나간 자리를 쓴다.
	// 왜 생성자인가: 공격은 플레이어의 상시 능력이다. 어느 날(모양)로 쓸지는 BP 기본값의 ShapeSet 이 정한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBMeleeTraceComponent> MeleeTraceComponent;

	// 처형 장면의 주인(DEC-019). 플러그인 접점이 여기 한 곳으로 모인다.
	// 왜 생성자에서 다나: 처형은 플레이어의 상시 능력이라 조건부로 붙일 이유가 없고,
	//   BP 에 손으로 다는 방식이면 빠뜨렸을 때 증상이 «아무 일도 안 일어남» 이라 찾기 어렵다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher")
	TObjectPtr<UVBFinisherComponent> FinisherComponent;

	// 처형 장면의 공격자 자리. 위 컴포넌트가 «장면을 시작하는 쪽» 이라면 이것은 «장면에 들어가는 쪽» 이고,
	//   플러그인이 두 액터를 묶을 때 양쪽 모두에 이것이 있어야 한다.
	// 왜 생성자인가: AVBEnemyBase::FinisherSceneComponent 와 같은 근거다(그 주석에 전문이 있다).
	//   시작 순간에 붙이면 원격 클라에서 도착 순서가 보장되지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher")
	TObjectPtr<UContextualAnimSceneActorComponent> FinisherSceneComponent;

	/**
	 * 화면 전체에 거는 후처리. 지금 드는 것은 처형 준비 외곽선 하나다.
	 *
	 * 왜 레벨의 PostProcessVolume 이 아니라 여기인가: 볼륨에 두면 레벨마다 다시 깔아야 하고,
	 *   빠뜨리면 «그 레벨에서만 조용히 안 나온다». 플레이어에 붙여 두면 어느 레벨이든 따라간다.
	 * 무한 범위로 둔다(bUnbound) - 플레이어가 어디 서 있든 자기 화면에는 걸려야 한다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Camera")
	TObjectPtr<class UPostProcessComponent> PostProcessComponent;

	/**
	 * 처형 준비 외곽선 머티리얼. 비면 외곽선이 안 나온다(그 외에는 아무 영향 없다).
	 *
	 * 왜 데이터인가: 색·굵기는 머티리얼 인스턴스가 들고, 어느 인스턴스를 쓸지는 연출의 선택이다.
	 *   코드가 특정 자산을 물면 그 선택을 코드 변경으로 바꾸게 된다.
	 * 대상 메시에 스텐실을 찍는 것은 UVBGC_FinisherWindow 가 한다 - 여기는 «그리는 쪽» 이다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Camera")
	TObjectPtr<UMaterialInterface> FinisherOutlineMaterial;

	// 트래버설(맨틀/볼트/허들) 컴포넌트 — IA_Jump 시 TryTraversalAction 우선 시도. GASP CBP TryTraversalAction 포팅.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Traversal")
	TObjectPtr<UVBTraversalComponent> TraversalComponent;

	// 세이브 로드 시 WP 스트리밍-안전 위치 복원 담당(GameMode 가 Continue 시 BeginRestore 호출).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Save")
	TObjectPtr<UVBPlayerRestoreComponent> PlayerRestoreComponent;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Player")
	TObjectPtr<UVBPlayerReadinessComponent> PlayerReadinessComponent;

	// 환경 상호작용 - 가리키는 대상 고르기와 홀드 표시. 판정은 UVBGA_Interact 가 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UVBInteractorComponent> InteractorComponent;

	void HandleMove(const FInputActionValue& Value);

	// 이동 입력을 뗀 순간. 평소 이동은 이 콜백이 없어도 되지만(호출이 멈추면 CMC 가 알아서 감속한다)
	//   매달림은 마지막 입력값을 «상태로 들고» 있으므로 누가 0 을 알려주지 않으면 영원히 옆걸음한다
	//   (2026-09-18 user 「한번 누르면 계속 Loop」). Triggered 는 누르는 동안만 오고 뗄 때 0 을 안 보낸다.
	void HandleMoveCompleted();
	void HandleLook(const FInputActionValue& Value);
	
	UPROPERTY(VisibleAnywhere, Category="Vowbound|Lock")
	TObjectPtr<UVBTargetLockComponent> TargetLockComponent;
	
private:
	friend class FVBSlideJumpTestAccess;
	bool IsSlideJumpSceneAllowed() const;

	// 캐시된 ASC 포인터 (PlayerState 소유, GC-only)
	UPROPERTY()
	TWeakObjectPtr<UVBAbilitySystemComponent> CachedASC;
	
	// InputTag로 Ability 활성화 해제
	void HandleAbilityInputPressed(FGameplayTag InputTag);
	void HandleAbilityInputReleased(FGameplayTag InputTag);
	
	// InputAction 콜백 -> InputTag 변환
	void HandleAttackLight();
	// 가드 입력(RMB 홀드) — Started=Pressed(가드 시작), Completed=Released(가드 종료). Sprint 홀드 패턴 동형.
	void HandleGuardPressed();
	void HandleGuardReleased();
	void HandleDodge();
	void HandlePurify();
	void HandleExecute();
	void HandleCrouchPressed();
	void HandleSprintPressed();
	void HandleSprintReleased();
	void HandleInteractPressed();
	void HandleInteractReleased();
	void HandleLockOnPressed();
	void HandleSwitchTarget(const FInputActionValue& Value);
	void HandleSwitchTargetReleased();
	void HandleSummonWeaponPressed();
	void HandleWeaponSlot(EVBWeaponType RequestedType);

	// IA_Jump 입력 핸들러 — 지상(GASP-exact IsMovingOnGround 게이트)에서 트래버설 우선 시도, 불가 시 기존 점프.
	// 공중 입력은 bJumpInputHeld 만 세팅 → Tick 의 공중 맨틀 체크가 이어받음.
	void HandleJumpPressed();

	// IA_Jump 해제 — 공중 맨틀 홀드 게이트 해제 + StopJumping.
	void HandleJumpReleased();

	// 점프 키 홀드 상태 (소유 클라 전용 — 입력 이벤트는 owning client 에서만 발생하므로 복제 불필요).
	// 공중 맨틀(Vowbound 확장) 게이트: 점프부터 계속 홀드했거나 공중에서 재입력하면 true.
	bool bJumpInputHeld = false;
	// Sliding uncrouches in the next movement update. Keep a tapped jump until that update completes.
	float PendingSlideJumpUntil = 0.f;
	bool bSlideJumpAwaitingMovement = false;
	bool bSlideJumpReleaseRequested = false;
	
	void ApplySprintState(bool bNewSprinting);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerApplySprintState(bool bNewSprinting);

	// 플레이어 Gait 변경 (Walk/Run 만 허용, Sprint은 거부). 서버 권한으로 CMC 속도 갱신.
public:
	// 회복 캔슬 직후 짧은 구간의 이동 상태를 찍는다 (진단 전용, 콘솔 `vb.Combat.RecoveryCancelTrace 1`).
	// 왜 캐릭터가 드나: `UGameplayAbility::EndAbility` 가 그 GA 의 타이머를 전부 지운다(엔진 GameplayAbility.cpp,
	//   CVar AbilitySystem.ClearAbilityTimers 기본 켬). 회복 캔슬은 EndAbility 로 끝나므로 GA 가 타이머를 들면
	//   첫 발화 전에 사라진다 - 2026-09-05 회피 커밋에서 같은 자리에 이미 걸렸다.
	// 무엇을 가르나: 몽타주가 끊긴 뒤 캡슐 속도가 «남아서» 도는지, 아니면 블렌드아웃과 함께 죽는지.
	//   전자면 모션매칭 쿼리가 그 구간을 «달리는 중» 으로 읽는다.
	void BeginRecoveryCancelTrace();

	// 공격을 중간에 끊을 때 남는 «의도하지 않은 이동» 을 버린다.
	// 왜 필요한가: 회복 캔슬은 몽타주를 자연 종료보다 일찍 끊는데, 그 시점의 루트모션 속도는 캡슐에 남는다.
	//   2026-09-07 실측 - 카타나 3타 창 닫힘에서 Speed2D 305, 이동 입력 0. 모션매칭은 그 캡슐을 정직하게
	//   «달리는 중» 으로 읽어 주행 포즈를 고른다(user 관측 「3타 뒤 Idle 이 아니라 Run 갔다가 Idle」).
	// 무엇을 하나: ①이미 쌓인 속도를 버리고 ②남은 블렌드아웃 구간의 루트모션 «이동» 을 0 으로 눌러
	//   다음 프레임에 다시 밀리지 않게 한다. 자세 블렌드는 그대로라 화면은 부드럽게 이어진다.
	//   루트모션 스케일은 RestoreAfterSeconds 뒤 1.0 으로 되돌린다 - 이동 입력에는 영향이 없다(루트모션 전용).
	// 왜 GA 가 아니라 캐릭터가 드나: 되돌림 타이머 때문이다. `EndAbility` 가 그 GA 의 타이머를 전부 지운다.
	void StopResidualAttackMotion(float RestoreAfterSeconds, const UAnimMontage* SourceMontage);

private:
	void ApplyGait(EVBGait NewGait);


	UFUNCTION(Server, Reliable, WithValidation)
	void ServerApplyGait(EVBGait NewGait);

	// Walk 토글 입력 콜백 — Walk↔Run 전환 RPC 송신
	void HandleWalkToggle();

	// Attack 입력 판정을 서버에서 수행하기 위한 Server RPC.
	// ServerOnly GA의 ActivationOwnedTags(State_Combat_Attacking)는 서버 ASC에만 존재 →
	// 클라의 HasMatchingGameplayTag는 항상 false → 콤보 분기 진입 불가. 서버로 라우팅해 판정+디스패치.
	// 태그를 인자로 받지 않는다: 서버가 클라에게서 받은 태그를 그대로 SendGameplayEventToActor 로
	//   넘기면 클라가 Event.Montage.AttackTrace 를 보내 서버 데미지 트레이스를 임의로 발사할 수 있다.
	//   호출처는 상수만 넘기고 있었으므로 인자를 없애 그 상태 자체를 표현 불가능하게 만든다(FIND-085).
	//   _Validate 화이트리스트는 차선이다 - false 반환은 거부가 아니라 연결 종료라 오탐 비용이 크다.
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerHandleAttackInput();

	// 회피 커밋 중 들어온 공격 입력의 시각(월드 초, 서버). -1 = 없음. 커밋이 풀릴 때 EndDodgeCommit 이 ConsumePendingAttackInput 으로 꺼낸다.
	double PendingAttackInputTime = -1.0;
	// 회피 커밋 타이머. 캐릭터가 쥐는 이유: UGameplayAbility::EndAbility 가 GA 의 타이머를 전부 지우는데 커밋(0.45)이 무적(0.4)보다 길다.
	FTimerHandle DodgeCommitTimerHandle;
	// 커밋 해제: 태그를 내리고 저장 입력을 내보낸다(서버).
	void EndDodgeCommit(float BufferSeconds);

	void ApplySprintCMCState(bool bNewSprinting);

public:
	// 회피 커밋 시작(서버): State.Combat.DodgeCommit 을 세워 경공격을 막고 CancelSeconds 뒤 EndDodgeCommit. 카운트 고정(재회피로 2 가 되는 것 방지).
	void BeginDodgeCommit(float CancelSeconds, float BufferSeconds);
	// 회피가 취소(피격)로 끝났을 때: 커밋과 저장 입력을 함께 버린다.
	void CancelDodgeCommit();
	// 커밋이 풀릴 때 저장된 공격 입력을 내보낸다. 나이가 BufferSeconds 를 넘으면 버린다. 서버 전용.
	void ConsumePendingAttackInput(float BufferSeconds);
	// 저장된 공격 입력을 버린다(회피가 피격 등으로 취소됐을 때).
	void ClearPendingAttackInput();

	// 콤보 section 즉시 전환을 sim proxy에 강제 전달하기 위한 Multicast.
	// RepAnimMontageInfo Position 기반 복제가 3+ 연속 JumpToSection을 제대로 전달 못하는 UE 5.7 한계 회피.
	// 서버(authority)와 소유 클라는 자기 로컬 경로로 이미 jump 완료 → 이 Multicast에서 skip.
	// Sim proxy만 실제 AnimInstance->Montage_JumpToSection 호출.
	// VBGA_MeleeAttackBase::OnComboInputReceived에서 서버 authority 경로에서만 호출.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastJumpToMontageSection(UAnimMontage* Montage, FName SectionName);

private:
	
	
	// ── 카메라 리그 튜닝은 UVBCameraConfig 로 외부화 (데이터화 P1.1, 2026-07-24) ──
	// 미할당 시 CDO 디폴트 fallback (GetCameraConfig) — 배선 전에도 현행값 보존.
	// 적용 시점은 PostInitializeComponents 다. VBTargetLockComponent 가 자기 BeginPlay 에서 SpringArm/Camera 의
	// 평상시 값을 캡처하는데, AVBCharacter::BeginPlay 는 Super::BeginPlay() 로 컴포넌트를 먼저 시작시키므로
	// 거기서 적용하면 락온이 이미 옛 값을 캐시한 뒤다(전투 카메라 Lerp 가 생성자 상수로 되돌아감).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Camera")
	TObjectPtr<UVBCameraConfig> CameraConfig;


	// ── 이동/로코모션 튜닝은 UVBMovementConfig 로 외부화 (데이터화 P1.2, 2026-07-22) ──
	// CMC 물리(감속/가속/마찰/점프/에어컨트롤)·회전율 3종·무기별 속도 프로파일·sprint 램프 전량 이동.
	// 미할당 시 CDO 디폴트 fallback (GetMovementConfig) — 배선 전에도 현행값 보존.
	// 잔류(config 아님): 카메라 노브(위), 런타임 상태(SprintHoldStartTime/bWasSprinting),
	//   StrafeSpeedMapCurve(BP 에셋 참조라 CDO 가 못 담음 — 아래), WantsToStrafe(게임플레이 상태).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Movement")
	TObjectPtr<UVBMovementConfig> MovementConfig;
	// GASP slide loop duplicated with root locked; CMC alone moves the capsule.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Slide")
	TObjectPtr<UAnimSequence> SlideLoopAnimation;
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> ActiveSlideMontage;

	// 제자리 회전 프로필 (FIND-105, 2026-08-28). 미할당이면 행이 0개인 CDO 디폴트라 어떤 조합도
	//   «프로필 없음» 이 되고 회전은 종전 경로 그대로다 - 배선 전에도 동작이 안 바뀐다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|TurnInPlace")
	TObjectPtr<UVBTurnInPlaceConfig> TurnInPlaceConfig;
	// Sprint 2단 램프 런타임 상태 — UpdateMaxSpeed 가 bIsSprinting rising-edge 에서 시작시각 기록(에디터/복제 노출 불필요).
	float SprintHoldStartTime = 0.0f;
	bool  bWasSprinting = false;

	// ── 가드/패링 튜닝은 UVBParryConfig 로 외부화 (DEC-009 카타나 파일럿, 2026-07-25) ──
	// 미할당 시 CDO 디폴트 fallback (GetParryConfig).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Combat")
	TObjectPtr<UVBParryConfig> ParryConfig;
	// 가드 개시 서버 시각(초). <0 = 비가드. VBGA_Guard 가 서버 권위로 set/clear, 데미지 hub 가 읽어 패링 윈도우 판정.
	// 복제 불필요: 패링 판정은 서버(PostGameplayEffectExecute)에서만 일어나고 GuardStartServerTime 도 서버에서만 쓰인다.
	float GuardStartServerTime = -1.0f;
	// Accept(받아넘김) 재생 중 락아웃 종료 시각. 이 시각 전에는 BeginGuard 가 가드 몽타주를 재생하지 않는다 —
	// 탭 패링(RMB 톡톡) 시 다음 탭의 가드 Start 가 방금 재생한 Accept 를 덮어 자르는 것 방지(06-26 로그 실증).
	float GuardMontageLockUntil = 0.0f;
	// ── 가드 진입 시점에 확정된 몽타주 캐시 (무기별 가드 승격, 2026-07-26) ──
	// BeginGuard 가 "그때 그 무기"의 세트에서 뽑아 고정하는 값. EndGuard/PlayGuardAccept 는
	//   config 를 다시 조회하지 않고 오직 이 값만 읽는다.
	// 이 캐시가 없으면 무기별 승격 자체가 버그를 만든다:
	//   가드는 무기 교체를 살아남는다. HandleWeaponSlot 이 GA 를 안 거치고 WSC->SummonWeapon 직행이라
	//   ActivationBlockedTags 로 원천 차단이 불가능하다. 몽타주가 무기별이 된 뒤 EndGuard 가 "지금 무기"로
	//   재해석하면 —
	//     카타나 가드(진입 −20cm 백스텝) → 2번 키(BigSword) → RMB 뗌
	//       → Montage_IsActive(BigSword 몽타주) == false (실제 재생 중인 건 카타나 것)
	//       → End 섹션 점프 스킵 → +20cm 복귀 영영 없음 → 교체할 때마다 뒤로 누적
	//   user 06-26 "복귀 안되던데" 증상의 재발이다.
	// 설계상 의미: "같은 필드를 BeginGuard 와 EndGuard 가 함께 읽으므로 어긋날 수 없다"는 기존 불변식을
	//   무기 축까지 확장한 것 — 새 개념이 아니라 기존 불변식의 보존.
	// bare UPROPERTY() 이므로 Category 금지: UObject 포인터라 GC 추적은 필요하지만 에디터/BP 노출은 없다.
	//   노출 키워드 없는 UPROPERTY 에 Category 를 붙이면 UHT 경고 → WarningsAsErrors → 빌드 실패(2026-04-20 사고).
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveGuardMontage = nullptr;

	// 위 가드 몽타주와 짝인 받아넘김. PlayGuardAccept 는 hub 가 피격 시점에 부르므로, 거기서 '지금 무기'로
	//  재해석하면 재생 중인 가드 포즈(옛 무기)와 받아넘김(새 무기)이 갈린다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveParryAcceptMontage = nullptr;

	// 위 몽타주와 짝인 해제 섹션명. 몽타주와 같은 세트에서 함께 캐시해 교차 조합을 원천 차단.
	// UPROPERTY 아님 — FName 은 GC 참조가 아니다(SprintHoldStartTime 관례 동형).
	FName ActiveGuardEndSection = NAME_None;

	// ※ bGuardSectionContractChecked 는 삭제됨 — UVBParryConfig::IsDataValid 가 대체(CHORE-031).
	//    래치는 인스턴스 스코프인데 계약은 에셋 스코프라, 무기가 둘이 되는 지금 그 구멍이 실제로 뚫린다.

	// 방향(0~180°) → zone 매핑 curve. 6 keys: (0,0)/(30,0)/(80,1)/(100,1)/(150,2)/(180,2) Linear.
	// 출력 0=Forward, 1=Strafe, 2=Backward zone. UpdateMaxSpeed() 에서 zone 값으로 X/Y/Z 사이 lerp.
	// 배선 자산은 Curve_VB_StrafeSpeedMap(Content/Vowbound/Data). 2026-08-14 이전에는 GASP 원본
	//   Curve_StrafeSpeedMap(Content/Blueprints)을 물고 있었고 내용 동일한 우리 사본이 고아로 남아 있었다 -
	//   튜닝하러 우리 폴더를 열면 아무 일도 일어나지 않는 상태였다(FIND-087). GASP 사본은 삭제했다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Locomotion")
	TObjectPtr<UCurveFloat> StrafeSpeedMapCurve;

	// GASP WantsToStrafe 대응. 기본 true = Strafe(캡슐이 카메라 추종). (FIND-019 FEEL-3, 2026-06-06 복원)
	// VB 게임 디자인의 기본 조작감: D키=Run_R 사이드스텝, idle 헤드룩(AO)+TIP — 전부 Strafe 모드 경험.
	// 이력: 2026-06-04(b37c824) 트래버설 종료 "회전 뚝 끊김" 대응으로 false(Free) 플립 → 그 컷은 같은 날
	//   OffsetRootBone IsSlotActive 근본 수정으로 해소됨(tool-traps 06-04) → 워크어라운드만 남아 비무장
	//   조작감 전체가 Free로 바뀌어 있었다(D키가 몸 돌려 전진). 원인 치료 후 워크어라운드 은퇴.
	// 주의: 트래버설 종료 회전 FEEL 회귀 체크 필수. RotationMode enum(UpdateStates)은 MM 선택 품질을 위해
	//   여전히 Free 유지 — TIP 게이트는 bCMCStrafeRotation(CMC 진실)로 판정하므로 이원화 무해.
	// meta=AllowPrivateAccess — private 유지하면서 BP에선 Getter/Setter로 접근.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing=OnRepGASPRotationIntent, Category="Vowbound|Locomotion", meta=(AllowPrivateAccess="true"))
	bool WantsToStrafe = true;

public:
	FORCEINLINE bool GetWantsToStrafe() const { return WantsToStrafe; }
	void SetWantsToStrafe(bool bNewValue);

	// Strafe(카메라 방향 고정) / Free(이동 방향 회전) 모드 적용 — 회전 플래그의 단일 진실원.
	// 정책(DEC-006 ⑧, 2026-06-07 user 확정): Sprint→Free / 전투(상태2·3)·LockedOn→Strafe /
	//   평상(Unarmed)→WantsToStrafe(기본 true). 상태2(전투·무기Off)도 카메라 페이싱 유지 —
	//   "돌아서 달림"은 user 거부 (06-07 CombatSheathed→Free 시도 즉시 원복 이력).
	// public 인 이유: WSC 상태 전환이 재평가를 위임 (이중 작성자 금지 — 06-07 PIE 플래그 배터리로 실측된 교훈).
	void ApplyRotationMode();
	// CMC 회전 직전/Character Tick의 공유 회전율 정책. 이동·포즈 소유 경계를 현재 상태로 확인한다.
	void UpdateRotationRateForMovementMode();

	// 이동/로코모션 튜닝 config 접근자 — 미할당 시 CDO 디폴트 fallback (P0 config 패턴 동형).
	const UVBMovementConfig* GetMovementConfig() const;

	// 제자리 회전 프로필 접근자 — 동일 폴백 규약. 애님 인스턴스가 이 답으로 회전 경로를 고른다.
	const UVBTurnInPlaceConfig* GetTurnInPlaceConfig() const;

	// 카메라 리그 튜닝 config 접근자 — 동일 폴백 규약 (P1.1).
	const UVBCameraConfig* GetCameraConfig() const;

	// ── IVBGuardable 구현 (FIND-057a) ── 데미지 hub 는 이 4개만 인터페이스로 호출한다.
	//  BeginGuard/EndGuard 는 인터페이스에 없다(소비처 = VBGA_Guard 단 하나, 근거는 VBGuardable.h 주석).
	// 가드/패링 config 접근자 — 미할당 시 CDO 디폴트 fallback (P0 config 패턴 동형 = IVBGuardable 계약 1: null 아님).
	virtual const UVBParryConfig* GetParryConfig() const override;
	virtual void OpenRiposteWindow(float Duration, AActor* Attacker) override;
	virtual void ApplyGuardStaminaDelta(float Delta) override;
	// 가드 중인가 (GuardStartServerTime >= 0).
	virtual bool  IsGuarding() const override { return GuardStartServerTime >= 0.0f; }
	// 가드 개시 후 경과(초). 비가드면 큰 값(패링 윈도우 밖 취급).
	virtual float GetGuardElapsed() const override;
	// 패링 성공 순간 연출(받아넘김 몽타주). 데미지 hub 가 패링 확정 시 호출. 경직(공격자)은 hub 가 별도 처리.
	virtual void  PlayGuardAccept() override;

	// ── 가드 개시/종료 (서버 권위, VBGA_Guard 전용 — 인터페이스 비포함) ──
	// 가드 개시: 서버 시각 기록 + (config 몽타주 있으면) 재생.
	void BeginGuard();
	// 가드 종료: 상태 클리어 + 몽타주 정지.
	void EndGuard();

	// ── 사망 (FIND-062) ── AVBEnemyBase 의 사망 4함수와 1:1 대칭 구조. 새 태그/컴포넌트 0개.
	// 사망 여부 — ASC 의 State.Dead 보유 판정. AVBEnemyBase::IsDead 와 동일 계약.
	// 적과 다른 점: 플레이어는 owning connection 이 있고 ASC 복제모드가 Mixed 라 CountToOwner loose 태그가
	//   소유 클라에도 실제 복제된다 → 적이 필요로 했던 "Multicast 로 태그 로컬 보완"이 불필요하다.

	bool IsDead() const;

protected:
	// 사망 처리(서버 전용). 태그 부여 → 능력 취소 → 락온 해제 → 코스메틱 멀티캐스트 순서로 진행한다.
	// AVBEnemyBase::HandleDeath 대칭이되 명성 처리는 없다(플레이어는 명성의 대상이 아니라 주체).
	virtual void HandleDeath();

	// 사망 애니/입력차단을 모든 Net Role 에 동시 적용. 사망 자세와 슬로모가 같은 프레임에 걸리도록
	// 타이밍을 동기화하는 것이 이 멀티캐스트의 존재 이유(별도 Client_ RPC 를 만들지 않는 근거).
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHandleDeathCosmetic();

	// 위 멀티캐스트의 실체. 각 머신에서 로컬로만 작동(복제 없음).
	void HandleDeathCosmetic();

	// 무기 미등록 시 쓰는 기본 사망 몽타주. 구조는 [Die 섹션] + [Loop 섹션] 2단이다.
	// 몽타주여야 하는 이유: 루트모션은 RootMotionFromMontagesOnly 라 AnimSequence 를 PlayAnimation 으로
	//   틀면 캡슐에 전달되지 않는다. 그러면 루트 변위가 포즈로만 적용돼 몸만 캡슐 밖으로 미끄러진다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	TObjectPtr<UAnimMontage> DeathMontage;

	// 무기별 사망 몽타주. 미등록 무기는 위 DeathMontage 로 폴백한다.
	// 폴백을 두는 이유(가드와 다른 점): 가드는 무기 무관 범용 클립이 없어서 미등록=가드 불가가 옳았지만,
	//   사망은 어떤 무기를 들었든 반드시 쓰러져야 한다. 폴백이 없으면 미등록 무기로 죽을 때 선 채로 굳는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	TMap<EVBWeaponType, TObjectPtr<UAnimMontage>> WeaponDeathMontages;

	// 사망 몽타주의 정착 섹션 이름. 이 섹션을 자기 자신으로 링크해 시체 자세를 영구 유지한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Death")
	FName DeathLoopSection = FName("Loop");

	// 현재 무기에 맞는 사망 몽타주를 고른다. 없으면 DeathMontage, 그것도 없으면 nullptr.
	UAnimMontage* ResolveDeathMontage() const;

	// 반격 창 만료 타이머. 창은 태그로 표현되고 이 핸들이 그 수명을 쥔다.
	FTimerHandle RiposteWindowHandle;



private:
	// Health 변경 감지 → 사망 판정. PossessedBy(서버 전용)의 HasAuthority 블록 끝에서 1회 바인드한다.
	void OnHealthChanged(const struct FOnAttributeChangeData& Data);

private:
	// 회복 캔슬 추적 타이머와 남은 표본 수 (진단 전용).
	// 남은 이동을 누르는 구간의 마감 시각(월드 시간). 0 이하면 비활성.
	// 왜 시각인가: 한 번만 지우면 같은 프레임에 루트모션이 다시 덮어쓴다 - 2026-09-07 [ComboTrace] 실측으로
	//   확인했다(캔슬 278 -> 다음 프레임 272, 이후 관성 감속). 그 구간 내내 매 tick 눌러야 이긴다.
	double       ResidualMotionSuppressUntil = 0.0;
	int32 ResidualMotionMontageInstanceId = INDEX_NONE;
	FTimerHandle RecoveryCancelTraceTimer;
	int32        RecoveryCancelTraceLeft = 0;

	// BeginPlay 에서 CMC 물리 baseline(감속/가속/마찰/점프/에어컨트롤/crouch/회전율)을 config 로부터 적용.
	// 생성자 대신 여기서 하는 이유: 생성자 시점엔 MovementConfig(BP 배선) 미해석 → 자산값 반영 불가.
	void ApplyMovementConfigToCMC();

	// PostInitializeComponents 에서 SpringArm/Camera 에 config 적용 (적용 시점 근거는 CameraConfig 필드 주석 참조).
	void ApplyCameraConfig();

	// LocomotionState 변경 시 CMC 속도 재계산 (WSC.OnLocomotionStateChanged 델리게이트에 바인딩)
	UFUNCTION()
	void OnLocomotionStateChanged(EVBLocomotionState NewState, EVBWeaponType NewWeaponType);

	// 매 tick CMC->MaxWalkSpeed 를 방향별 (Forward/Strafe/Backward) 속도로 갱신 (GASP CalculateMaxSpeed 이식).
	// velocity vs ActorForward 각도 → StrafeSpeedMapCurve(0~2) → gait_speeds.(X|Y|Z) lerp → MaxWalkSpeed.
	// 결과: backward 입력 시 ~300cm/s 로 캡 → MM trajectory 가 적정 거리 예측 → Run_Start_B 정상 매칭.
	void UpdateMaxSpeed();

	// TargetLock on/off 시 회전 모드 재적용을 위해 TargetLockComponent 델리게이트에 바인딩.
	UFUNCTION()
	void OnTargetLocked(AActor* TargetActor);
	UFUNCTION()
	void OnTargetUnlocked(AActor* TargetActor);
	
	
	// 마우스 타겟 전환 튜닝값(SwitchTargetMouseThreshold/Cooldown)은 UVBTargetLockConfig 로 이동(데이터화 P0).
	// FPlatformTime 기반 쿨다운 — 런타임 상태라 캐릭터에 잔류.
	// 대상 전환의 입력 상태는 TargetLockComponent가 소유한다.
};
