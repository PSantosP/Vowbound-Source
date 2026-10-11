// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBClimbAnimSet.generated.h"

class UAnimMontage;
class UBlendSpace;

/**
 * UVBClimbAnimSet
 *
 * 렛지 매달리기(그리고 뒤에 올 자유 등반)의 클립 묶음과, 그 클립을 재서 얻은 수치.
 * 수치가 UVBTraversalConfig 가 아니라 여기 사는 이유: 전부 «이 클립» 의 성질이다.
 *   렛지가 root 에서 얼마나 위·앞에 있는지, 옆걸음이 초당 몇 cm 인지는 클립을 바꾸면 같이 바뀐다.
 *   판정 임계(어디서 발동하나)는 Config, 자세 기하(어디에 몸을 두나)는 여기 - 홈이 갈린다.
 * 실측 근거: 2026-09-18, VB_LedgeClimb_* (FreeAnimationLibrary → UEFN 골격 리타겟본). 설계 .sw/traversal/design.md §10.
 * 매달림 루프 자체(대기·옆걸음)는 ABP 의 BS_VB_LedgeHang 이 든다 - C++ 는 축 값만 준다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBClimbAnimSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 매달린 채 위로 올라서기. MotionWarping 노티 «FrontLedge» 가 전 구간을 덮어 렛지 안쪽 stand 지점에 정확히 선다.
	// 비어 있으면 매달림 자체가 꺼진다(올라설 길이 없는 매달림은 함정이다) - 공중 잡기는 종전 맨틀로 떨어진다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	TObjectPtr<UAnimMontage> HangClimbUpMontage;

	// 매달림 자세에서 렛지(손이 잡은 모서리)가 root 보다 위로 떨어진 거리(cm). 실측 132 (VB_LedgeClimb_Idle t=0).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangRootBelowLedge = 130.0f;

	// 옆걸음 몽타주의 위상 0 에서의 «렛지-root 높이»(cm). 매 틱 여기에 클립 root 의 상하 흔들림을 더해 쓴다.
	// 2026-09-18 개정 - 종전에는 «출렁임의 최악값» 하나를 고정으로 썼다. 그것이 손 뜸·박힘의 원인이었다.
	//   실측: 클립 안에서 손의 절대 높이는 폭 6.1cm(왼쪽)로 거의 붙어 있고, root 가 폭 10.5cm 로 흔들린다.
	//   즉 «손 기준 root 높이» 가 14.8cm 출렁이는 것은 root 가 움직이기 때문이다. 그 흔들림을 지우고
	//   높이를 고정하면, 지워진 흔들림이 그대로 손으로 옮겨간다 - user 가 본 것이 그것이다.
	// 값의 유래: 위상 0 실측 손 높이 132.85 + user 가 맞춘 «렛지보다 9.1cm 아래» = 142.0.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangRootBelowLedgeShimmy = 142.0f;

	// 매달림 자세에서 «손» 이 root 보다 벽 쪽으로 나간 거리(cm). 실측 33.
	// 이 값은 배치가 아니라손 IK 가 메울 간격을 정한다 - 아래 발 깊이와의 차이가 곧 IK 보정량이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangHandDepth = 33.0f;

	// 매달림 자세에서 «가장 깊은 부위(발가락)» 가 root 보다 벽 쪽으로 나간 거리(cm). 실측 53.4.
	//몸을 벽에서 떼는 기준은 이 값이다- 손으로 잡으면(33) 다리가 20cm 벽에 박힌다(2026-09-18 user PIE).
	// 이 캡처는 손과 발이 같은 평면에 없다. 평평한 벽에서는 둘 중 하나만 맞출 수 있고,
	//  깊은 쪽(발)을 맞추고 얕은 쪽(손)을 IK 로 당긴다- 당기는 것은 팔 길이 안이라 항상 풀리지만,
	//   반대로 미는 것은 팔이 모자라면 못 푼다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangFootDepth = 53.4f;

	// 옆걸음 몽타주의 위상 0 에서의 «손 깊이»(cm). 매 틱 여기에 클립 root 의 앞뒤 흔들림을 더해 쓴다.
	// 2026-09-18 개정 - 위 높이 값과 같은 이유다. 실측: 손의 절대 깊이는 폭 4.5cm(왼쪽)/9.4cm(오른쪽)이고
	//   그 9.4 도 대부분 손을 떼어 뻗는 구간의 것이다. 반면 root 는 폭 7.1/13.8cm 로 흔들린다.
	// 값의 유래: 위상 0 실측 손 깊이 35.3 + user 가 맞춘 «렛지보다 4.8cm 바깥» = 40.1.
	//   대조 확인: 이 식이 내는 최대 깊이가 47.1 로, 손으로 맞췄던 옛 고정값 47 과 일치한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangHandDepthShimmy = 40.1f;

	// 대기↔옆걸음 깊이를 옮겨가는 속도(cm/s). 한 번에 바꾸면 몸이 벽에서 툭 튄다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangDepthInterpSpeed = 60.0f;

	// 두 손이 렛지를 따라 벌어진 간격의 절반(cm). 실측 손 사이 약 27 → 13.5.
	// 손 IK 목표를 렛지 스플라인 위 이 간격만큼 좌우로 잡아손이 렛지 윤곽을 따라가게한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangHandLateralOffset = 13.5f;

	// 손 IK 목표를 렛지 모서리에서 위로 띄우는 양(cm). 손목 뼈가 모서리에 정확히 놓이면 손가락이 블록을 파고든다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangHandGripRise = 3.0f;

	// 심은 손의 IK 세기(0~1). 뻗는 중인 손에는 안 걸린다 - 아래 뻗는 구간이 그것을 가른다.
	// 기본 0 인 이유(2026-09-18 실측): 이 캡처는 «심은» 손도 렛지 위에서 흐른다 - 왼손 17cm, 오른손 1.9/9.3cm.
	//   한 점에 묶으면 IK 가 매 프레임 그만큼 클립과 싸워 손이 떨린다(user PIE 「손이 막 덜덜 떨린다」).
	//   제대로 «잡고 버티는» 클립을 저작하면 그때 올린다. 그 전까지 손의 접지는 아래 깊이 값이 맡는다.
	// 종전에 두 손을 모두 한 점에 묶었더니 «뻗어서 잡는» 리듬이 통째로 덮여 미끄러져 보였다(2026-09-18 user PIE).
	// 잡은 손만 잡은 자리에 붙이면 리듬은 클립이, 접지는 IK 가 맡는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangHandIKWeight = 1.0f;

	// 손을 «벽 바깥으로» 밀어내는 양(cm). 클립 자세를 그대로 두고 손만 이만큼 빼낸다.
	// 왜 한 점에 안 묶나: 그렇게 했더니 클립의 손이 17cm 흐르는 것과 IK 가 매 프레임 싸워 손이 떨렸다.
	//   상수 오프셋은 싸울 상대가 없다 - 애니가 그리는 궤적을 통째로 유지한 채 평행이동만 한다.
	// 기본 0 인 이유(2026-09-18 실측): 손이 벽을 파고든 원인은 IK 가 아니라 위 두 값이 지우던 root 흔들림이었다.
	//   그 흔들림을 살린 뒤 계산하면 손은 렛지 바깥 0.8~9.8cm 에 머물러 어느 위상에서도 벽을 안 넘는다.
	//   즉 지금은 밀어낼 것이 없다. 상수로 밀면 이미 바깥에 있는 손을 더 띄우기만 한다.
	//   배선(ABP TwoBoneIK + BCS_BoneSpace)은 살아 있으므로, 잔여 파고듦이 보이면 이 값만 올려 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangHandPushOut = 0.0f;

	// 그 «벽 바깥» 방향을 손뼈 자기 공간으로 옮긴 것. 실측(2026-09-18): 위상 내내 (-0.35, 0.93, -0.13) 에서
	//   변동폭이 0.06~0.18 뿐이라 상수로 써도 각도 오차가 10도 안쪽이다(6cm 에서 1.7cm 이하).
	//   손뼈 방향이 크게 바뀌는 다른 클립을 쓰면 다시 재야 한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	FVector HangHandPushDirLocal = FVector(-0.35f, 0.93f, -0.13f);

	// 옆걸음 한 주기에서 «손이 뻗는» 구간(위상 0~1). 실측(2026-09-18, 월드 속도 임계 100cm/s):
	//   앞선 손 0.025~0.150, 뒤따르는 손 0.575~0.725. 두 클립이 정확히 대칭이라 한 벌로 쓴다.
	//   왼쪽 몽타주는 왼손이 앞서고 오른쪽 몽타주는 오른손이 앞선다.
	// 이 구간 동안 그 손의 IK 를 놓아 준다 - 안 놓으면 뻗는 동작이 사라진다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HandReachLeadStart = 0.025f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HandReachLeadEnd = 0.150f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HandReachTrailStart = 0.575f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HandReachTrailEnd = 0.725f;

	// 뻗음↔심음 전환을 부드럽게 하는 시간(위상). 0 이면 IK 가 한 프레임에 켜졌다 꺼져 손이 튄다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HandReachBlendPhase = 0.05f;

	// 발목을 벽면에서 띄우는 양(cm). 발목을 표면에 딱 붙이면 앞으로 뻗은 발가락이 벽을 파고든다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangFootWallClearance = 8.0f;

	// 발 IK 세기(0~1). 평평한 벽에서는 배치만으로 발끝이 벽에 닿으므로 거의 할 일이 없다.
	//   울퉁불퉁한 벽·기울어진 면에서 발을 실제 표면에 붙이는 몫이라 0 이 아니게 둔다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangFootIKWeight = 1.0f;

	// 발 IK 가 벽면을 찾을 때 쓰는 앞뒤 탐색 거리(cm). 이 안에서 벽을 못 찾으면 그 발은 애니 그대로 둔다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangFootProbeDistance = 60.0f;

	// IK 가 0에서 1로 차오르는 시간(s). 잡는 순간 손이 순간이동하지 않게 한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangIKBlendInTime = 0.2f;

	// 옆걸음 몽타주. 이것이 있으면 클립이 캡슐을 몬다 - 재생 위치에서 그 구간의 루트모션을 꺼내
	//   그만큼만 렛지를 따라 간다. 그래서 심은 손이 안 미끄러진다.
	// 왜 일정 속도가 안 되나(2026-09-18 실측): 이 클립의 root 속도는 한 주기 안에서 0.5 ~ 125.7 cm/s 로 출렁인다
	//   (뻗을 때 빨라지고 심었을 때 멈춘다). 평균 56.8 로 캡슐을 일정하게 밀면 심은 손이 계속 미끄러진다.
	// 비어 있으면 아래 HangShimmySpeed 로 가는 옛 경로(블렌드스페이스 + 일정 속도)로 떨어진다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	TObjectPtr<UAnimMontage> ShimmyMontageL;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	TObjectPtr<UAnimMontage> ShimmyMontageR;

	// 몽타주 없이 갈 때의 일정 속도(cm/s). 위 몽타주가 있으면 안 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangShimmySpeed = 54.0f;

	// 올라선 뒤 root 가 렛지 모서리에서 안쪽으로 들어가는 거리(cm). 매달림 root(-33) + 클립 전진 68 = 35.
	//   워프 타겟이 이 지점이라 클립 자연 종점과 거의 같다 - 워프가 메우는 것은 벽 두께 오차뿐이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangClimbUpStandInset = 35.0f;

	// 손이 닿는 여유(cm). 렛지가 발 위로 HangRootBelowLedge + 이 값보다 높으면 아직 안 잡고 다음 틱에 다시 본다.
	//   2026-09-18 첫 PIE: 발이 9cm 뜬 첫 프레임에 241cm 위 렛지를 잡아 캡슐이 110cm 를 끌려 올라갔다(«점프 없이 솟는» 그림).
	//   점프 궤적이 손을 렛지에 데려간 프레임에 잡아야 잡는 동작으로 보인다. 정점 127 + 132 = 259 라 250 렛지는 정점 근처에서 걸린다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangReachTolerance = 40.0f;

	// 잡는 순간 캡슐이 매달림 자리로 미끄러져 들어가는 시간(s). 0 이면 한 프레임에 붙는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangGrabBlendTime = 0.15f;

	// 옆걸음 축(-1~+1) 보간 속도. 입력이 바뀔 때 대기↔옆걸음 블렌드가 튀지 않게.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Hang")
	float HangMoveAxisInterpSpeed = 8.0f;

	// ===================== 자유 등반 (단계 2, 2026-09-18) =====================
	// 벽면 등반의 2D 블렌드스페이스. 가로 -1(왼쪽)~+1(오른쪽), 세로 -1(아래)~+1(위), 가운데가 대기.
	// 비어 있으면 자유 등반이 꺼진다 - 없는 것은 깨진 것이 아니다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UBlendSpace> ClimbBlendSpace;

	// 방향별 등반 몽타주. 이것이 있으면 그 클립의 루트모션이 몸을 몬다 - 옆걸음과 같은 구조다.
	// 왜 블렌드스페이스가 아니라 몽타주인가(2026-09-19, user 「루트모션 적용 안한것같네」):
	//   블렌드스페이스로는 루트모션이 이동 컴포넌트까지 오지 않았다(원인 미상). 몽타주 루트모션은
	//   이 프로젝트에서 이미 돌고 있다 - 렛지 옆걸음이 그것으로 움직인다. 되는 길을 쓴다.
	// 블렌드스페이스는 대기 자세용으로 남는다 - 몽타주가 없는 동안 그것이 보인다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageUp;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageDown;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageLeft;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageRight;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageUpLeft;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageUpRight;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageDownLeft;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	TObjectPtr<UAnimMontage> ClimbMontageDownRight;
	// 오른쪽은 왼쪽 클립을 미러해 구운 것이다(VB_Climb_Right_M). 팩의 Climb_Right 는 Climb_Left 를
	//   평행이동한 것뿐이라 못 쓴다(설계 12.1). 미러는 MDT_UEFN_Mannequin 으로 굽고 root 이동 부호가
	//   +243 에서 -243 으로 뒤집히는 것을 대조군(원본 불변)과 함께 확인했다(2026-09-19).

	// 몽타주가 비어 있는 방향에서 쓰는 일정 속도(cm/s). 그 구간은 발이 미끄러진다 - 부팅 경고가 그것을 알린다.
	// 왜 루트모션이 아닌가(2026-09-19 실측): 등반 자세는 블렌드스페이스라 몽타주가 아니고,
	//   루트모션 모드를 RootMotionFromEverything 으로 바꿔도 이동 컴포넌트까지 값이 오지 않았다
	//   (모드는 켜졌고 엔진 쪽 조건도 다 맞는데 옮겨진 양이 계속 0 이었다. 원인은 못 가렸다 - 엔진이
	//    감춘 값을 봐야 해서 우리 계측으로는 못 간다). user 결정: 입력 구동 + IK 로 간다.
	// 72 인 이유: 블렌드스페이스 표본의 RateScale 이 수직·대각을 72 cm/s 로 맞춰 놨다(설계 12.2).
	//   순수 좌우만 예외다 - 전용 클립이 없어 대각 둘의 블렌드로 나오고 그 수평 속도는 약 53 이라
	//   좌우로 갈 때는 약 26% 빠르게 민다. 좌우 표본이 들어오면 그때 방향별 속도로 가른다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbSpeed = 72.0f;

	// 말단을 벽면에서 띄우는 여유(cm). 0 이면 손목·발목이 면에 딱 붙어 손가락·발가락이 파고든다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbLimbWallClearance = 3.0f;

	// 발 전용 여유(cm). 손보다 커야 한다 - 발볼 뼈(ball_l/r)가 발의 끝이 아니기 때문이다.
	// 왜 따로 두나(2026-09-19 user 「발이 벽에 들어가고」, 두 번째): 처음엔 발목을 벽면에 뒀다가
	//   발볼이 발목보다 7~15cm 앞이라는 것을 재고 발볼로 옮겼다. 그런데 발가락은 발볼보다 더 나가고
	//   그 자리에는 뼈가 없다. 같은 실수를 한 칸 더 간 셈이다.
	// 이 값만은 뼈에서 못 잰다. 발끝은 메시에만 있고 스켈레톤에 없어서, 앞의 두 값(발목->발볼 7~15cm,
	//   손목->손끝 0.4~0.7cm)처럼 AnimPose 로 유도할 수가 없다. 8 은 발목->발볼 거리의 절반쯤을 잡은
	//   출발점이고 눈으로 맞출 값이다. 추측이라는 것을 여기 적어 둔다 - 다음에 볼 때 근거를 찾지 말 것.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbFootWallClearance = 8.0f;

	// 말단을 면에 맞출 때 한 번에 옮길 수 있는 최대 거리(cm).
	// 이 값 하나가 «잡았나 뻗나» 판정을 대신한다(2026-09-19 재설계). 근거는 실측 두 개다 -
	//   붙어 있는 말단은 벽에서 3.5cm 어긋나고(깊이를 두 요구의 중간으로 잡은 결과),
	//   뻗는 말단은 20~31cm 다. 그 사이에 상한을 두면 붙은 것은 온전히 맞춰지고
	//   뻗는 것은 상한만큼만 당겨져 뻗는 동작이 그대로 남는다. 판정할 것이 없다.
	// 종전에는 몸 기준 면내 흐름으로 판정했는데(ClimbReachSpeed·ClimbFlowOutlier, 둘 다 은퇴),
	//   그 판정이 프레임마다 뒤집혀 손이 떨렸다. 판정을 다듬는 대신 판정을 없앴다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbIKMaxCorrection = 6.0f;

	// 오차를 한 프레임에 얼마나 따라잡나(0~1). 1 이면 한 방에 맞추려 들고 그러면 진동한다.
	// 왜 한 방이 아니라 조금씩인가(2026-09-19, 두 판을 실패한 뒤): 내가 노드에 넘긴 값과 노드가
	//   실제로 적용하는 양은 같지 않다(뼈 공간이라 그 뼈의 회전을 타고, 팔 길이 한계에도 걸린다).
	//   한 방에 맞추는 식은 그 둘이 같다는 전제 위에 서고, 전제가 깨지면 오차가 다음 프레임에 실려 떨린다.
	//   적분은 전제가 필요 없다 - 결과를 보고 조금씩 고치므로 노드가 무엇을 하든 수렴한다.
	// 0.35 의 근거: 60fps 에서 시상수가 약 0.05초다(1/0.35 프레임 ≈ 3프레임). 붙는 데 세 프레임이면
	//   눈에 안 띄고, 고리 이득이 1 보다 충분히 작아 진동하지 않는다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbIKGain = 0.35f;

	// IK 세기가 잡음↔뻗음 사이를 오갈 때의 보간 속도(초당). 한 프레임에 확 바뀌면 말단이 튄다.
	//   실측 로그에서 세기가 0.82 에서 0.00 으로 한 번에 떨어지는 것이 보였다(2026-09-19).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbIKWeightInterpSpeed = 10.0f;

	// 손·발 IK 세기(0~1). 0 이면 그 말단은 클립 그대로다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbHandIKWeight = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbFootIKWeight = 1.0f;

	// 벽면에서 root 를 얼마나 떼어 놓나(cm). 대기와 이동이 다르다.
	// 실측(2026-09-18, root 대비 «가장 깊은 말단»): 대기 48.7 / 수직 42.5 / 수평 43.4 / 대각 47.3~47.4.
	// 왜 평균이 아니라 최대인가: 손발은 벽에서 떨어졌다 붙었다 하고(깊이 폭 10~27cm) 벽면은 «붙어 있는
	//   순간» 이다. 평균으로 재면 32~48 로 갈려 15cm 를 틀린다. 최대로 재면 폭이 6.2cm 로 모인다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbWallDepthIdle = 48.7f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbWallDepthMoving = 45.0f;

	// 대기↔이동 깊이 보간 속도(cm/s)와 축 보간 속도. 매달림의 같은 이름 값과 역할이 같다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbDepthInterpSpeed = 60.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbMoveAxisInterpSpeed = 8.0f;

	// 붙는 순간 캡슐이 벽 자리로 미끄러져 들어가는 시간(s).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbEnterBlendTime = 0.15f;

	// 방향이 바뀌었을 때 그 방향이 이만큼 유지돼야 몽타주를 갈아끼운다(초).
	// 왜(2026-09-19 실측): 키보드로 W 에서 S 로 갈 때 중간 구획(대각·좌우)을 스치면서
	//   1초에 몽타주가 여섯 번 바뀌었다(15ms / 95ms / 109ms 간격 포함). 바뀔 때마다 블렌드가 새로 돌고
	//   IK 판정이 쉬어서 손발이 튄다. user 「손 덜덜 떨리고 W 누르다가 바로 S 누르면 사삭」.
	// 대기에서 처음 거는 것에는 안 걸린다 - 그건 입력이 늦게 먹는 것으로 느껴진다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Climb|Free")
	float ClimbDirectionHoldTime = 0.12f;

	// ===== 사다리 (2026-09-19, 설계 .sw/traversal/design.md 14) =====

	// 사다리에 붙어 있을 때의 대기 자세. 제자리 클립이라 몽타주로 걸어 자기 자신을 가리키게 돌린다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	TObjectPtr<UAnimMontage> LadderIdleMontage;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	TObjectPtr<UAnimMontage> LadderUpMontage;

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	TObjectPtr<UAnimMontage> LadderDownMontage;

	// 대기의 반대쪽 벌(좌우를 뒤집은 것). 비어 있으면 위의 LadderIdleMontage 하나로 간다.
	// 왜 두 벌인가(2026-09-19 user 「Idle 왼쪽과 오른쪽 나누어야 할 것 같은데」):
	//   오르내리기 클립은 1.2초마다 좌우가 뒤집히는데(실측: 발 좌우차가 +38 <-> -38 로 왕복)
	//   대기는 한 벌뿐이라 그 두 위상 중 한쪽하고만 맞는다. 나머지 절반에서 손을 떼면
	//   손발 넷이 한꺼번에 40cm 씩 교차하며 자리를 바꾼다.
	// 자산은 MirrorModifier + MDT_UEFN_Mannequin 으로 구웠다(2026-09-19).
	//   대조: 손 189.0<->149.4, 발볼 42.7<->80.5 로 정확히 맞바뀌고 깊이·골반 높이는 그대로다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	TObjectPtr<UAnimMontage> LadderIdleMirrorMontage;

	// 사다리 면에서 몸(root)까지의 거리(cm). 손 접촉 깊이에 맞춘다.
	// 2026-09-22: DA의 65.4cm에서는 팔 길이가 부족했다. 38cm에서 손 도달 범위를 확보하고,
	// 발의 깊이 차이(약 25cm)는 후처리의 사다리 IK 노드가 현재 포즈에서 직접 보정한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderDepth = 38.0f;

	// 클립 안에서 «발이 가로대를 딛는 높이» 의 위상(cm). 클립 시작을 0 으로 본 높이를 가로대 간격으로
	//   나눈 나머지다. 실측 2026-09-19(디딤 열둘 전수): Up 1.62~1.78, Down 1.63~1.84 로 평균 1.67 이다.
	//   두 클립이 같은 격자를 쓴다는 뜻이라 값 하나면 되고, 흩어진 폭 0.22cm 는 IK 가 먹는다.
	// 어디에 쓰나: 몽타주를 «어느 위상에서 시작할지» 고르는 데 쓴다. 캐릭터의 지금 높이는 아무 값이나
	//   될 수 있는데 가로대는 정해진 높이에만 있으므로, 시작 위상을 골라 디딤이 가로대에 떨어지게 한다.
	//   캐릭터를 순간이동시켜 맞추지 않는 이유가 이것이다 - 그러면 최대 20cm 를 튀어야 한다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderStancePhase = 1.67f;

	// 위쪽 손이 root 보다 얼마나 위에 있나(cm). 실측 189.0 (VB_Ladder_Idle 의 hand_l).
	// 어디에 쓰나: 꼭대기에서 멈추는 높이를 정한다. 사다리 높이에서 이 값을 뺀 데까지만 오른다.
	// 왜 필요한가 (2026-09-19 user 「공중 손잡고있잖아」): 종전에는 root 가 사다리 꼭대기에 닿을
	//   때까지 올랐다. 그러면 손은 꼭대기보다 189cm 위, 즉 허공을 쥔다. 실측으로 높이 258 일 때
	//   손이 z=447 이고 사다리 꼭대기가 400 이었다.
	// 잡는 것이 손이므로 «손이 아직 가로대에 닿는가» 가 꼭대기의 기준이다. 발이 아니다.
	// 진짜 답은 꼭대기에서 올라서는 동작인데 그 클립이 없다(설계 14.3). 그것이 생기기 전까지의 선이다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderTopHandReach = 189.0f;

	// 대기 자세에서 «아래쪽 발» 이 root 보다 얼마나 위에 있나(cm). 실측 42.7 (ball_l, 거울 벌은 ball_r).
	// 어디에 쓰나: 멈출 때 그 발이 가로대 윗면에 놓이도록 높이를 가장 가까운 자리로 당긴다.
	// 왜 필요한가 (2026-09-19 user 「발이 안맞고 손도 안맞고」): 오르는 동안의 정렬은
	//   LadderStancePhase 가 맞추는데, 그것은 «디딤이 일어나는 순간» 에만 성립한다.
	//   손을 떼는 자리는 아무 데나라 최대 간격의 절반(약 20cm)까지 어긋난 채로 선다.
	//   실측: 높이 258 에서 아래 발이 z=300.7 이고 가로대가 281.8 / 322.0 이라 19cm 사이에 떴다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderIdleFootOffset = 42.7f;

	// 멈출 때 그 자리로 당겨 앉는 속도(FInterpTo). 8 이면 20cm 를 0.3초쯤에 덮는다.
	//   0 이면 안 당긴다 - 어긋난 채로 서는 종전 동작이다.
	// 왜 즉시 옮기지 않나: 최대 20cm 라 한 프레임에 옮기면 그대로 순간이동으로 보인다.
	//   대기 블렌드(0.25초)와 겹치게 두면 «가로대에 내려앉는» 것으로 읽힌다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderIdleSettleSpeed = 8.0f;

	// 입력이 끊긴 뒤 이만큼 조용해야 당겨 앉기가 시작된다(초).
	// 왜 필요한가 (2026-09-19 user 「s키를 다다다닥 누르니까」): 탭 사이마다 당겨 앉기가 끼어들면
	//   방금 누른 것을 되돌리거나(위쪽 가로대) 한 칸을 통째로 끌어내린다(아래쪽 가로대).
	//   방향으로 막아 봤는데 그러면 최대 40cm 를 미끄러진다 - 방향이 아니라 «때» 의 문제였다.
	//   탭보다 길게 잡아 두면 두드리는 동안에는 아예 안 끼어들고, 진짜로 멈춘 뒤 한 번만 앉는다.
	// 0.2 인 근거: 사람이 연타하는 간격이 보통 0.1~0.15초다. 그보다 길어야 «멈췄다» 로 읽힌다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderIdleSettleDelay = 0.2f;

	// === 말단 IK (2026-09-20, user 「손이 못따라가는 것 같네 IK가」) ===
	// 이 자산은 손과 발을 두 축 모두에서 다른 자리에 둔다 - 깊이 27.1cm, 높이 위상 27.0cm.
	//   평평한 사다리로는 각 축에서 한쪽만 맞출 수 있고, 두 축을 동시에 닫는 것은 IK 뿐이다.

	// 한 말단의 최대 보정 거리(cm). 발의 깊이 차이와 높이 오차를 함께 덮는다.
	// 팔/다리의 실제 길이 제한은 별개다. 상한을 키워도 닿지 못하는 목표는 몸 배치부터 고친다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder", meta=(ClampMin="0"))
	float LadderIKMaxCorrection = 36.0f;

	// 클립의 지지 발볼이 root보다 앞에 놓이는 거리(cm). 0이면 원본 발 궤적을 유지한다.
	// 몸/발판 배치와의 깊이 차이는 스윙 중에도 유지하는 기본 포즈 보정으로 처리한다.
	// 접촉 가중치에 섞으면 발을 뗄 때 큰 깊이 보정이 꺼졌다가 다시 켜진다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder", meta=(ClampMin="0"))
	float LadderFootPoseDepth = 0.0f;

	// 기본 포즈를 맞춘 뒤 발판에 고정할 작은 잔차의 한도이자 접촉 획득 거리(cm).
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder", meta=(ClampMin="0"))
	float LadderFootIKMaxCorrection = 12.0f;

	// hand_l/r 뼈 로컬 공간의 봉 그립 중심(cm). 0이면 손목 원점을 사용한다.
	// 클립/체형별로 측정한다. 손목 자체를 봉 중심에 놓으면 손가락은 봉을 벗어난다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	FVector LadderLeftHandGripOffset = FVector::ZeroVector;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	FVector LadderRightHandGripOffset = FVector::ZeroVector;

	// 잡은 손 / 밟은 발의 IK 세기(0~1). 0 이면 그 말단은 클립 그대로다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderHandIKWeight = 1.0f;
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderFootIKWeight = 1.0f;

	// 접촉/해제 속도와 블렌드 시간은 AnimNode_VBLadderIK의 편집 가능한 설정이다.
	// 보정 전 입력 포즈로 판정하므로 과거 소켓 기반 속도 판정의 되먹임이 없다.

	// 붙는 순간 지금 자리에서 사다리 자리로 옮겨 가는 시간(초). 등반의 ClimbEnterBlendTime 과 같은 역할.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderEnterBlendTime = 0.15f;

	// 오르내리는 기준 속도(cm/s). 평소에는 쓰이지 않는다 - 이동의 주인은 클립의 루트모션이다.
	//   여기가 쓰이는 자리는 둘뿐이다: 그 방향 몽타주가 아예 없을 때의 폴백, 그리고 한 틱 이동의 상한(8배).
	// 33.5 는 클립이 실제로 내는 속도다(AnimPose 실측 2026-09-19): 세로 이동이 7.2초에 241.5cm.
	//   내려가기도 -241.5 로 대칭이라 한 값이면 된다. 순간 최고속은 98 cm/s 로 평균의 2.9배다.
	// 경위 2026-09-19: 처음엔 이 값으로 캡슐을 직접 밀었는데, 클립의 세로 이동이 골반에 들어 있어
	//   (root Z=0, enable_root_motion=false) 애니와 몸이 따로 놀았다. user 「Climb 이랑 처음이랑 똑같은 버그」.
	//   그래서 그 이동을 root 트랙으로 옮겨(자세는 보존) 루트모션으로 몰게 바꿨다. 고정 속도로 미는 구조
	//   자체가 결함이었지 값이 틀렸던 게 아니다 - 60 을 33.5 로 고친 것은 증상만 줄인 것이었다.
	// 같이 움직여야 하는 값: AVBLadder::RungSpacing(40.3). 둘 다 같은 클립에서 나왔고
	//   속도는 「1초에 얼마나」, 간격은 「한 번 잡을 때마다 얼마나」다. 클립을 갈면 둘 다 다시 잰다.
	//   한 자산에 사는 사실이지만 영역이 달라(애니 대 레벨 기하) 한 곳에 못 둔다 - 그래서 서로 가리킨다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Ladder")
	float LadderSpeed = 33.5f;
};
