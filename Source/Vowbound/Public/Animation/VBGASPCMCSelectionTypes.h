#pragma once

#include "CoreMinimal.h"
#include "AlphaBlend.h"
#include "Animation/VBAnimationTypes.h"
#include "Types/VBWeaponStateTypes.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "VBGASPCMCSelectionTypes.generated.h"

class UAnimationAsset;
class UAnimInstance;
class UBlendProfile;
class UChooserTable;
class UPoseSearchDatabase;

/** CMC playback role, independent of weapon/gait database selection. */
UENUM(BlueprintType)
enum class EVBGASPCMCPhase : uint8
{
	Any,
	Loop,
	Transition
};

/** Value-only candidate context. GT captures weapon data; CMC selection supplies its own state. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPCMCWeaponQuery
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float MMDatabaseLOD = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBWeaponType MMWeaponType = EVBWeaponType::None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBLocomotionState LocomotionState = EVBLocomotionState::Unarmed;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBMovementMode MovementMode = EVBMovementMode::Grounded;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBStance Stance = EVBStance::Stand;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBMMState MMState = EVBMMState::Idle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") EVBGait Gait = EVBGait::Run;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float Speed2D = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float TimeToLand = 999.0f;
	/** Raw collision prediction is distinct from the main instance's TimeToLand() helper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float TrajectoryTimeToLand = 999.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") float MMSprintGateSpeed = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool IsStarting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool IsPivoting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool JustTraversed = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool JustLandedLight = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool JustLandedHeavy = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool ShouldSpinTransition = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC") bool ShouldTurnInPlace = false;
	UPROPERTY(Transient) bool bTurnProfileOwnsTIP = false;
};

/** Inputs authored per character; angles are degrees and normals are world-space. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGSlopeSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC", meta=(ClampMin="0"))
	float NormalInterpSpeed = 5.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC", meta=(ClampMin="0"))
	float DeadZoneDegrees = 15.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC", meta=(ClampMin="0.01"))
	float MaxSlopeDegrees = 30.0f;
};

/** Chooser context 1. Each evaluation starts with a fresh value. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPCMCChooserOutput
{
	GENERATED_BODY()
	/** Input snapshot of the unchanged weapon MasterChooser's active pools. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TArray<TObjectPtr<UPoseSearchDatabase>> AllowedDatabases;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EVBGASPCMCPhase Phase = EVBGASPCMCPhase::Any;
	/** Filter feedback distinguishes a transient empty phase from a corrupt table. */
	bool bHasPhaseCandidates = false;
	bool bPhaseDataValid = true;
	/** Required for production weapon selections, optional for isolated filter tests. */
	bool bRequireCandidateChooser = false;
	bool bCandidateChooserEvaluated = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UPoseSearchDatabase> SourceDatabase = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EPoseSearchInterruptMode InterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float StartTime = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float SearchCost = MAX_flt;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float MaxAcceptedCost = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bPoseMatched = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bMirror = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bForceBlend = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float BlendTime = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	FName BlendProfileName = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EAlphaBlendOption BlendCurve = EAlphaBlendOption::QuadraticInOut;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TArray<FName> Tags;
	/** Initialized by native code, before filter columns are evaluated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bHistoryReady = false;
	/** True only on explicitly authored deterministic startup/failure rows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bFallback = false;
};

/** Committed playback command. Only BlendParameters changes between selections. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPCMCBlendInputs
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UAnimationAsset> Anim = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bLoop = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float StartTime = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	bool bMirror = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	float BlendTime = 0.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UBlendProfile> BlendProfile = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	EAlphaBlendOption BlendCurve = EAlphaBlendOption::QuadraticInOut;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	TArray<FName> Tags;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="GASP|CMC")
	FVector BlendParameters = FVector::ZeroVector;
};

/** Instance-owned state on the linked CMC AnimBlueprint; no process-global cache. */
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBGASPCMCState
{
	GENERATED_BODY()
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bUsesWeaponLocomotion = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UAnimInstance> SelectionContext = nullptr;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	TObjectPtr<UChooserTable> ActiveChooser = nullptr;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	TArray<TObjectPtr<UPoseSearchDatabase>> AllowedDatabases;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 WeaponSelectionKey = 0;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	EPoseSearchInterruptMode CurrentInterruptMode = EPoseSearchInterruptMode::DoNotInterrupt;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 CapturedWeaponPool = -1;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVBGASPCMCWeaponQuery WeaponQuery;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	float SecondsSinceSearch = 0.0f;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bNeedsReselect = true;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVector GroundNormal = FVector::UpVector;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bHasWalkableFloor = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVector SmoothedGroundNormal = FVector::UpVector;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVector2D SlopeAngle = FVector2D::ZeroVector;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSlopeInitialized = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSetupWasActive = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bHistoryWasReady = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bHasSelection = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSelectionContractFailed = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 SelectedWeaponKey = 0;
	/** Actual captured weapon pool; SelectedWeaponKey is only a database-set hash. */
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	int32 SelectedWeaponPool = -1;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bSelectedWeaponLocomotion = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bWaitingForPhase = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bTransitionToLoopRequested = false;
	// 콘텐츠 없는 피벗 재진입 막기(.sw/crouch-mm-completion/pivot-guard-design.md, 2026-10-04): 무장 길에서 피벗 중에 들어간 전이가
	//   반복 클립으로 대체되면 켠다. 켜져 있는 동안 UVBGASPLocomotionLibrary::IsPivoting 이 거짓이다 - 피벗 원판정(IsPivotingRaw)이 한 번 거짓이면 끈다.
	//   설정 켜짐·꺼짐, 경로(무장/맨손)·무기 풀이 바뀌면 끈다. 맨손 길에서는 켜지지 않는다(대체가 무장 길에만 있다).
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bPivotFallbackLatched = false;
	// 래치를 켤 때의 자세(앉음이면 참). 래치는 «이 풀·이 자세에 피벗 콘텐츠가 없다» 는 사실이라 자세가 바뀌면 지운다 -
	//   같은 무기로 앉았다 서면 서기 풀의 저작된 피벗을 막지 않게(Codex 01 검토 10-04). 무기 풀 번호에는 자세가 없다.
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	bool bPivotFallbackCrouched = false;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	uint8 SelectedState = 0;
	UPROPERTY(Transient, BlueprintReadWrite, Category="GASP|CMC")
	FVBGASPCMCChooserOutput LastSelection;
	// Serial counters are native-only: a Blueprint persistent bool must never restart every frame.
	uint64 SelectionSerial = 0;
	uint64 PendingForceSerial = 0;
	uint64 ConsumedForceSerial = 0;
};
