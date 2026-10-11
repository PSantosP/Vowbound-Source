// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PoseSearch/PoseSearchResult.h"
#include "VBFinisherComponent.generated.h"

class UAnimMontage;
class UContextualAnimSceneActorComponent;
class UVBFinisherConfig;
class UMultiAnimAsset;
class UAnimInstance;
class UAnimSequenceBase;
class USkeletalMeshComponent;

/** 서버의 선택만 복제한다. AnimInstance 포인터는 수신 월드의 참가자에서 재구성한다. */
USTRUCT()
struct FVBInteractionReplay
{
	GENERATED_BODY()
	UPROPERTY() int32 SessionId = 0;
	UPROPERTY() bool bActive = false;
	UPROPERTY() bool bVictimReleased = false;
	UPROPERTY() TObjectPtr<UMultiAnimAsset> Asset = nullptr;
	UPROPERTY() TObjectPtr<UPoseSearchDatabase> Database = nullptr;
	UPROPERTY() TObjectPtr<const UVBFinisherConfig> Config = nullptr;
	UPROPERTY() TArray<TObjectPtr<AActor>> Actors;
	UPROPERTY() TArray<float> StartTimes;
	UPROPERTY() TArray<float> PlayRates;
	UPROPERTY() TArray<uint8> SupersededMontagePlayIds;
	UPROPERTY() TArray<FTransform> RootTransforms;
	UPROPERTY() TArray<FTransform> RootBoneTransforms;
	UPROPERTY() FVector BlendParameters = FVector::ZeroVector;
	UPROPERTY() double ServerStartTime = 0.0;
};

/**
 * 처형 장면이 끝났다. 성공적으로 끝났든 중간에 빠졌든 한 번만 온다.
 *
 * 왜 우리 델리게이트인가: 플러그인의 OnLeftSceneDelegate 를 어빌리티가 직접 구독하면 플러그인 타입이
 *   어빌리티까지 샌다. 접점을 이 컴포넌트로 모은 뜻이 없어진다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVBFinisherEndedDelegate);

// 피해자 notify가 물리 전환 시점에 도달했다. 인자는 그 시점의 저작 루트 속도(월드 cm/s)다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBInteractionVictimLanded, FVector, LaunchVelocity);

/**
 * 진행 중인 처형의 설정이 이 기계에 «도착했다». 시작 신호가 아니라 도착 신호다.
 *
 * 왜 static 인가: 듣는 쪽(연출)은 어느 액터가 처형을 걸지 미리 모른다. 액터별 델리게이트면
 *   모든 후보에 구독을 걸어야 하고, 그러면 등록 배선이 생겨 빠뜨릴 자리가 늘어난다.
 *   클래스 하나에 붙여 두면 듣는 쪽이 한 번만 구독한다.
 * 왜 이쪽이 «연출» 을 직접 부르지 않는가: 그러면 장면이 연출을 알게 되어 의존이 거꾸로 선다.
 *   여기는 사실만 알리고 누가 듣는지 모른다.
 * 인자: 공격자 액터(설정을 든 컴포넌트의 소유자), 도착한 설정(장면이 끝나면 nullptr).
 * 주의: PIE 는 월드가 여럿이다. 듣는 쪽이 Attacker->GetWorld() 로 자기 월드인지 갈라야 한다.
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBFinisherConfigArrived, AActor* /*Attacker*/,
                                     const UVBFinisherConfig* /*Config*/);

/**
 * 처형 장면의 시작과 끝을 소유한다. 그리고 ContextualAnimation 플러그인 API 를 보는 유일한 곳이다.
 *
 * 무엇: 대상에게 걸 처형 고르기 / 씬 시작 / 섹션 전환 / 조기 이탈.
 * 왜 한 파일로 모으나: 플러그인이 Experimental 이라 API 가 엔진 버전마다 바뀔 수 있다. 접점이 흩어지면
 *   그때 고칠 곳이 여럿이 되고, 하나를 빠뜨리면 컴파일은 되는데 장면만 안 걸리는 무음 실패가 된다.
 *   어빌리티와 카메라는 이 컴포넌트가 돌려주는 값만 받고 플러그인 타입을 직접 보지 않는다.
 * 전제: 소유 액터에 UVBWeaponStateComponent 가 있어야 처형을 고를 수 있다(무기 데이터에 목록이 있다).
 * 전제: 씬에 참가하는 두 액터가 플러그인의 SceneActorComponent 를 이미 들고 있어야 한다.
 *   AVBCharacter 와 AVBEnemyBase 의 생성자가 그것을 보장한다 - 이 컴포넌트는 찾기만 하고 만들지 않는다.
 * 부작용: 씬 시작 시 공격자를 클립이 기대하는 자리로 옮긴다(StartFinisher 주석).
 *
 * 범위: 시작·전환·이탈만 든다. 카메라와 슬로우는 여기 없다 - 그것은 «연출» 이고 이 컴포넌트는 «장면» 이다.
 *   섞으면 플러그인 접점을 한 곳으로 모은 뜻이 없어진다.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBFinisherComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBFinisherComponent();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 기존 완료 델리게이트를 유지하면서 피해 확정 여부를 구분한다. */
	bool WasLastFinisherCancelled() const { return bLastFinisherCancelled; }

	/** 저작된 native notify가 해당 참가자의 현재 세션에만 전달된다. */
	static bool NotifyInteractionRagdoll(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, int32 MontageInstanceId);

	// 처형 시작 때 «적» 을 제자리로 데려오는 보정. 남은 양을 매 틱 조금씩 «더한다».
	//
	// 왜 더하나(세팅이 아니라): 처형 몽타주에는 루트모션이 있어서 적이 스스로도 움직인다.
	//   절대 위치를 매 틱 세팅하면 그 이동을 지워 버리고, 한 번만 세팅하면 순간이동이 된다.
	//   «남은 어긋남» 만 나눠서 더하면 루트모션과 겹치지 않고 보정만 소진된다.
	// 왜 서버만: 적의 이동은 서버 권한이고 복제로 클라에 간다. 양쪽에서 밀면 두 배로 간다.
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

	// 남은 보정량. 0 이 되면 틱을 끈다 - 할 일이 없는 틱은 켜 두지 않는다.
	FVector  PendingAlignOffset = FVector::ZeroVector;
	float    PendingAlignYaw    = 0.0f;
	float    PendingAlignTime   = 0.0f;
	TWeakObjectPtr<AActor> PendingAlignActor;

	// 적을 제자리로 데려오기 시작한다. 시간이 0 이면 그 자리에서 다 민다.
	void BeginAlign(AActor& Victim, const FTransform& Desired, float BlendTime);

	/**
	 * 대상에게 걸 처형을 고른다. 없으면 nullptr.
	 *
	 * 고르는 규칙: 현재 무기의 FinisherConfigs 에서 «접근 방향이 맞는 것들» 중 랜덤.
	 *   피해자의 정면과 피해자에서 공격자로 향하는 수평 방향으로 앞/뒤를 구분한다.
	 * 왜 여기인가: 무기 데이터의 생김새를 아는 곳을 하나로 둔다. 어빌리티가 직접 뒤지면
	 *   무기 데이터가 바뀔 때 고칠 곳이 둘이 된다.
	 * 목록이 비어 있는 것은 정상이다 - 처형 자산이 아직 없는 무기가 그렇다. 그때 nullptr 를
	 *   돌려주고, «말할 수는 있으므로» 무기당 한 번만 경고를 남긴다(입력마다 찍으면 로그가 죽는다).
	 */
	const UVBFinisherConfig* SelectFinisher(const AActor* Target) const;

	/** 씬을 시작한다. 서버 권한에서만 성립한다. 실패하면 false 이고 아무 상태도 안 남긴다. */
	bool StartFinisher(const UVBFinisherConfig* Config, AActor* Target);

	/**
	 * GASP 5.8 경로로 처형을 시작한다. 성공하면 두 액터가 상호작용 클립에 들어가 있다.
	 *
	 * 옛 경로(StartFinisher)와 무엇이 다른가:
	 *   - 짝을 «자산 하나» 로 고르지 않는다. chooser 가 후보 DB 들을 내고 모션매칭이 지금 두 몸의
	 *     자세를 보고 클립을 고른다. 그래서 접근 방향(Front/Back) 판정이 통째로 사라진다 -
	 *     PSIA 가 F/B/L/R 넷을 들고 있고 고르는 것은 검색이다.
	 *   - 자리 맞춤을 코드가 안 한다. 상호작용 클립에 붙은 UVBAnimNotifyState_InteractionWarp 가
	 *     모션워핑 목표를 내려놓고 루트모션이 휘어서 간다. BeginAlign 도 캡슐 텔레포트도 안 쓴다.
	 *
	 * 전제: 공격자 ABP 에 FinisherChooserTable 이 배선돼 있어야 한다. 비어 있으면 false 를 돌려주고
	 *   부르는 쪽이 옛 경로로 간다 - 이행기 동안 둘이 공존하는 유일한 분기점이다.
	 * 전제: 두 액터 모두 UVBAnimInstance 와 UMotionWarpingComponent 를 들어야 한다.
	 *   워핑 컴포넌트가 없으면 클립은 돌고 자리만 안 맞는다(엔진이 조용하다) - 그래서 여기서 막는다.
	 * 참고: 2026-09-19 GASP 실측 = `SandboxCharacter_Mover_Ragdoll::Try_MultiCharacterInteraction`.
	 */
	bool StartInteractionFinisher(AActor* Target);

	/**
	 * 새 경로를 걸 수 «있는가». 검색은 돌리지 않는다 - 조건만 본다.
	 *
	 * 왜 따로 두나: 부르는 쪽이 진행 중인 자기 공격을 끊고 나서 시작해야 하는데(그 이유는
	 *   UVBGA_Execute 의 취소 주석), 걸 수 없는 상황에서 끊으면 아무 일도 안 일어나면서 공격만 끊긴다.
	 *   그래서 끊기 전에 먼저 묻는다. 판정 본문은 StartInteractionFinisher 가 다시 쓰므로 한 곳이다.
	 */
	bool CanStartInteractionFinisher(const AActor* Target) const;

	/** 배선 여부와 시작 가능 여부를 구분한다. 잘못된 native 데이터가 legacy 피해로 우회하지 않는다. */
	bool HasInteractionFinisherData() const;

	/** 연출 소비자는 ContextualAnimation/MMI의 내부 바인딩을 구분하지 않는다. */
	AActor* GetParticipant(FName Role) const;
	void GetParticipants(TArray<AActor*>& OutActors) const;
	float GetPlaybackTime() const;

	/** 다음 단계로 넘긴다(프롬프트 성공 등). 씬이 안 돌고 있으면 false. */
	bool TransitionSection(FName SectionName);

	/** 중간 이탈. 두 액터 모두 멈춘다. 씬이 안 돌고 있으면 아무 일도 안 한다. */
	void EarlyOut();

	/** 지금 이 액터가 처형 장면 안에 있는가. */
	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher")
	bool IsPlaying() const;

	/**
	 * 임의의 액터가 지금 처형 장면 안에 있는가. 피해자 쪽에서 묻기 위한 정적 질의다.
	 *
	 * 왜 정적인가: 피해자(적)에는 UVBFinisherComponent 가 없다 - 그 컴포넌트는 AVBCharacter 생성자가
	 *   붙이는데 AVBEnemyBase 는 ACharacter 를 직접 상속한다. 피해자에게 붙는 것은 플러그인의
	 *   SceneActorComponent 뿐이고, 그것을 보는 코드는 이 파일 안에만 있어야 한다.
	 */
	static bool IsActorInScene(const AActor* Actor);

	/**
	 * 플러그인의 SceneActorComponent 를 찾는다. 없으면 nullptr 이고 여기서 만들지 않는다.
	 *
	 * 왜 공개인가: 연출 쪽(UVBFinisherDirectorSubsystem·AVBFinisherCamera)이 씬 재생 시간과
	 *   바인딩을 읽어야 한다. 그러자고 플러그인 헤더를 그쪽까지 들이면 「접점을 한 파일로 모은다」가
	 *   무너진다. 접점은 여기 하나로 두고 조회만 빌려준다 - IsActorInScene 이 공개인 것과 같은 이유다.
	 *
	 * 왜 안 만드나: 만드는 자리는 AVBCharacter 와 AVBEnemyBase 의 생성자다. 종전에는 이 함수가
	 *   처형 시작 순간에 붙였는데, 그러면 공격자의 씬 바인딩과 피해자의 새 컴포넌트가 서로 다른
	 *   액터 채널로 가서 도착 순서가 보장되지 않는다. 늦게 도착한 원격 클라에서는 피해자만 씬에
	 *   안 들어가 플레이어가 허공에 처형한다. 막을 수 있는 자리에서 막았으므로(생성자) 여기는
	 *   말만 한다(규칙 27).
	 * 못 찾는 경우: 두 기초 클래스를 안 거치는 액터가 처형에 끼어든 것이다. 설정 오류이므로
	 *   조용히 넘어가지 않고 경고한다 - 증상이 «아무 일도 안 일어남» 이라 로그가 유일한 단서다.
	 */
	static UContextualAnimSceneActorComponent* FindSceneActorComponent(const AActor* Actor);

	/** 진행 중인 처형의 설정. 없으면 nullptr - 카메라와 어빌리티가 이것으로 연출 값을 읽는다. */
	UFUNCTION(BlueprintPure, Category="Vowbound|Finisher")
	const UVBFinisherConfig* GetActiveConfig() const { return ActiveConfig; }

	// 장면이 끝나면 발화. 어빌리티가 이것으로 피해 적용과 종료를 한다.
	// 서버 전용이다 - 구독이 StartFinisher 안에서 걸리고 그 함수는 권한에서만 돈다.
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Finisher")
	FVBFinisherEndedDelegate OnFinisherEnded;

	// 기존 델리게이트 이름은 유지한다. 실제 의미는 피해자 native notify의 물리 전환 시점이다.
	// 서버에서 세션당 한 번 발화하고, 공격자 몽타주는 계속 재생한다.
	UPROPERTY(BlueprintAssignable, Category="Vowbound|Finisher")
	FVBInteractionVictimLanded OnInteractionVictimLanded;

	// 실제 notify 시점에 샘플링한 루트 속도의 연출 배율.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.1", ClampMax="10.0"))
	float InteractionRagdollLaunchScale = 1.0f;

	// 설정이 복제로 도착했을 때 모든 기계에서 발화한다. 위 델리게이트와 달리 클라에서도 온다.
	// 선언 주석에 무엇을 의미하고 무엇을 의미하지 않는지 적어 두었다.
	static FVBFinisherConfigArrived OnConfigArrived;

private:
	friend class FVBFinisherLifecycleTest;
	// 플러그인의 OnLeftSceneDelegate 를 받아 우리 델리게이트로 옮긴다. 그 사이에서 상태도 비운다.
	UFUNCTION()
	void HandleLeftScene(UContextualAnimSceneActorComponent* SceneActorComponent);

	/**
	 * 우리 두 골격에 안 맞는 후보 DB 를 목록에서 뺀다.
	 *
	 * 왜 필요한가: 엔진의 상호작용 제약 갱신은 짝의 골격이 스키마 선언과 같다고 믿고, 다르면
	 *   막지 않고 크래시한다(2026-09-19 user PIE. 자세한 것은 UVBInteractionIKLibrary 헤더).
	 *   GASP 이관분은 두 역할 다 UEFN 골격이라 우리 적(SK_Mannequin)과는 안 맞는다.
	 * 버리는 이유를 로그로 밝힌다 - 조용히 버리면 「왜 처형이 안 걸리지」로 보인다.
	 */
	static void FilterDatabasesBySkeleton(TArray<class UPoseSearchDatabase*>& InOutDatabases,
	                                      const AActor* Attacker, const AActor* Victim);
	void GatherInteractionDatabases(TArray<UPoseSearchDatabase*>& OutDatabases, const AActor* Target = nullptr) const;
	const UVBFinisherConfig* FindInteractionConfig(const UPoseSearchDatabase* Database) const;

	// 상호작용 처형이 끝났다(공격자 몽타주 종료). 두 쪽 결과를 비우고 우리 델리게이트로 옮긴다.
	//
	// 왜 공격자 몽타주만 보나: 한 장면에 끝나는 자리가 둘이면 «먼저 끝난 쪽» 이 정리를 해 버려
	//   나머지가 상태 없이 계속 돈다. 시작을 건 쪽이 끝도 건다.
	UFUNCTION()
	void HandleInteractionMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation);

	// 전환 notify 없이 피해자 재생이 끝나면 피해 없이 세션을 취소한다.
	UFUNCTION()
	void HandleInteractionVictimMontageEnded(UAnimMontage* Montage, bool bInterrupted, int32 Generation);
	UFUNCTION()
	void HandleInteractionBlendOut(UAnimMontage* Montage, bool bInterrupted, int32 Generation);
	UFUNCTION()
	void HandleInteractionParticipantDestroyed(AActor* Actor);

	bool PlayInteractionLocally(const TArray<FPoseSearchBlueprintResult>& Results, float ElapsedSeconds);
	void FinishInteraction(bool bCancelled, bool bBroadcast, bool bStopMontages);
	bool HandleRagdollNotify(AActor* Victim, UAnimSequenceBase* Animation, int32 MontageInstanceId);
	FVector ComputeVictimLaunch(UAnimInstance* Anim, UAnimMontage* Montage) const;
	void TryReplayInteraction();
	void ApplyReplicatedVictimRelease();

	UFUNCTION()
	void OnRep_InteractionReplay();

	UPROPERTY(ReplicatedUsing=OnRep_InteractionReplay)
	FVBInteractionReplay InteractionReplay;

	// 서로 다른 액터 채널의 참조가 아직 해석되지 않았다면 짧게 재시도한다.
	FTimerHandle InteractionReplayRetryTimer;
	int32 ReplayRetrySession = 0;
	double ReplayRetryDeadline = 0.0;
	int32 LocalInteractionSession = 0;
	int32 FinishedInteractionSession = 0;
	int32 LocalGeneration = 0;
	bool bInteractionVictimLanded = false;
	bool bFinishingInteraction = false;
	bool bLastFinisherCancelled = false;
	TArray<TWeakObjectPtr<AActor>> InteractionParticipants;
	TArray<TWeakObjectPtr<UAnimMontage>> InteractionMontages;
	TArray<TWeakObjectPtr<class UVBAbilitySystemComponent>> InteractionAbilitySystems;
	TArray<int32> InteractionMontageInstanceIds;
	TArray<bool> AddedTickPrerequisite;
	TArray<bool> AddedOwnerCollisionIgnore;
	TArray<bool> AddedVictimCollisionIgnore;

	// GameThread에서만 접근한다. 종료/파괴 때 해제하며, notify마다 월드를 검색하지 않는다.
	static TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UVBFinisherComponent>> ParticipantOwners;

	// 조기 이탈에서 상호작용 몽타주를 끄는 데 걸리는 시간(초).
	// 값을 밖으로 낸 이유: 0 으로 끄면 두 몸이 그 프레임에 자세를 잃고 튄다. 튜닝 대상이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Finisher")
	float InteractionEarlyOutBlendTime = 0.25f;


	// 도착 확인용 로그 하나뿐이다. 여기서 무언가를 시작하지 않는다 - 클라에서 장면이 도는지는
	// 플러그인이 제 바인딩 복제로 알려주고(IsActorInScene), 이 값은 그때 읽을 «연출 값» 이다.
	// 왜 로그가 필요한가: 이 복제는 원격 클라에서만 의미가 있는데 지금 그것을 몰 수단이 없다
	//   (호스트 PIE 뿐). 값이 도착했는지를 나중에 물어볼 자리가 없으면 통째로 무증상이 된다.
	UFUNCTION()
	void OnRep_ActiveConfig();

	// 지금 돌고 있는 처형의 설정. 씬이 끝나면 비운다.
	//
	// 왜 복제하나: 서버의 StartFinisher 만 이것을 세운다. 복제가 없으면 원격 클라에서 영영 nullptr 이고,
	//   이 값을 읽는 것들(카메라·슬로우·쉐이크)이 그 클라에서 통째로 안 걸린다. 클라가 스스로 메울 수도
	//   없다 - SelectFinisher 의 RandRange 가 비결정적이라 같은 답이 안 나온다.
	// 왜 지금 하나: 읽으면 아는 사실이라 재 볼 것이 없다. 카메라를 먼저 올리면 「클라에서만 카메라가
	//   안 나온다」를 카메라 탓으로 쫓게 된다(2026-09-12 Codex 지적, DEC-019).
	UPROPERTY(ReplicatedUsing=OnRep_ActiveConfig)
	TObjectPtr<const UVBFinisherConfig> ActiveConfig = nullptr;

	// 빈 목록 경고를 무기마다 한 번만 남기기 위한 기록. 입력이 연타로 들어와도 로그가 안 죽는다.
	mutable TSet<uint8> WarnedEmptyWeapons;
};
