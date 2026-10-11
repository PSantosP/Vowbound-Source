// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Traversal/VBTraversalTypes.h"
#include "Animation/AnimMontage.h" // FOnMontageEnded
#include "Engine/EngineTypes.h"    // EMovementMode (매달림 복구 모드)
#include "VBTraversalComponent.generated.h"

class AVBLadder;

class AVBCharacter;
class UVBTraversalConfig;
class UVBClimbAnimSet;
class UChooserTable;
class UPrimitiveComponent;
class USplineComponent;

/**
 * UVBTraversalComponent
 *
 * GASP CBP_SandboxCharacter::TryTraversalAction / PerformTraversalAction / UpdateWarpTargets 를
 * C++ 로 1:1 포팅(서버 권위 래퍼 추가). VBCharacter 에 부착.
 *
 * 흐름(GASP 그대로):
 *  TryTraversalAction(소유 클라) → 전방/공간/상단/하향 트레이스 + ActionType 판정(UVBTraversalConfig)
 *    → InteractionTransform 전달 → CHT 평가 → MotionMatch → 몽타주/시작시간 선택
 *    → ServerPerformTraversal(RPC) → MulticastPerformTraversal → 전원 PerformTraversalAction(Flying+Warp+몽타주)
 *
 * deviation(승인됨): GASP 싱글 BP → Vowbound 서버권위(Server_/Multicast_), C++ 포팅. 로직은 1:1.
 *
 * 매달림(Hang, Vowbound 확장 2026-09-18, 설계 .sw/traversal/design.md):
 *  공중 잡기에서 렛지 아래 MantleHeightMax 안에 바닥이 없으면(절벽·높은 벽) 몽타주 대신 «상태» 로 들어간다.
 *  캡슐은 렛지 스플라인 위의 거리 하나(HangDistance)에서 매 틱 파생되고(드리프트 없음), 좌우 입력이 그 거리를 민다.
 *  앞 입력·점프 = 올라서기 몽타주(워프로 렛지 안쪽 stand 지점), 뒤 입력·크라우치 = 놓기(Falling).
 *  이동모드는 MOVE_Flying 에 속도 0 - 위치의 주인은 이 컴포넌트다. 회전은 IsCapsuleRotationOwnedByFeature(트래버설 중)
 *  와 ApplyRotationMode 의 매달림 게이트가 함께 막는다. 애니는 UVBAnimInstance 가 bIsHanging/HangMoveAxis 를 미러한다.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBTraversalComponent();

	/** IA_Jump 진입점. 트래버설 수행 시 true(점프 억제), 불가 시 false(기존 점프 진행).
	 *  bAirMantleOnly=true: 공중(점프 키 홀드) 자동 체크 모드 — Mantle(/Climb)만 허용, Vault/Hurdle 제외.
	 *  (Vowbound 확장 — GASP 는 IA_Jump 에 IsMovingOnGround 게이트가 있어 공중 트래버설이 없음.
	 *   user 승인 2026-06-04: "점프 중 벽 딛고 올라가기", 발동 = 점프 키 홀드 or 공중 재입력) */
	UFUNCTION(BlueprintCallable, Category="Vowbound|Traversal")
	bool TryTraversalAction(bool bAirMantleOnly = false);

	FORCEINLINE bool IsDoingTraversal() const { return bDoingTraversalAction; }

	// === 매달림 ===
	FORCEINLINE bool IsHanging() const { return bIsHanging; }
	// 옆걸음 애니 축 -1(왼쪽)~+1(오른쪽). UVBAnimInstance 가 미러해 BS_VB_LedgeHang 의 X 로 쓴다.
	FORCEINLINE float GetHangMoveAxis() const { return HangMoveAxis; }

	// 매달림 IK 목표(월드). 값의 홈은 여기 하나이고 ABP 는 AnimInstance 를 거쳐 읽기만 한다.
	FORCEINLINE const FVBHangIKTargets& GetHangIKTargets() const { return HangIKTargets; }

	// 소유 클라의 이동 입력을 매달림이 가로챈다(AVBCharacter::HandleMove). X=좌우(옆걸음) Y=앞(올라서기)/뒤(놓기).
	void SetHangMoveInput(const FVector2D& Input);
	// 점프 입력 → 올라서기. 크라우치 입력 → 놓기. 둘 다 소유 클라에서 부르고 서버가 전원에 퍼뜨린다.
	void RequestHangClimbUp();
	void RequestHangDrop();

	// === 자유 등반 ===
	FORCEINLINE bool IsClimbing() const { return bIsClimbing; }

	// 사다리에 붙어 있나. 애니는 UVBAnimInstance 가 미러한다.
	FORCEINLINE bool IsOnLadder() const { return bIsOnLadder; }

	// 이 컴포넌트가 캡슐 «얼굴» 의 주인인가. 매 틱 벽·사다리 쪽으로 직접 쓰는 상태들이다.
	//   AVBCharacter::ApplyRotationMode 가 이것을 보고 CMC 의 회전 플래그 둘을 끈다. 안 끄면
	//   CMC 가 카메라 쪽으로, 이 컴포넌트가 벽 쪽으로 돌려 주인이 둘이 된다.
	// 홈이 여기인 이유: «누가 얼굴을 쓰는가» 를 아는 것은 이 컴포넌트다. 종전에는 캐릭터 쪽에
	//   매달림·등반을 나열했고 2026-09-20 에 사다리가 그 목록에서 빠진 채로 들어가 캡슐이
	//   카메라를 따라 돌았다(손발 IK 가 깊이 대신 좌우를 읽어 63cm 로 튀었다). 목록이 둘이면
	//   다음 상태도 한쪽에만 들어간다.
	FORCEINLINE bool OwnsCapsuleFacing() const { return bIsHanging || bIsClimbing || bIsOnLadder; }

	// 사다리 IK 노드가 PreUpdate(게임 스레드)에서 기하/설정만 복사한다.
	const AVBLadder* GetActiveLadderForIK() const { return bIsOnLadder ? ActiveLadder.Get() : nullptr; }
	const UVBClimbAnimSet* GetClimbAnimSetForIK() const { return ClimbAnimSet; }

	// 사다리에서 내려오기(놓기). 등반의 RequestClimbDrop 과 같은 자리다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|Traversal")
	void RequestLadderDrop();

	// 사다리 위아래 입력. 축이 하나뿐이라 FVector2D 를 안 받는다 - 안 쓰는 축을 받으면
	//   나중에 「좌우도 되나」를 코드에서 되묻게 된다.
	void SetLadderMoveInput(float InputY);
	// 등반 블렌드스페이스의 두 축. X=-1(왼쪽)~+1(오른쪽), Y=-1(아래)~+1(위).
	FORCEINLINE FVector2D GetClimbMoveAxis() const { return ClimbMoveAxis; }
	// 등반 중 네 말단의 IK 목표. 매달림과 같은 구조체를 쓴다 - 담는 내용이 같아서 새로 만들 이유가 없다.
	//   손은 손뼈 공간의 오프셋, 발은 월드 좌표다(ABP 노드 설정이 그렇게 갈려 있다).
	FORCEINLINE const FVBHangIKTargets& GetClimbIKTargets() const { return ClimbIKTargets; }

	// 소유 클라의 이동 입력을 등반이 가로챈다. X=좌우, Y=위아래.
	void SetClimbMoveInput(const FVector2D& Input);
	// 크라우치 입력 → 벽에서 놓기.
	void RequestClimbDrop();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

	// 트레이스/판정 임계값 (GASP 인라인 리터럴의 데이터화).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal")
	TObjectPtr<UVBTraversalConfig> Config;

	// CHT_VB_TraversalAnims (struct 컨텍스트=FVBTraversalChooserParams). T6에서 할당.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal")
	TObjectPtr<UChooserTable> TraversalChooserTable;

	// 매달림 클립 묶음 + 자세 기하. 비어 있으면 매달림이 꺼지고 공중 잡기는 종전 맨틀로만 간다(BeginPlay 가 한 줄 알린다).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal")
	TObjectPtr<UVBClimbAnimSet> ClimbAnimSet;

	// ABP PoseSearchHistoryCollector 노드 태그 (MotionMatch query 생성). GASP MotionMatch 와 동일 "PoseHistory".
	// NAME_None 이면 MotionMatch 가 history 를 못 찾아 SelectedAnim=null → 트래버설 미발동(일반 점프).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Traversal")
	FName PoseHistoryName = TEXT("PoseHistory");

	// UVBTraversalConfig 안전 접근(미할당 시 CDO 디폴트=GASP exact).
	const UVBTraversalConfig* GetConfig() const;

	// Step1-3: 전방→LevelBlock→GetLedgeTransforms→공간/높이/깊이/바닥 트레이스. 채워진 Result 반환(front 유효 시 true).
	// bAirMode=true: 공중 맨틀 체크 — 전방 트레이스를 속도비례(최대 350) 대신 AirMantleTraceDistance 고정 단거리로.
	bool PerformTraversalCheck(FVBTraversalCheckResult& OutResult, bool bAirMode = false) const;

	// Step4.1: 조건/파라미터로 ActionType 결정 (UVBTraversalConfig).
	EVBTraversalActionType DecideActionType(const FVBTraversalCheckResult& R) const;

	// === 서버 권위 래퍼 ===
	// WithValidation: 악성 패킷(null 몽타주/범위밖 ActionType/NaN 위치) 조기 차단. _Implementation 은
	// 추가로 클라가 보낸 렛지 좌표를 서버 캐릭터 위치 기준 거리 상한으로 검증(임의 워프 텔레포트 차단).
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerPerformTraversal(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPerformTraversal(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	// Step5.5 / PerformTraversalAction: Flying 전환 + 장애물 콜리전 무시 + 워프 + 몽타주 재생.
	void PerformTraversalAction(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	// 몽타주 재생 + 복구 델리게이트 두 개 바인딩. 맨틀류와 매달림 올라서기가 같은 복구 계약을 쓴다.
	//   애님 인스턴스가 없으면 즉시 복구한다(PerformTraversalAction 의 그 분기가 여기로 옮겨 왔다).
	void PlayTraversalMontage(UAnimMontage* Montage, float StartTime, float PlayRate);

	// === 매달림 내부 ===
	// 공중 잡기가 맨틀 대신 매달림으로 갈 조건: 클립 묶음이 있고, 렛지 스플라인이 있고,
	//   (블록이 «항상 매달림» 이거나 렛지 아래 MantleHeightMax 안에 바닥이 없다).
	bool ShouldHangInsteadOfMantle(const FVBTraversalCheckResult& Result, bool bFrontFloorFound) const;

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartHang(const FVBTraversalCheckResult& Result);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartHang(const FVBTraversalCheckResult& Result);

	// 소유 클라 → 서버 옆걸음 입력. 위치는 서버가 굴리고 CMC 복제가 나른다. 매 변경마다라 Unreliable.
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetHangMoveInputX(float InputX);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerHangClimbUp();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHangClimbUp();
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerHangDrop();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHangDrop();

	// 전원에서 실행되는 상태 전이 셋.
	void StartHang(const FVBTraversalCheckResult& Result);
	void StartHangClimbUp();
	void EndHangByDrop();

	// 매 틱: 입력으로 HangDistance 를 밀고, 스플라인에서 캡슐 트랜스폼을 파생해 쓴다.
	void UpdateHang(float DeltaTime);

	// 옆걸음 몽타주를 이 틱만큼 돌린다. 걸기·루프·끊기와 몸통 흔들림 산출이 여기 있고,
	//   반환값은 «몽타주 없이 갈 때» 의 진행량(cm)이다. 몽타주가 돌면 이동은 루트모션이 맡으므로 0 을 돌려준다.
	//   부호는 붙이지 않는다 - 방향은 호출처가 스플라인 접선으로 정한다.
	float TickShimmyMontage(float DeltaTime, float InputX);

	// 몽타주를 걸고 자기 섹션으로 돌린다. 이미 그것이 돌고 있으면 아무것도 안 한다.
	//   Wanted 가 null 이면 Active 를 끊는다. 돌아온 값이 «지금 걸려 있는 것» 이다.
	// 왜 뺐나(2026-09-19): 옆걸음·등반·사다리 셋이 같은 뼈대를 쓴다. 등반 때 「두 번째라 안 뺀다,
	//   사다리가 세 번째가 되면 그때 옮긴다」고 적어 뒀고(규칙 12) 사다리가 왔다.
	//   섹션 루프를 쓰는 이유는 셋 다 같다 - 몽타주는 끝보다 BlendOut 만큼 일찍 «재생 아님» 이 되어
	//   「끝나면 다시 걸기」로는 매 주기 자세가 튀고 그 구간의 루트모션도 버려진다(옆걸음에서 실측).
	// bOutStarted 는 «이번에 새로 걸었나» 다. 그때 초기화할 부기가 호출처마다 달라서 밖으로 낸다.
	// StartTime 은 «이번에 새로 걸 때 어느 위상에서 시작할지» 다. 사다리가 이것을 쓴다 -
	//   발이 가로대 위에 떨어지도록 위상을 고른다(FindLadderMontageStart). 나머지는 0 을 준다.
	UAnimMontage* DriveLoopingMontage(UAnimInstance* Anim, UAnimMontage* Wanted,
		UAnimMontage* Active, float StopBlend, bool& bOutStarted, float StartTime = 0.0f);

	// 두 대기 벌 중 «지금 자세와 이어지는» 쪽을 고른다. 지금 어느 발이 위에 있는지로 가른다.
	//   위상으로 세지 않는 이유: 위상표는 클립마다 다시 재야 하는데 발 높이는 어느 클립이든 같은 질문이다.
	UAnimMontage* PickLadderIdle() const;

	// 사다리 몽타주를 «어느 위상에서 시작해야 발이 가로대에 떨어지는가». 못 정하면 0 을 돌려준다.
	//   캐릭터를 순간이동시켜 맞추는 대신 위상을 고르는 이유: 맞추려면 최대 20cm 를 튀어야 한다.
	float FindLadderMontageStart(UAnimMontage* Montage, float HeightNow) const;

	// === 자유 등반 내부 ===
	// 바라보는 쪽에 등반 가능한 면이 있으면 붙는다. 렛지 판정이 실패한 자리에서 불린다.
	bool TryStartClimb();

	// 앞쪽에서 마주 보는 사다리를 찾아 붙는다. 등반보다 먼저 본다 - 사다리가 있으면 사다리가 맞다.
	bool TryStartLadder();
	void StartLadder(AVBLadder* Ladder, float Height);
	void EndLadder();
	void UpdateLadder(float DeltaTime);
	// 사다리 축 위의 높이에서 캡슐이 있어야 할 자리.
	FTransform ComputeLadderCapsuleTransform(float Height) const;

	// 한 점 둘레에서 면을 찾는다. 면 바깥에서 시작해 안쪽으로 쏘므로 면 위의 점에서 시작해 놓치는 일이 없다.
	//   충분히 서 있는 면만 참이다(바닥에 붙어 기어가는 것을 막는다).
	bool TraceClimbSurface(const FVector& Around, const FVector& Normal, FHitResult& OutHit) const;

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartClimb(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartClimb(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface);
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetClimbMoveInput(FVector2D Input);
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerClimbDrop();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastClimbDrop();

	// 사다리도 등반과 같은 모양으로 간다 - 서버가 상태의 주인이고 소유 클라는 축 값을 보낸다.
	//   로컬로만 두면 싱글에서는 돌고 멀티에서만 안 되는 자리가 생긴다(2026-09-19 자가검토에서 잡음).
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartLadder(AVBLadder* Ladder, float Height);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartLadder(AVBLadder* Ladder, float Height);
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetLadderMoveInput(float InputY);
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerLadderDrop();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastLadderDrop();

	void StartClimb(const FVector& SurfacePoint, const FVector& SurfaceNormal, UPrimitiveComponent* Surface);
	void EndClimbByDrop();
	void UpdateClimb(float DeltaTime);

	// 네 말단을 벽면에 붙인다. 벽에서 멀리 있는(뻗는 중인) 말단은 가중치를 낮춰 클립 그대로 둔다.
	// 말단 IK 목표를 정한다. 목표는 캐릭터가 아니라 세상에서 온다 - 말단 자리에서 벽으로 쏴 면을 찾는다.
	//   매달림의 발 IK(TraceFoot)와 같은 방식이고, 되먹임이 없어 «내가 직전에 넣은 양» 을 알 필요가 없다.
	void UpdateClimbIKTargets(float DeltaTime);

	// 입력 방향에 맞는 몽타주. 없으면 nullptr 이고 그 방향은 일정 속도로 떨어진다.
	UAnimMontage* PickClimbMontage(const FVector2D& Input, FVector2D& OutDir) const;
	// 그 몽타주를 이 틱만큼 돌린다(걸기·루프·끊기). 이동은 루트모션이 맡으므로 반환값이 없다.
	// 지금 걸어 둔 몽타주가 «맡은» 8방향 단위벡터를 돌려준다(X 가로, Y 위). 몽타주가 없으면 0 이다.
	//   멤버로 안 들고 돌려주는 이유: 그 방향은 ActiveClimbMontage 와 한 쌍이라 따로 두면 네 곳에서
	//   같이 지워야 하고, 한 곳을 빠뜨리는 날 엉뚱한 축에 투영한다. 파생값은 파생시킨다.
	FVector2D TickClimbMontage(const FVector2D& Input);

	// 방향이 바뀌었는데 아직 갈아끼우지 않은 후보와 그 방향이 들어온 시각.
	//   ClimbDirectionHoldTime 만큼 유지되면 그때 건다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> ClimbPendingMontage;
	double ClimbPendingSince = 0.0;
	// 지금 걸려 있는 몽타주가 맡은 방향. ActiveClimbMontage 와 한 쌍이고 이 함수 밖에서는 안 읽는다 -
	//   갈아끼우기를 미루는 동안에도 «지금 뭐가 돌고 있나» 를 돌려줘야 해서 들고 있다.
	FVector2D ActiveClimbDir = FVector2D::ZeroVector;

	// 면 위의 점·법선(바깥) → 캡슐 월드 트랜스폼. root 가 면에서 바깥으로 ClimbDepthCurrent 에 오도록.
	FTransform ComputeClimbCapsuleTransform(const FVector& SurfacePoint, const FVector& SurfaceNormal) const;

	// 매달림과 등반이 똑같이 하는 진입 절차. 무기 임시수납 · CMC 를 Flying 으로 · 장애물 콜리전 무시 ·
	//   회전 모드 적용 · 틱 켜기. 복제하지 않고 한 곳에 둔다 - 사다리가 세 번째 호출자가 된다.
	void BeginSurfaceAttachment(UPrimitiveComponent* Obstacle);

	// 렛지 점·법선(바깥) → 캡슐 월드 트랜스폼. root 가 렛지 아래 HangRootBelowLedge, 바깥 HangFootDepth 에 오도록.
	//   깊이 기준이 «발» 인 이유는 UVBClimbAnimSet::HangFootDepth 주석에 있다.
	FTransform ComputeHangCapsuleTransform(const FVector& LedgePoint, const FVector& LedgeNormal) const;

	// 네 말단의 IK 목표를 레벨 기하에서 뽑는다. 손은 렛지 스플라인 위, 발은 벽 표면(트레이스).
	//   벽을 못 찾은 발은 가중치 0 으로 두어 애니 그대로 둔다 - 못 재면 안 건드린다(규칙 27).
	void UpdateHangIKTargets(const FVector& LedgePoint, const FVector& LedgeNormal, float DeltaTime);

	// UpdateWarpTargets: FrontLedge(항상) / BackLedge(Hurdle·Vault) / BackFloor(Hurdle) MotionWarping 타겟.
	void UpdateWarpTargets(const FVBTraversalCheckResult& Result, UAnimMontage* Montage);

	// 몽타주 블렌드아웃 "시작" 콜백 — bInterrupted(=BP_NotifyState_MontageBlendOut 의 조기 Montage_Stop 등)면
	// 즉시 복구. GASP PlayMontage OnInterrupted 대응: GASP 는 조기 탈출 순간 Walking 복귀해 블렌드 중에도
	// 지상 가속이 걸린다. VB 가 종료(블렌드아웃 완료) 시점에만 복구하던 동안의 ~0.3s Flying 공백이
	// "올라온 뒤 멈칫"의 원인 (user 보고 2026-06-04).
	void OnTraversalMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	// 몽타주 완전 종료 콜백 — 자연 종료(!bInterrupted)만 여기서 복구. GASP PlayMontage OnCompleted 대응.
	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 공통 복구: MovementMode 복귀(Vault→Falling, 그 외→Walking — GASP Select 핀 리터럴) +
	// 콜리전 복구 + 워프 타겟 제거 + DoingTraversal 해제. 두 콜백 중 먼저 온 쪽만 실행(가드).
	void RecoverFromTraversal();

	// capsule 트레이스 헬퍼(TraceChannel 사용).
	bool CapsuleSweep(const FVector& Start, const FVector& End, FHitResult& OutHit) const;

	// 렛지에서 LedgeNormal 방향 (CapsuleRadius+2) 수평 오프셋 후 하향 캡슐 스윕으로 바닥 탐색.
	// 왜 오프셋: 캡슐(직경≈68)이 얇은 벽(depth 30 등)을 감싸 시작부터 관통 → 벽 측면을 floor 로
	//   오인식(BackLedgeHeight 과소측정 → Hurdle/Vault 오탈락) 방지 — Step3.6 버그 교훈.
	// Z: Start 는 렛지 위 +HalfH+2, End 는 렛지 아래 -DownDepth — 깊이는 호출처가 결정
	//   (back-floor: ObstacleHeight-HalfH+50 / air front-floor: MantleHeightMax+50, GASP 수치 그대로).
	bool TraceFloorBelowLedge(const FVector& LedgePoint, const FVector& LedgeNormal, float DownDepth, FHitResult& OutFloorHit) const;

private:
	UPROPERTY()
	TWeakObjectPtr<AVBCharacter> OwnerCharacter;

	bool bDoingTraversalAction = false;

	// 트래버설 중 콜리전 무시한 장애물(종료 시 복구).
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> IgnoredObstacle;

	// 복구 시 MovementMode 분기용 — 이번 트래버설의 ActionType (Vault→Falling, 그 외→Walking).
	EVBTraversalActionType LastActionType = EVBTraversalActionType::None;

	// 복구 때 돌아갈 이동모드. 진입 시 정하고(Vault→Falling, 그 외→Walking) 매달림 놓기가 Falling 으로 덮는다.
	//   LastActionType 만으로는 «매달림에서 올라섰나 놓았나» 를 못 가른다.
	EMovementMode RecoverMovementMode = MOVE_Walking;

	// === 매달림 상태 ===
	bool bIsHanging = false;
	TWeakObjectPtr<USplineComponent> HangLedge;
	// 스플라인 거리 - 매달림의 유일한 위치 상태. 캡슐 위치·회전은 매 틱 여기서 파생된다.
	float HangDistance = 0.0f;
	float HangMinDistance = 0.0f;
	float HangMaxDistance = 0.0f;
	// 소유 클라와 서버가 든다. X 좌우, Y 앞뒤.
	FVector2D HangMoveInput = FVector2D::ZeroVector;
	// 지금 도는 옆걸음 몽타주와 그 재생 위치(초). 위치는 흔들림 산출의 입력이다 -
	//   위상 0 부터 여기까지의 누적 루트모션이 몸통의 앞뒤·상하 흔들림이 된다.
	//   렛지를 따라가는 이동은 여기서 안 센다. 그쪽은 루트모션이 캡슐을 실제로 옮긴 양으로 잰다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveShimmyMontage;
	float ShimmyMontagePosition = 0.0f;
	// 지금 위상에서 클립 root 가 시작 자세보다 얼마나 벽 쪽으로(+) / 위로(+) 가 있나. 몽타주 없으면 0.
	//   이 둘을 배치에 더하지 않으면 클립이 의도한 몸통 흔들림이 지워지고, 지워진 몫이 손으로 옮겨간다.
	float ShimmySwayTowardWall = 0.0f;
	float ShimmySwayRise = 0.0f;

	// === 사다리 상태 ===
	bool bIsOnLadder = false;
	UPROPERTY()
	TWeakObjectPtr<AVBLadder> ActiveLadder;
	// 축 위의 어디에 있나(cm, 0=바닥). 사다리의 «유일한 위치 상태» 다 - 캡슐 자리는 매 틱 여기서 파생된다.
	//   매달림의 HangDistance 와 같은 꼴이다.
	float LadderHeightOnAxis = 0.0f;
	float LadderMoveInput = 0.0f;
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveLadderMontage;
	FVector LadderEnterLocation = FVector::ZeroVector;
	FQuat LadderEnterRotation = FQuat::Identity;
	double LadderEnterTime = 0.0;
	double LadderNextLogTime = 0.0;
	// 직전 틱에 내가 캡슐을 «놓은» 자리. 다음 틱에 보이는 차이가 곧 루트모션이 옮긴 양이다.
	//   등반의 ClimbLastPlacedLocation 과 같은 장치다 - 배치가 위치의 주인이라 그 차이 말고는 잴 길이 없다.
	FVector LadderLastPlacedLocation = FVector::ZeroVector;
	bool bLadderPlacedOnce = false;
	double LadderNextClampLogTime = 0.0;
	double LadderNextJumpLogTime = 0.0;
	// 지금 도는 사다리 몽타주의 재생 위상(초). 이동은 이 값의 «전 프레임과의 구간» 에서 나온다 -
	//   액터가 얼마나 움직였는지로 재면 캡슐이 벽에서 밀려난 양까지 이동으로 세어진다.
	float LadderMontagePosition = 0.0f;
	bool bLadderMontageTracked = false;
	// 멈출 때 «가장 가까운 가로대» 로 당겨 앉는 목표 높이와 그 진행 여부.
	//   오르는 동안의 정렬은 디딤 순간에만 성립하므로, 서는 자리는 따로 맞춰야 한다.
	float LadderSettleTarget = 0.0f;
	bool bLadderSettling = false;
	// 입력이 끊긴 뒤 조용히 흐른 시간(초). 이것이 문턱을 넘어야 당겨 앉기가 시작된다.
	//   왜 방향이 아니라 시간인가: 방향으로 막으면 당기는 쪽이 한 칸 통째(최대 40cm)가 되어
	//   이번엔 아래로 미끄러진다. 탭과 싸우지 않게 하려면 «그 사이에 아예 안 끼어들면» 된다.
	float LadderIdleQuietTime = 0.0f;
	// 직전에 돌던 «방향 몽타주» 와 그 위상. 같은 것을 다시 걸 때 처음이 아니라 여기서 이어 붙인다.
	//   왜: 탭마다 처음부터 다시 걸면 애니가 같은 도입부만 반복한다(user 「계속 처음부터 반복해서 재생」).
	UPROPERTY()
	TObjectPtr<UAnimMontage> LadderResumeMontage;
	float LadderResumePhase = 0.0f;

	// === 자유 등반 상태 ===
	bool bIsClimbing = false;
	// 붙어 있는 면 위의 점(root 높이)과 그 바깥 법선. 이 둘이 캡슐 자리를 정한다.
	FVector ClimbSurfacePoint = FVector::ZeroVector;
	FVector ClimbSurfaceNormal = FVector::ForwardVector;
	FVector2D ClimbMoveInput = FVector2D::ZeroVector;
	FVector2D ClimbMoveAxis = FVector2D::ZeroVector;
	// 대기와 이동 사이를 잇는 깊이. 이 둘은 같은 값이다 - 한때 여기에 클립의 앞뒤 흔들림을 더했으나
	//   주기 도중에 손을 떼면 남아서 걷어냈다(2026-09-19). 말단의 깊이는 IK 가 맡는다.
	//   그래도 둘로 둔 것은 보간의 대상(Base)과 배치에 쓰는 값(Current)이 나중에 갈릴 수 있어서다.
	float ClimbDepthBase = 0.0f;
	float ClimbDepthCurrent = 0.0f;
	FVBHangIKTargets ClimbIKTargets;
	// 지금 도는 등반 몽타주. 이것이 있으면 이동은 루트모션이 맡는다.
	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveClimbMontage;
	// 직전 틱에 내가 놓은 자리. 루트모션이 이 틱에 몸을 얼마나 옮겼는지를 이것과의 차이로 잰다.
	FVector ClimbLastPlacedLocation = FVector::ZeroVector;
	bool bClimbPlacedOnce = false;
	// 지금 각 말단에 넣고 있는 보정량(벽 법선 방향 cm, 바깥이 양수). 상태로 들고 오차만큼 조금씩 고친다.
	//   이것이 이 IK 의 핵심이다 - 「노드가 내 숫자를 그대로 적용한다」를 전제하지 않는다.
	//   수렴하면 오차가 0 이 되어 값이 유지되고, 클립이 말단을 움직이면 그만큼 다시 따라간다.
	float ClimbCorrHandL = 0.0f;
	float ClimbCorrHandR = 0.0f;
	float ClimbCorrFootL = 0.0f;
	float ClimbCorrFootR = 0.0f;
	// 보간된 IK 세기. 판정은 프레임마다 튈 수 있어도 실제로 노드에 가는 값은 부드러워야 한다.
	float ClimbWeightHandL = 0.0f;
	float ClimbWeightHandR = 0.0f;
	float ClimbWeightFootL = 0.0f;
	float ClimbWeightFootR = 0.0f;
	FVector ClimbEnterLocation = FVector::ZeroVector;
	// 붙잡는 순간의 회전. 위치만 섞고 회전은 안 섞으면 같은 순간에 하나는 0.15초 끌리고
	//   하나는 한 프레임에 꺾인다 - 2026-09-19 진입 튐의 절반이 그것이었다.
	FQuat ClimbEnterRotation = FQuat::Identity;
	double ClimbEnterTime = 0.0;
	// 지난 1초 동안의 최대 속도(cm/s). 몸은 «내가 놓은 자리», 말단은 «몸 기준 손발 자리» 를 잰다.
	//   문턱을 넘은 사건만 보면 문턱을 잘못 고른 날 아무것도 안 보인다 - 최대값은 늘 나오므로 분포가 보인다.
	//   말단을 따로 재는 이유: 캡슐이 매끄러워도 자세가 튈 수 있고, 몸만 보는 계측은 그것을 못 본다.
	float ClimbMaxBodyRate = 0.0f;
	float ClimbMaxLimbRate = 0.0f;
	FVector ClimbPrevLimbRel[4] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	bool bClimbLimbRatePrimed = false;
	// 진단 줄을 1초에 한 번만 낸다. 매 틱 쏟으면 진짜 줄을 못 찾는다.
	double ClimbNextLogTime = 0.0;
	// 한 틱 이동을 잘랐다는 알림도 1초에 한 번만.
	double ClimbNextClampLogTime = 0.0;
	// «보이는 자리가 한 틱에 뛰었다» 알림. 튀는 프레임을 보려는 것이라 간격이 훨씬 촘촘하다(0.05초).
	double ClimbNextJumpLogTime = 0.0;
	// 진입 실패 사유도 같은 이유로 묶는다(0.5초). 점프 연타에 로그가 쏟아지지 않게.
	double ClimbNextProbeLogTime = 0.0;
	// 폴백 경고를 한 번만 낸다 - 매 틱 쏟으면 진짜 경고를 무시하게 된다.
	bool bShimmyFallbackWarned = false;

	// 각 손이 «잡고 있는» 렛지 위 거리. 뻗음이 끝나는 순간 그 자리를 찍고, 심어 있는 동안 그대로 붙든다.
	//   매 틱 HangDistance 에서 새로 계산하면 손이 몸을 따라 흘러 «미끄러짐» 이 된다 - 잡은 자리는 안 움직인다.
	// 직전 틱에 내가 놓은 캡슐 위치. 다음 틱 시작에 실제 위치와 비교하면 그 사이 루트모션이 옮긴 양이 나온다.
	//   이 차이가 옆걸음 진행량이다 - 클립에서 값을 꺼내 흉내 내지 않고 엔진이 실제로 적용한 것을 쓴다.
	FVector HangLastPlacedLocation = FVector::ZeroVector;
	bool bHangPlacedOnce = false;
	// 진단용 1초 간격 계측. 옆걸음이 안 움직일 때 «루트모션이 안 들어온다» 와 «입력이 안 온다» 를 가른다.
	//   이 갈래가 안정되면 지운다 - 지금은 화면만 봐서 둘을 구별할 수 없다.
	double HangNextTraceTime = 0.0;
	float HangDistanceAtTrace = 0.0f;

	// 대기 자세와 옆걸음 자세 사이를 부드럽게 옮겨가는 «기준» 깊이·높이(cm).
	//   상태가 바뀌는 순간의 단차만 여기서 녹인다 - 한 번에 바꾸면 몸이 튄다.
	float HangDepthBase = 0.0f;
	float HangBelowBase = 0.0f;
	// 실제로 쓰는 깊이·높이. 기준에 클립 root 의 흔들림을 더한 값이다.
	//   흔들림은 보간에 통과시키지 않는다 - 그러면 클립이 낸 신호가 깎여 손이 다시 어긋난다.
	//   즉 여기서 보간할 것은 «상태 전환» 뿐이고, 흔들림은 그대로 지나가야 한다.
	float HangDepthCurrent = 0.0f;
	float HangBelowCurrent = 0.0f;
	// 직전 프레임에 그 손이 뻗는 중이었나. 뻗는 손에는 밀어내기를 안 건다 - 허공이라 팔 자세만 바뀐다.
	bool bHandReachingL = false;
	bool bHandReachingR = false;
	float HangMoveAxis = 0.0f;
	// 앞/뒤 입력으로 이미 나가기를 요청했으면 같은 홀드로 두 번 보내지 않는다.
	bool bHangExitRequested = false;
	// 잡는 순간 쥐고 있던 앞/뒤 입력은 무시한다. 달리며 W 를 쥔 채 잡히면 그 프레임에 바로 올라서 매달림이 안 보인다
	//   (2026-09-18 셋째 PIE: 잡힘 → 11ms 뒤 올라서기). 축이 한 번 중립으로 돌아와야 앞/뒤 나가기가 무장된다. 점프·크라우치는
	//   눌림 이벤트라 원래 새 입력만 들어온다.
	bool bHangExitInputArmed = false;
	// 잡는 순간의 캡슐 위치와 시각 - HangGrabBlendTime 동안 매달림 자리로 미끄러진다.
	FVector HangGrabStartLocation = FVector::ZeroVector;
	double HangGrabStartTime = 0.0;
	// 놓은 뒤 이 시각까지는 공중 잡기가 렛지를 안 문다(Config HangRegrabDelay).
	double HangRegrabBlockUntil = 0.0;

	// 매달림 IK 목표 + 세기. UpdateHangIKTargets 가 매 틱 채운다.
	FVBHangIKTargets HangIKTargets;
	// IK 가 차오른 정도 0~1. 잡는 순간 손이 순간이동하지 않게 HangIKBlendInTime 동안 올린다.
	float HangIKAlpha = 0.0f;

	FOnMontageEnded MontageEndedDelegate;
	FOnMontageBlendingOutStarted MontageBlendingOutDelegate;
};
