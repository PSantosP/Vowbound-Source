// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PoseSearch/PoseSearchConstraints.h"   // FPoseSearchConstraint
#include "PoseSearch/PoseSearchLibrary.h"       // FPoseSearchBlueprintResult
#include "VBInteractionIK.generated.h"

class UAnimInstance;
class UAnimMontage;
class UPoseSearchDatabase;

/**
 * 상호작용 중 한쪽 말단이 상대의 어디를 잡아야 하는가. AnimGraph 의 IK 노드가 읽는다.
 *
 * 왜 필요한가: 두 클립은 각자 자기 골격으로 만들어졌고 우리는 그 둘이 골격까지 다르다
 *   (플레이어 UEFN / 적 SK_Mannequin, 상체 높이 차 ~10cm — 실측 2026-09-19).
 *   모션워핑이 «몸통 위치» 를 맞추고 남는 것이 손끝이다. 그 남는 양을 이 목표가 닫는다.
 */
USTRUCT(BlueprintType)
struct FVBInteractionIKHand
{
	GENERATED_BODY()

	// 잡아야 할 자리. 컴포넌트 공간이다 - AnimGraph 의 IK 노드가 그 공간에서 일한다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Finisher")
	FTransform Target = FTransform::Identity;

	// 얼마나 잡고 있나(0~1). 엔진이 노티파이의 RampUpTime/CoolDownTime 으로 만들어 준다 -
	//   우리가 보간하지 않는다. 그러면 켜고 끄는 주인이 둘이 된다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Finisher")
	float Alpha = 0.0f;
};

/**
 * 상호작용 손 IK 의 런타임 상태 한 덩어리. AnimInstance 가 이것 하나만 멤버로 든다.
 *
 * 왜 묶었나: 플레이어와 적 AnimInstance 가 형제 클래스라 같은 필드 넷을 양쪽에 적어야 한다.
 *   묶어 두면 한쪽에만 필드가 늘어나는 일이 안 생긴다(규칙 12 는 3회부터지만 여기는 비용이 0 이다).
 */
USTRUCT(BlueprintType)
struct FVBInteractionIKRuntime
{
	GENERATED_BODY()

	// 지금 IK 를 걸 상황인가. 상호작용 몽타주가 실제로 돌고 있을 때만 참이다.
	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Finisher")
	bool bEnabled = false;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Finisher")
	FVBInteractionIKHand Left;

	UPROPERTY(BlueprintReadOnly, Category="Vowbound|Finisher")
	FVBInteractionIKHand Right;

	// 어느 소켓을 IK 대상으로 보나. 클립의 제약 노티파이가 적은 이름과 같아야 한다.
	//   데이터로 빼 둔 이유: 손이 아닌 곳을 잡는 처형(멱살·머리)이 오면 코드를 안 고치고 바꾼다.
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Finisher")
	FName LeftSocket = FName("hand_l");

	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Finisher")
	FName RightSocket = FName("hand_r");

	// 엔진이 채우고 지우는 제약 목록. 프레임을 넘겨 살아 있어야 한다(램프업이 시간에 걸쳐 오른다).
	UPROPERTY(Transient)
	TArray<FPoseSearchConstraint> Constraints;

	// 표시 인스턴스별 이력. 서버/원격 모두 실제 복제된 밀기 섹션 변화에 블렌드를 요청한다.
	TWeakObjectPtr<UAnimMontage> PreviousPushMontage;
	FName PreviousPushSection;
};

/**
 * 상호작용 손 IK 갱신. 플레이어·적 AnimInstance 가 둘 다 매 프레임 부른다.
 *
 * 원본: GASP 5.8 `SandboxCharacter_Mover_ABP::Update_InteractionConstraints`(45노드, 실측 2026-09-19).
 *   그쪽 주석이 이렇게 적고 있다 — 「제약은 상대 캐릭터의 뼈에 대한 IK 목표일 뿐이고,
 *   상호작용 애니의 노티파이 스테이트로 저작된다」.
 * 우리가 다르게 한 것 둘:
 *   1. BP 인터페이스(`BPI_SandboxCharacter_Pawn::Get_MMIResult`)를 안 거친다. 결과는 이미
 *      AnimInstance 가 들고 있다(IVBInteractionAnimInterface).
 *   2. 소켓 이름을 상수로 안 적고 런타임 구조체의 필드로 둔다.
 */
UCLASS()
class VOWBOUND_API UVBInteractionIKLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 제약을 갱신하고 두 손의 IK 목표를 낸다. 상호작용 중이 아니면 전부 꺼진 상태로 돌려준다.
	 *
	 * 게임 스레드에서만 부른다 — 몽타주 상태(`Montage_IsActive`/`Montage_GetPosition`)를 본다.
	 * 부작용: `InOutRuntime.Constraints` 를 엔진이 제자리에서 고친다(램프업·쿨다운 누적).
	 */
	static void Update(UAnimInstance* AnimInstance,
	                   const FPoseSearchBlueprintResult& Result,
	                   float DeltaSeconds,
	                   FVBInteractionIKRuntime& InOutRuntime);

	/**
	 * 이 AnimInstance 의 골격이 데이터베이스 스키마가 그 역할에 대해 선언한 골격과 같은가.
	 *
	 * 이것이 없으면 엔진이 크래시한다(2026-09-19 실측, user PIE). `UpdateConstraints` 는 소켓 «이름» 이
	 *   없으면 막아 주지만(INDEX_NONE 검사) 이름이 있고 인덱스가 다른 골격의 것일 때는 안 막는다.
	 *   그 인덱스를 상대의 FCompactPose 에 그대로 넣어 `Pose.IsValidIndex(BoneIndex)` 로 터진다
	 *   (`PoseSearchInteractionLibrary.cpp:878`). 즉 엔진은 짝의 골격이 스키마와 같다고 «믿는다».
	 * 그래서 짝을 짓기 전에도(UVBFinisherComponent) 짓고 나서도(아래 Update) 이것을 묻는다.
	 * 골격을 못 알아내면 false 를 돌려준다 - 모르는 경우는 막는 쪽으로 떨어뜨린다(규칙 27).
	 */
	static bool DoesSkeletonMatchSchema(const UPoseSearchDatabase* Database,
	                                    FName Role,
	                                    const UAnimInstance* AnimInstance);
};
