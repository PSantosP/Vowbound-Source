// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBFinisherConfig.generated.h"

class UContextualAnimSceneAsset;
class UAnimMontage;
class UPoseSearchDatabase;

/**
 * 처형 한 벌이 어느 쪽에서 걸리는가.
 *
 * 왜 데이터인가: 무기마다 처형이 넷이고(앞 둘·뒤 둘) 그중 하나를 골라야 하는데, 고르는 기준이
 *   코드에 있으면 처형을 추가할 때마다 그 코드를 고쳐야 한다. 방향을 각 설정이 스스로 말하게 하면
 *   고르는 쪽은 "방향이 맞는 것들 중 랜덤" 한 줄로 끝난다.
 * 가정: 방향 판정은 처형 시작 시점의 두 액터 yaw 차 하나로 낸다(그 순간 이후로는 안 바뀐다).
 */
UENUM(BlueprintType)
enum class EVBFinisherApproach : uint8
{
	Front UMETA(DisplayName="Front"),	// 마주 본 상태에서 시작
	Back  UMETA(DisplayName="Back"),	// 등 뒤에서 시작
};

/**
 * 처형 chooser 는 «오브젝트 컨텍스트» 다 - 전용 입력 구조체를 두지 않는다.
 *
 * 2026-09-19 에 FVBFinisherChooserInput(무기·속도)을 만들었다가 걷어냈다. 이유:
 *   우리 로코모션 chooser 가 이미 `EvaluateChooserMulti(this, ...)` 로 UVBAnimInstance 를
 *   컨텍스트로 넘기고, 그 클래스에 `CurrentWeaponType` 과 `Speed2D` 가 **이미 있다**.
 *   새 구조체를 두면 같은 값이 두 곳에 살고 chooser 저작 방식도 둘이 된다(규칙 11·26).
 * GASP 는 구조체 컨텍스트(`S_CHT_CharacterInteractionPSDs_IN`)를 쓰지만 그쪽은 AnimInstance 에
 *   그 값이 없어서 그랬을 뿐이다. 우리는 있다.
 */

/**
 * 카메라가 이 키에 «도달하는 방식».
 *
 * 왜 데이터인가: 같은 처형이라도 「쓸고 지나간다」와 「버티다 훅 넘긴다」는 다른 연출이고,
 *   그 선택은 값이지 코드가 아니다. 무기·적이 늘어도 여기만 고르면 된다.
 */
UENUM(BlueprintType)
enum class EVBFinisherCameraInterp : uint8
{
	// 앞뒤 키를 함께 보는 부드러운 곡선. 구간 전체를 쓰고, 키를 지날 때 속도가 이어진다.
	//   기본값인 이유: 키를 셋 이상 놓았을 때 사람이 기대하는 것이 이것이다.
	Curve   UMETA(DisplayName="커브 (부드럽게, 기본)"),

	// 직선을 일정 속도로. 의도적으로 기계적인 이동에 쓴다.
	Linear  UMETA(DisplayName="직선 (일정 속도)"),

	// 앞 키에 머물다가 마지막 BlendTime 초에 옮겨간다. 「그 구도로 버티다 훅」에 쓴다.
	Hold    UMETA(DisplayName="머물다 이동 (블렌드 시간)"),

	// 그 시각에 즉시 바꾼다. 컷 편집.
	Cut     UMETA(DisplayName="컷 (즉시)"),
};

/**
 * 처형 중 카메라가 지나갈 한 지점.
 *
 * 시각의 기준은 씬 재생 시간이지 월드 시간이 아니다. 처형에는 슬로우가 걸리는데(TimeDilationSegments),
 *   월드 델타로 키를 진행하면 카메라도 같이 느려져 연출이 무너진다. 두 축을 섞으면 슬로우 값을 바꿀 때마다
 *   카메라를 다시 잡아야 한다.
 * 위치·회전은 대상(피해자) 기준 상대값이다. 처형은 세계 어디서나 걸리므로 절대 좌표는 뜻이 없다.
 */
/**
 * 구조체 멤버는 EditAnywhere 다. EditDefaultsOnly 가 아니다.
 *
 * 왜: USTRUCT 에는 UObject 같은 «기본값 대 인스턴스» 구분이 없어서 EditDefaultsOnly 가 여기서는
 *   뜻을 더하지 않는다. 그런데 파이썬의 set_editor_property 는 그 플래그를 보고 거부하므로
 *   이 프로젝트의 저작 경로(MCP 스크립트)가 통째로 막힌다 - 2026-09-13 에 실제로 막혔다.
 *   바깥 배열 프로퍼티의 노출 키워드가 «어디에 보이나» 를 정하므로 디자이너 화면은 안 바뀐다.
 */
USTRUCT(BlueprintType)
struct FVBFinisherCameraKey
{
	GENERATED_BODY()

	// 씬 재생 시간(초). 0 = 처형 시작.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.0"))
	float Time = 0.0f;

	// 카메라가 설 자리. 앵커 기준 상대값이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher")
	FVector RelativeLocation = FVector::ZeroVector;

	// 카메라가 볼 지점. 역시 앵커 기준 상대값이다. 회전은 이 둘에서 계산한다 - 손으로 안 적는다.
	//
	// 왜 회전이 아니라 점인가(2026-09-13 전환):
	//   1) 위치를 옮기면 화각이 따라온다. 회전을 적어 두면 카메라를 20cm 옮길 때마다 각도를 다시
	//      계산해야 하는데 디테일 패널에서는 그걸 눈대중으로 할 수 없다. 그래서 실제로는 위치를
	//      못 만지게 된다 - 튜닝을 막는 것이 데이터 형식이었다.
	//   2) 값이 무엇을 찍는지 말해 준다. (70,0,60) 은 「가슴 높이, 조금 앞」으로 읽히지만
	//      pitch -12.7 / yaw -34.3 은 아무것도 안 알려 준다.
	//   3) 적 체격이 달라져도 안 깨진다. 키 큰 적에 같은 회전을 쓰면 머리가 잘리는데 점은 가슴을
	//      계속 가리킨다. 「모든 몬스터가 처형 가능」이 목표라 이게 크다.
	// 일부러 피사체를 안 보는 앵글이 필요하면 이 점을 옆으로 빼면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher")
	FVector LookAtOffset = FVector(0.0f, 0.0f, 100.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="5.0", ClampMax="170.0"))
	float FOV = 90.0f;

	// 이 키에 어떻게 도달하나. 커브가 기본이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(DisplayName="도달 방식"))
	EVBFinisherCameraInterp Interp = EVBFinisherCameraInterp::Curve;

	// 앞 키에 머물다가 마지막 몇 초에 옮겨갈 것인가.
	//
	// 「머물다 이동」에서만 뜻이 있어서 다른 모드에서는 아예 숨긴다 - 안 쓰이는 값이 보이면
	//   그것을 고쳐 놓고 왜 안 먹는지 찾게 된다. 값의 주인은 하나여야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.0",
	          EditCondition="Interp == EVBFinisherCameraInterp::Hold", EditConditionHides))
	float BlendTime = 0.2f;
};

/**
 * 처형 중 한 번 거는 슬로우.
 *
 * 값만 여기 있고 거는 것은 UVBTimeDilationSubsystem 이다. 그쪽이 이 프로젝트에서 전역 배율을
 *   쓰는 유일한 주인이고(요청 스택 min-wins), 여기서 SetGlobalTimeDilation 을 직접 부르면
 *   히트스톱 같은 다른 요청과 서로 덮어쓴다. 필드 이름을 그 API(PushTimed)에 맞춰 둔 이유다.
 */
USTRUCT(BlueprintType)
struct FVBFinisherTimeDilationSegment
{
	GENERATED_BODY()

	// 씬 재생 시간(초) — 카메라 키와 같은 축이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.0"))
	float Time = 0.0f;

	// 1.0 = 정상, 0.01 = 거의 정지. UVBTimeDilationSubsystem::PushTimed 의 Scale.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.01", ClampMax="1.0"))
	float Scale = 0.5f;

	// 실제 벽시계 기준 길이(초). 서브시스템이 게임시간 타이머로 환산한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.0"))
	float RealDuration = 0.3f;
};

/**
 * 처형 한 벌의 «연출». 안무는 씬 자산이 들고 나머지를 이것이 든다.
 *
 * 무엇: 카메라 경로 · 슬로우 · 피해 배수 · 조기 이탈 허용 여부.
 * 왜 씬 자산과 나누는가: 씬 자산은 플러그인이 정의한 형식이라 우리가 필드를 못 늘린다.
 *   그리고 «두 몸이 어떻게 맞물리나»(안무)와 «화면에 어떻게 보이나»(연출)는 바뀌는 이유가 다르다 —
 *   같은 안무에 다른 연출을 얹는 일이 생기고, 그 반대는 안 생긴다.
 * 전제: Scene 이 비면 이 설정은 쓸 수 없다. IsDataValid 가 저장 시점에 막는다.
 * 부작용: 없음(순수 데이터).
 *
 * 새 처형은 씬 자산 하나 + 이 설정 하나로 끝난다. 그것이 이 갈래의 합격 기준이다 — C++ 은 안 건드린다.
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBFinisherConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 두 몸의 안무. 역할·클립·상대 정렬·섹션을 든다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher")
	TObjectPtr<UContextualAnimSceneAsset> Scene = nullptr;

	// 검색 후보 한 벌과 연출 설정의 연결. 설정은 무기 데이터가 고르고, 자세는 MotionMatchMulti가 고른다.
	// 배선된 설정은 ContextualAnimation으로 되돌아가지 않는다. Scene은 저작 원본으로 보존한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher")
	TObjectPtr<UPoseSearchDatabase> InteractionDatabase = nullptr;

	// 무기 목록이 비어 있는 Fighter는 AnimInstance의 속도 chooser를 사용한다.
	// 무기 전용 목록은 각 설정의 DB를 함께 검색한다. Approach는 미이관 Scene의 선택에만 쓰인다.

	// 이 처형이 앞에서 걸리는 것인가 뒤에서 걸리는 것인가.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher")
	EVBFinisherApproach Approach = EVBFinisherApproach::Front;

	// 비면 카메라를 안 건드린다(게임플레이 카메라 그대로). 연출 없는 처형도 성립한다.
	//
	// TitleProperty 는 접힌 행에 무엇을 보여줄지 정한다. 없으면 행 머리가 전부 「[0] [1] [2]」라
	// 어느 키가 어느 시각인지 보려면 하나씩 펴야 한다. 키가 네다섯 개만 돼도 그 왕복이 저작을 막는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(TitleProperty="{Time}초  FOV {FOV}"))
	TArray<FVBFinisherCameraKey> CameraKeys;

	// 카메라 키의 상대 좌표가 «무엇에 대한» 상대인가. 씬 자산의 역할 이름이다.
	//
	// 왜 데이터인가: 지금 두 역할 처형은 전부 피해자 기준이 맞다. 그것을 C++ 에 박으면 역할 구성이
	//   다른 처형(셋이 얽히는 것, 무기가 역할을 갖는 것)을 넣을 때 코드를 고쳐야 한다 -
	//   「새 처형 = 자산 두 벌, C++ 무변경」이라는 이 갈래의 합격 기준을 그 자리에서 깬다.
	// 왜 공격자가 기본이 아닌가: 공격자는 장면 시작에 UVBFinisherComponent 가 자리를 옮긴다.
	//   움직이는 것을 기준으로 삼으면 키를 잡을 때마다 그 이동량을 빼야 한다.
	// 저작: GetOptions 로 «위 Scene 이 실제로 가진 역할» 중에서 고르게 한다. 자유 입력이면 오타 하나가
	//   「카메라가 안 나온다」로만 나타나고 그 증상으로는 원인을 못 찾는다. 막을 수 있는 자리이므로
	//   막는다(규칙 27) - 저장 시점 검증도 함께 건다(IsDataValid). Scene 이 비면 목록도 비는데,
	//   그것은 Scene 미할당 오류가 먼저 뜨는 상태다.
	// 런타임에 못 찾으면 카메라를 세우지 않고 경고한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(GetOptions="GetSceneRoleOptions"))
	FName CameraAnchorRole = FName("Victim");

	// 게임플레이 카메라로 되돌아가는 블렌드 시간.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="0.0"))
	float CameraReturnBlend = 0.35f;

	/**
	 * 처형이 시작될 때 «적» 을 제자리로 데려오는 데 쓸 시간(초). 0 이면 한 프레임에 옮긴다.
	 *
	 * 왜 적을 옮기나: 기준은 플레이어다(2026-09-13 user 「캐릭터 위주로 잡아야지 몬스터의 회전이랑
	 *   포지션은 캐릭터 기준으로 잡아야 해」). 플레이어가 끌려가면 내가 조종하던 것이 남의 것이 된다.
	 * 왜 시간이 필요한가: 적에는 MotionWarpingComponent 가 없어서(실측 2026-09-13) 플러그인이
	 *   부드럽게 끌어줄 수가 없다. 그 몫을 우리가 나눠서 민다.
	 * 너무 길면 처형 동작이 이미 시작됐는데 몸이 아직 오고 있는 그림이 된다. 0.1~0.2 부근에서 고른다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="적을 데려오는 시간(초)"))
	float AlignBlendTime = 0.15f;

	/**
	 * 처형을 맞고도 죽지 않았을 때 대상이 일어나는 몽타주. 비면 기상 연출 없이 그냥 선다.
	 *
	 * 왜 처형마다 두나: 기상 클립이 처형 클립과 짝이다(카타나 `UE5_SSH_Getup_Seq` /
	 *   대검 `UE5_2H_Getup_Seq`). 전역에 하나 두면 대검으로 처형했는데 카타나 기상이 나온다.
	 * 비어도 되는 이유: 연출 없는 처형도 성립해야 한다 - 카메라 키와 같은 규약이다.
	 * 대상은 이 자산을 모른다. UVBGA_Execute 가 이벤트에 실어 보내고 UVBGA_GetUp 이 받는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(DisplayName="버텼을 때 일어나는 몽타주"))
	TObjectPtr<UAnimMontage> GetUpMontage = nullptr;

	/**
	 * 기상 시작 때 대상을 «추가로» 이만큼 돌린다(도). 보통 0 이다.
	 *
	 * 각도 정합은 코드가 «재서» 한다. 두 곳이 나눠 맡는다.
	 *   1. `UVBGA_Execute::HandleFinisherEnded` - 장면이 끝나는 순간 메시의 root 뼈 월드 변환을 읽어
	 *      캡슐을 몸이 그려진 자리로 데려온다(위치).
	 *   2. `UVBGA_GetUp::ExecuteAbility` - 지금 누운 방향(골반·머리·쇄골로 세운 기저)과 기상 클립의
	 *      «첫 프레임» 방향을 재서 그 차만큼 돌린 뒤 튼다(각도).
	 * 그래서 처형마다 값을 손으로 적을 필요가 없다. 2026-09-14 에 여덟 벌의 값을 실측했더니
	 *   다섯이 175도 안팎, 하나가 50도, 셋은 앞뒤가 뒤집혀 회전으로는 못 고치는 것이었다 -
	 *   숫자를 박는 대신 재게 한 이유다(처형을 하나 더 넣어도 다시 안 재도 된다).
	 * 이 값은 그러고도 눈에 거슬릴 때 쓰는 손보정이다. 0 이 아닌 값이 필요하다는 것은
	 *   재는 쪽이 틀렸다는 뜻이므로, 먼저 기상 로그의 「잰 값」을 의심해라.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(ClampMin="-180.0", ClampMax="180.0", DisplayName="기상 회전 손보정(도, 보통 0)"))
	float GetUpYawOffset = 0.0f;

	// 비면 슬로우 없음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher",
	          meta=(TitleProperty="{Time}초  x{Scale}  {RealDuration}초간"))
	TArray<FVBFinisherTimeDilationSegment> TimeDilationSegments;

	// 처형 피해 배수. 체력을 직접 만지는 값이 아니다 — 기존 피해 GameplayEffect 에
	//   SetByCaller 로 넘어간다. 피해 적용 시점의 주인은 몽타주의 기존 타격 노티파이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher", meta=(ClampMin="1.0"))
	float DamageMultiplier = 3.0f;

	// 중간에 빠져나올 수 있는가(대상이 안 죽는 처형 등).
	// 단순 플러그인 호출 여부가 아니라 어빌리티 종료 정책이다 — 켜면 이탈 시
	//   입력 차단 해제 -> 슬로우 요청 회수 -> 카메라 복귀 -> 태그 정리 순으로 풀어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Finisher")
	bool bAllowEarlyOut = false;

	// CameraAnchorRole 의 드롭다운을 채운다. Scene 이 가진 역할 이름을 그대로 돌려준다 -
	// 목록을 여기 따로 적으면 씬 자산과 두 곳이 되고, 역할 이름을 바꾼 날 한쪽만 낡는다.
	UFUNCTION()
	TArray<FString> GetSceneRoleOptions() const;

	/**
	 * 이 시각의 카메라 구도를 낸다. 앵커 기준 상대값이다.
	 *
	 * 왜 카메라 액터가 아니라 여기 있나: 저작 도구도 같은 답이 필요한데 에디터에는 그 액터가 없다.
	 *   베끼면 주인이 둘이 되어 미리보기가 런타임과 갈리고, 갈린 것을 알아챌 방법이 없다.
	 *   「이 시각의 구도는 무엇인가」는 이 데이터 자신의 질문이므로 홈이 여기다.
	 *   나중에 리플레이·관전·컷신이 같은 답을 물어도 이 한 곳을 부른다.
	 * 키가 없으면 false 를 주고 출력은 안 건드린다.
	 */
	bool EvaluateCameraAt(float SceneTime, FVector& OutRelLocation, FVector& OutRelLookAt, float& OutFOV) const;

	// 카메라 키 저작 도구(키 담기·꺼내기·미리보기 자세·재생)는 에디터 모듈의
	//   UVBFinisherCameraAuthoringSession 에 있다(2026-10-02 구조 정리 1b). 이 자산은 «무엇을 재생하나» 만 말한다.
	//   EvaluateCameraAt 은 런타임과 저작이 같이 쓰므로 여기 남는다.

#if WITH_EDITOR
	// 저작 시점에 막는다 - 런타임에 «조용히 아무 일도 안 일어나는» 처형을 만들지 않기 위해서다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
