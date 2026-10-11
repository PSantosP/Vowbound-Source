// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Data/VBReachRules.h"
#include "VBInteractableConfig.generated.h"

// 입력 방식. GDD 14장 7.1 의 세 종류.
UENUM(BlueprintType)
enum class EVBInteractMode : uint8
{
	Tap,    // 누르면 즉시 (문·스위치·아이템)
	Hold,   // HoldDuration 동안 누르고 있어야 완료 (레버·밸브)
	// 입력 없이 InteractRange 안에 들어오면 한 번(저장 포인트). 나갔다 다시 들어오면 또.
	// 판정은 거리뿐이다 - 시야·방향은 안 본다(보지 않고 지나쳐도 걸려야 자동이다). 프롬프트 초점 후보에서 빠진다.
	Auto,
	Push,   // E로 잡기/놓기, 잡은 동안 이동 입력을 대상이 소비한다.
};

class UAnimMontage;
class UAnimSequence;

// 접촉 시간은 몽타주 초, 목표는 소품 뼈/소켓 기준 손 뼈 변환이다. 이름이 비면 IK를 쓰지 않는다.
USTRUCT(BlueprintType)
struct VOWBOUND_API FVBEnvironmentHandContact
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Contact")
	FName PropBone;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Contact")
	FTransform HandTarget = FTransform::Identity;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Contact", meta=(ClampMin="0"))
	float StartTime = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Contact", meta=(ClampMin="0"))
	float EndTime = 0.0f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Contact", meta=(ClampMin="0"))
	float BlendTime = 0.15f;

	float GetWeight(float MontageTime) const;
};

/**
 * 몸으로 하는 상호작용 한 벌 - 캐릭터 몽타주, 그와 짝인 소품 애니, 캐릭터가 설 자리.
 *
 * 설 자리의 기준은 소품 액터의 루트다. 팩(Content/Interaction) 관례는 «캐릭터와 소품이 같은 원점에서 저작됐다» 이다 -
 *   2026-09-26 실측: 두 애니를 원점에 겹치면 접촉 순간 손과 소품 뿌리 뼈가 12~17cm(스위치·밸브·게이트).
 *   캐릭터는 원점에서 소품의 +Y 를 본다. 우리 캐릭터 액터의 앞(+X)은 메시의 +Y 이므로 기본값이 Yaw 90 이다.
 * 캐릭터 애니는 전부 제자리라(루트 이동 0) 설 자리까지는 GA 가 옮긴다 - 모션워핑은 루트모션이 없어 못 쓴다.
 */
USTRUCT(BlueprintType)
struct FVBInteractionMotion
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UAnimMontage> CharacterMontage;

	// 소품이 같은 시각에 재생할 애니. 비어 있으면 소품은 움직이지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	TObjectPtr<UAnimSequence> PropAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	FTransform StandTransform = FTransform(FRotator(0.0f, 90.0f, 0.0f));

	// 설 자리(StandTransform)의 기준. 꺼져 있으면 소품 액터의 변환 - 문·레버처럼 앞뒤가 있는 소품.
	//   켜면 «소품 위치 + 플레이어가 다가온 방향»: X 축 = 플레이어에서 소품으로, 원점 높이 = 플레이어 발.
	//   방향이 없는 소품(바닥의 아이템)은 이것을 켠다 - 소품 회전 기준이면 어느 쪽에서 다가가도 한쪽으로 돌아가 선다(.sw/narrative 10.3).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	bool bStandFromApproach = false;

	// 몽타주 시작 후 상태를 확정할 시각. 0은 기존 동시 재생, 발차기는 발 접촉 시각.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction", meta=(ClampMin="0"))
	float CommitTime = 0.0f;

	// 확정 순간부터 소품 애니를 재생할 위치(초). 준비 구간을 건너뛰어 접촉을 맞춘다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction", meta=(ClampMin="0"))
	float PropStartTime = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Contact")
	FVBEnvironmentHandContact LeftContact;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Contact")
	FVBEnvironmentHandContact RightContact;

	bool HasCharacterMotion() const { return CharacterMontage != nullptr; }
};

/**
 * 상호작용 대상 하나의 설정. UVBInteractableComponent 가 든다.
 *
 * 왜 자산인가: 같은 종류의 대상(문 스무 개)이 한 벌의 값을 공유해야 튜닝이 한 번에 끝난다.
 *   인스턴스마다 다른 것(문이 어디와 연결됐나)은 대상 액터 쪽 프로퍼티다.
 * 설계: .sw/interaction/design.md
 */
UCLASS(BlueprintType)
class VOWBOUND_API UVBInteractableConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	EVBInteractMode Mode = EVBInteractMode::Tap;

	// 프롬프트에 나오는 행동 이름(「열기」「당기기」). 대상이 상태에 따라 바꿀 수 있다 -
	//   UVBInteractableComponent::SetActionTextOverride.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	FText ActionText;

	// Hold 일 때 완료까지 걸리는 시간(초).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction",
		meta=(ClampMin="0.1", EditCondition="Mode==EVBInteractMode::Hold"))
	float HoldDuration = 1.5f;

	// 플레이어 몸 중심에서 대상 컴포넌트 위치까지 이 거리 안이어야 한다(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction", meta=(ClampMin="0.0"))
	float InteractRange = 200.0f;

	// 높이차 · 방향각 · 시야. 기준 방향은 카메라 방향이다 - 플레이어가 «보고 있는 것» 을 고른다.
	// 시야 캡슐은 플레이어 몸 중심에서 대상 컴포넌트 위치로 쓴다. 대상이 바닥 가까이 있으면
	//   캡슐 아래끝이 바닥에 닿으니 LineOfSightHalfHeight 를 줄이거나 시야 검사를 끈다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	FVBReachRules ReachRules;

	// 칼을 든 채 상호작용하면 수납했다가 끝나고 원래 무기로 돌아온다(user 결정 2026-09-23).
	// Hold 와 «몽타주가 있는 Tap» 에 적용된다. 몽타주 없는 Tap 은 같은 프레임에 수납·복원이 겹쳐 아무것도 안 보인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction")
	bool bSheatheWeapon = true;

	// 켜짐 상태가 꺼짐->켜짐(문 열기·스위치 켜기) / 켜짐->꺼짐 으로 바뀔 때의 몸 동작. 앞쪽.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Motion")
	FVBInteractionMotion ActivateMotion;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Motion")
	FVBInteractionMotion DeactivateMotion;

	// 뒤쪽에서 할 때(문). 캐릭터 몽타주가 비어 있으면 뒤쪽이 없는 대상이다.
	// 기본은 가까운 설 자리. 문은 고정 판정면을 별도로 저작한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Motion")
	FVBInteractionMotion ActivateMotionBack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Motion")
	FVBInteractionMotion DeactivateMotionBack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Side")
	bool bUseSidePlane = false;

	// 소품 루트 기준 평면. 법선 쪽이 Front이며 움직이는 문짝을 따라 돌지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Side", meta=(EditCondition="bUseSidePlane"))
	FVector SidePlaneOrigin = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Side", meta=(EditCondition="bUseSidePlane"))
	FVector SidePlaneNormal = FVector::YAxisVector;

	// 닫힌 문을 질주하며 밀어 여는 쪽에서만 사용. 비어 있으면 일반 열기를 유지한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Kick")
	FVBInteractionMotion KickOpenMotion;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Kick")
	bool bKickFromFront = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Kick", meta=(ClampMin="0"))
	float KickMinSpeed = 250.0f;

	// 닫기 중 E 재입력. 1은 비활성, 배속은 누적하지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Motion", meta=(ClampMin="1", ClampMax="3"))
	float FastCloseRate = 1.0f;

	// 설 자리까지 옮기는 시간(초).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Motion", meta=(ClampMin="0.0"))
	float AlignDuration = 0.25f;

	// 정렬이 막히면 접촉하지 못한 채 소품 상태를 바꾸지 않는다(cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Vowbound|Interaction|Motion", meta=(ClampMin="0"))
	float AlignTolerance = 10.0f;

	// 선택 결과를 재검색 없이 전달할 고정 순서: 앞 켜기/끄기, 뒤 켜기/끄기, 발차기 열기.
	const FVBInteractionMotion* GetMotionByIndex(uint8 Index) const;
	uint8 GetMotionIndex(const FVBInteractionMotion& Motion) const;

	// 판정면 또는 가까운 설 자리로 앞뒤를 고른다. 해당 방향에 동작이 없으면 null.
	// PropTransform = 소품 액터 루트의 월드 변환.
	const FVBInteractionMotion* SelectMotion(bool bActivating, const FTransform& PropTransform,
	                                         const FVector& InteractorLocation, bool bSprinting = false,
	                                         const FVector& Velocity = FVector::ZeroVector) const;

	// 소품 애니만 필요할 때(연결된 스위치가 문을 여는 경우처럼 사람이 없다). 앞쪽 것을 쓴다.
	const FVBInteractionMotion& GetFrontMotion(bool bActivating) const { return bActivating ? ActivateMotion : DeactivateMotion; }
};
