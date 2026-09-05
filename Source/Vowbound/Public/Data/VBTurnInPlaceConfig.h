// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/VBWeaponStateTypes.h" // EVBWeaponType (행의 키)
#include "Engine/DataAsset.h"
#include "VBTurnInPlaceConfig.generated.h"

class UAnimMontage;
class UAnimSequence;

/**
 * 제자리 회전 한 행. «무기 x 자세 x 부호 있는 요구 각도 구간» 으로 클립을 고른다.
 *
 * 왜 부호 있는 각도인가: 종전 구조는 회전 종류를 enum(L90/R90/180)으로 갖고 180 에는 클립이
 * 하나뿐이었다. 그래서 어느 쪽으로 돌든 같은 클립이 나왔고, 절반의 회전은 반대로 도는 몸을
 * 보여줬다(FIND-105, user PIE 로 서있기·크라우치 양쪽에서 확인). 부호를 값에 넣으면 그 결함이
 * 데이터 규격에서 사라진다 - 왼쪽 회전은 음수 행만 후보가 된다.
 *
 * 왜 «담당 구간» 을 적나 (2026-08-28, Codex 교차검토 후): 종전에는 같은 부호 중 기준각이 가장
 * 가까운 행을 골랐다. 45/90/180 을 갖추면 그 규칙이 작은 요구각에 큰 클립을 붙일 수 있고, 그러면
 * 회전 워프가 클립을 «줄이는» 쪽으로 돈다. 축소 워프는 2026-08-28 에 실측으로 해롭다고 판명났다
 * (90 클립을 60도로 쪼그라뜨려 발 접지와 몸 회전이 어긋났다). 구간을 저작하면 그 사고가 코드가
 * 아니라 데이터 검토에서 잡힌다. 유도식으로 두면 정책이 공식 안에 숨는다.
 */
/**
 * 왜 EditAnywhere 인가 (2026-08-28). 이 자산의 행은 MCP(파이썬)로 저작한다. `EditDefaultsOnly` 는
 * 인스턴스 쓰기를 막아서 파이썬이 값을 못 넣고("cannot be edited on instances"), 구조체 생성자
 * 인자 경로도 bool 필드에서 막힌다(실측). 데이터 자산은 레벨에 놓는 인스턴스가 아니라 자산 자체라
 * 둘의 실질 차이가 없다. 되돌리면 저작 경로가 막히니 «정리» 삼아 EditDefaultsOnly 로 바꾸지 마라.
 */
USTRUCT(BlueprintType)
struct FVBTurnInPlaceEntry
{
	GENERATED_BODY()

	// 이 행이 어느 «표현 포즈 계열» 의 것인가. 선택한 무기(CurrentWeaponType)가 아니라
	//   지금 실제로 재생되는 포즈 계열(VBAnimInstance::MMWeaponType)이다.
	// 왜 갈리나: `CurrentWeaponType` 은 평상 상태로 갈 때만 비워지므로 납도(상태2)에서도 선택 무기가
	//   남는다. 그런데 납도의 포즈 풀은 `SheathedMMPool=None` 이라 맨손 풀이다. 선택 무기로 키를 잡으면
	//   «맨손 자세인데 무기 회전 클립» 이 나온다(2026-08-29 Codex 교차검토, 트레이스로도 확인).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	EVBWeaponType PoseWeapon = EVBWeaponType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	bool bCrouching = false;

	// 부호가 방향이다. 음수=왼쪽 / 양수=오른쪽 (UE yaw 관례. L90 루트 클립 실측 -90, R90 은 +90).
	// 값 자체는 «그 클립의 루트가 실제로 도는 각» 이다(자산 실측). 요구 각도와의 차이를 워프가 메우므로
	// 여기 적는 값이 틀리면 워프가 틀린 배율로 돈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace", meta=(ClampMin="-180.0", ClampMax="180.0"))
	float ReferenceYaw = 0.0f;

	// 이 행이 담당하는 요구 각도 구간(절댓값, 도). RequestMin 이상 RequestMax 미만.
	// 한 방향의 구간들은 서로 겹치지 않고 180 을 넘어 빈틈없이 이어져야 한다 - IsDataValid 가 본다.
	// 상한이 180 을 넘는 이유 (2026-09-04): 카메라를 반 바퀴 «넘겨» 감으면 남은 회전량이 180 을 넘는다(실측 196~204).
	//   그것을 180 에서 자르면 잔여가 남아 카메라를 덜 보고, 최단 경로로 바꾸면 반대 방향 클립이 나간다(user PIE 5건).
	//   그래서 «한 번에 직접 낼 수 있는 상한» 은 이 값이 유일한 홈이고, 워프 배율(WarpScaleRange)이 낼 수 있는
	//   범위 안이어야 한다(IsDataValid). 이 상한을 넘는 요청은 ResolveStepRequest 가 같은 방향으로 나눈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace", meta=(ClampMin="0.0", ClampMax="360.0"))
	float RequestMin = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace", meta=(ClampMin="0.0", ClampMax="360.0"))
	float RequestMax = 0.0f;

	// 루트모션이 켜진 몽타주여야 한다. 회전을 «몸» 에만 가진 클립은 몽타주가 끝날 때 그 회전이
	// 포즈와 함께 사라져 되돌아온다(FIND-105 에서 다섯 번 확인).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	// 재생 속도. 자산의 길이가 자세마다 달라서 회전 각속도가 튄다 - 크라우치 클립은 0.4초에 180도
	// (450도/초)인데 서있기는 약 110도/초다. 몽타주의 RateScale 이 아니라 여기서만 조절한다(홈이 하나).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace", meta=(ClampMin="0.05", ClampMax="4.0"))
	float PlayRate = 1.0f;

	// 회전 워프가 클립 각도를 요구 각도에 맞출 때 허용하는 배율. 벗어나면 낼 수 있는 만큼만 내고
	// 나머지는 다음 회전이 가져간다.
	// 아래쪽 기본값이 1.0 인 이유 - 늘리는 워프와 줄이는 워프는 대칭이 아니다. 늘리면 발이 조금
	// 끌리는 정도지만, 줄이면 애니가 만든 발 접지와 몸의 회전이 어긋나 뭉갠다(2026-08-28 실측).
	// 행마다 다른 이유: 서있기 90 과 크라우치 180 에 같은 정책을 강제할 근거가 없다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	FVector2D WarpScaleRange = FVector2D(1.0f, 1.4f);

	// 이 행이 «반대 부호의 요청» 도 받는가 (2026-09-03, user 결정). 180 근처는 어느 쪽으로 돌든 같은 방위에
	//   닿으므로 왼쪽 180 원본 한 클립으로 오른쪽 요청도 낸다. 워프 목표는 «방위» 라 캡슐은 요청 방위에 정확히
	//   서고, 몸은 클립 방향으로 돈다. 그래서 |기준각| 이 180 인 행에만 허용한다(IsDataValid 가 막는다) -
	//   90 행이 반대를 받으면 목표까지 180 이 남아 워프가 어느 쪽으로 보정할지 정해지지 않는다.
	// 왜 이렇게 하나: 반대 방향 클립이 없다. 미러는 비대칭 자세를 뒤집고(착지가 대기와 86~132cm 어긋남, 칼 손
	//   반대), 역재생은 동작의 앞뒤를 뒤집는다(머리가 반대를 봄, 골반이 뒤로 갔다 옴) - 둘 다 실측과 user PIE 로
	//   확인했다. 제대로 된 반대 방향 클립은 사람 손이 든다. 그 클립이 오면 이 플래그를 끄고 행을 하나 더 넣는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	bool bServesBothDirections = false;
};

/**
 * 착지 자세 참조 (2026-09-02, FIND-105). (표현 무기 x 자세) 조합이 회전 몽타주의 앞뒤에서 «돌아와야 하는»
 * 대기 자세를 가리킨다. 검증기가 몽타주의 첫 프레임·블렌드아웃 시점·마지막 프레임의 발 위치와 발끝 방향을
 * 이 클립의 0초와 비교한다.
 *
 * 왜 필요한가: 회전 몽타주가 대기 자세에서 먼 자세로 끝나면 블렌드아웃이 그 거리를 발 미끄러짐으로 만들고
 *   LegIK 가 그것을 «다리가 도는» 모양으로 바꾼다. 아홉 가설이 전부 «넘기는 방법» 을 바꿨고 «넘기는 목표
 *   자세» 를 재지 않았다. 이 참조가 그 자세의 홈이다.
 * 왜 MM 대기 풀에서 유도하지 않나: 풀은 런타임 Chooser 가 고른다. 검증기가 그 판정을 흉내 내면 판정 구현이
 *   둘이 된다. 여기 두는 것은 값의 복사가 아니라 «이 조합의 미술 계약» 을 가리키는 포인터다.
 */
USTRUCT(BlueprintType)
struct FVBTurnInPlaceLandingPoseReference
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	EVBWeaponType PoseWeapon = EVBWeaponType::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	bool bCrouching = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	TObjectPtr<UAnimSequence> Idle = nullptr;
};

/**
 * 검증기가 쓰는 뼈 이름과 임계. 기본값은 전부 «미설정» 이고 미설정은 오류다 - 재지 못하는 상태를 통과로
 * 떨어뜨리지 않는다(규칙 27). 값은 DA_TurnInPlace 에 저작한다. 스켈레톤이 바뀌면 코드가 아니라 데이터가 바뀐다.
 *
 * 진입(첫 프레임)과 착지(블렌드아웃 시점·마지막 프레임)의 임계를 나눈 이유: 회전 클립의 첫 프레임과 대기 클립의
 *   첫 프레임은 서로 다른 자산이라 10cm 안팎의 차이가 정상이고, 착지는 같은 자세로 «돌아와야» 하므로 더 좁게
 *   본다. 같은 임계를 쓰면 입도가 틀린다.
 */
USTRUCT(BlueprintType)
struct FVBTurnInPlaceValidationRules
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Bones")
	FName FootBoneL = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Bones")
	FName FootBoneR = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Bones")
	FName BallBoneL = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Bones")
	FName BallBoneR = NAME_None;

	// 단위는 cm 와 도. 경고 < 오류 여야 하고 0 은 미설정이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Entry", meta=(ClampMin="0.0"))
	float EntryWarnCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Entry", meta=(ClampMin="0.0"))
	float EntryErrorCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Entry", meta=(ClampMin="0.0"))
	float EntryWarnDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Entry", meta=(ClampMin="0.0"))
	float EntryErrorDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Landing", meta=(ClampMin="0.0"))
	float LandingWarnCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Landing", meta=(ClampMin="0.0"))
	float LandingErrorCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Landing", meta=(ClampMin="0.0"))
	float LandingWarnDeg = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Landing", meta=(ClampMin="0.0"))
	float LandingErrorDeg = 0.0f;

	// 워프 창이 닫힌 뒤 클립 끝까지의 루트 순회전 허용치(도). 워프는 창 안에서만 보정하고 창 밖의 회전은
	//   원값 그대로 캡슐에 실린다 - 순회전이 남으면 요청각에서 그만큼 벗어난 자리에 선다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Warp", meta=(ClampMin="0.0"))
	float MaxNetYawAfterWarpDeg = 0.0f;

	// IK 발의 부모 뼈(ik_foot_root). 로컬 회전이 항등이어야 한다(허용각 아래).
	// 왜 (2026-09-02, FIND-105 의 진짜 원인): 컴포넌트 공간에서 IK 발이 실제 발과 같아도, 이 뼈가 루트를 되감는
	//   +180 을 로컬로 들고 있으면(카타나·대검 180 원본) 대기 클립(항등)과 블렌드할 때 부모 180 과 자식 180 이
	//   각자 «최단 호» 를 고른다. 둘이 반대로 가면 IK 목표가 360도를 쓸고 LegIK 가 발을 끌고 돈다.
	//   격투 클립은 항등이라 멀쩡했다. 컴포넌트 공간만 재면 이 결함이 안 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Bones")
	FName IkRootBone = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace|Bones", meta=(ClampMin="0.0"))
	float MaxIkRootLocalDeg = 0.0f;
};

/**
 * 제자리 회전 설정.
 *
 * 소유권 판정의 홈이 여기 하나다 (2026-08-28 개정). 종전에는 `HasProfile(무기, 자세)` 가
 * «행이 하나라도 있나» 만 보고 캡슐 회전을 잠갔는데, 그 답은 방향도 각도도 안 본다. 그래서 클립이
 * 한 방향뿐인 조합(크라우치)에 행을 넣으면 반대 방향으로는 애니도 안 돌고 캡슐도 안 도는
 * «그 방향으로 못 도는 캐릭터» 가 됐다. 이제 `ResolveRequest` 하나가 «이 요청을 낼 행이 실제로
 * 있는가» 에 답하고, 애님 인스턴스와 캐릭터가 그 답을 같이 쓴다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBTurnInPlaceConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	TArray<FVBTurnInPlaceEntry> Entries;

	// (무기, 자세) 조합당 정확히 하나. IsDataValid 가 누락·중복·미사용·스켈레톤 불일치를 오류로 낸다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	TArray<FVBTurnInPlaceLandingPoseReference> LandingPoseReferences;

	// 검증 임계와 뼈 이름. 미설정이면 IsDataValid 가 통과가 아니라 오류를 낸다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace")
	FVBTurnInPlaceValidationRules ValidationRules;

	// 회전이 끝난 뒤 이 각도 아래로 내려와야 다음 회전을 시작할 수 있다(경계에서 왕복하지 않게).
	// 폴백 상태를 푸는 기준도 이 값이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace", meta=(ClampMin="0.0", ClampMax="180.0"))
	float RearmYawThreshold = 15.0f;

	// 요구 각도가 «아직 커지는 중» 이면 클립을 고르지 않고 기다린다. 그 판정 기준(도/초).
	// 왜 필요한가: 문턱을 넘는 첫 프레임에 고르면 빠른 180 플릭이 «90 이 막 넘은» 값으로 잡혀
	//   90 클립이 뽑히고, 그것이 끝난 뒤 남은 각이 다시 요청돼 180 이 또 나온다 - 회전 한 번이
	//   두 번이 된다(2026-08-29 실측: aim 96.0 에 90 을 고르고 0.53초 뒤 aim 132.8 에 180).
	//   카메라가 멎을 때까지 기다리면 그 회전의 «최종 각도» 로 한 번에 고를 수 있다.
	// 값이 작을수록 오래 기다린다. 0 이면 기다리지 않는다(종전 동작).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|TurnInPlace", meta=(ClampMin="0.0"))
	float RequestSettleRate = 90.0f;

	// 부호가 같고 |요구각| 이 담당 구간에 드는 행. 없으면 nullptr = 「이 요청은 못 낸다」.
	// 진입 임계를 따로 두지 않는 이유: 가장 작은 구간의 하한이 곧 진입 임계다. 값이 둘이면
	// 둘이 어긋날 수 있고, 어긋나면 «임계는 넘었는데 낼 클립이 없는» 구간이 생긴다.
	const FVBTurnInPlaceEntry* ResolveRequest(EVBWeaponType InPoseWeapon, bool bInCrouching, float InRequestedYaw) const;

	// 이 조합이 새 경로 위에 있나. 회전을 «시작할 각도» 를 모으려면 대기 중에도 캡슐이 멈춰 있어야 하고,
	// 요구각이 0 인 순간에는 방향이 없어 행을 못 고른다. 그래서 정지 판정은 조합 단위로 남긴다.
	// 한쪽 방향만 등록해 생기던 «그 방향으로 못 도는 캐릭터» 는 IsDataValid 가 자산 단계에서 막고,
	// 그래도 못 내는 요청이 나오면 런타임 걸쇠가 캡슐 회전으로 되돌린다 - 층이 셋이다.
	bool HasProfile(EVBWeaponType InPoseWeapon, bool bInCrouching) const;

	// 그 방향에서 회전이 시작되는 각도 = 그 방향 행들의 담당 구간 하한 중 최솟값.
	// 따로 저작하지 않고 행에서 유도한다 - 값이 둘이면 어긋날 수 있고, 어긋나면 «임계는 넘었는데
	// 낼 클립이 없는» 구간이 생긴다. 행이 없으면 false.
	bool TryGetEnterYaw(EVBWeaponType InPoseWeapon, bool bInCrouching, float InRequestedYaw, float& OutEnterYaw) const;

	// 남은 회전량(부호 있음, 180 을 넘을 수 있다)을 «지금 한 번에 낼 요청» 으로 자른다.
	//   담당 구간 안이면 그대로. 넘으면 같은 방향으로 나누되, 나머지가 진입 각(TryGetEnterYaw) 아래로
	//   떨어져 아무도 안 돌리는 잔여가 되지 않게 첫 조각을 고른다. 그 방향에 행이 없으면 false.
	//   상한을 여기서 유도하는 이유: 담당 상한의 홈은 행(RequestMax)뿐이다. C++ 에 상수를 두면 홈이 둘이 된다.
	bool ResolveStepRequest(EVBWeaponType InPoseWeapon, bool bInCrouching, float InRemainingYaw, float& OutStepYaw) const;

#if WITH_EDITOR
	// 반쪽 프로필(한 방향만 덮은 조합)과 구간의 겹침·빈틈을 자산 단계에서 거부한다.
	// 막을 수 있는 것은 막는다 - 런타임 폴백은 안전망이지 정상 경로가 아니다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
