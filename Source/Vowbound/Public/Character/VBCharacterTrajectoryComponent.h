// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CharacterTrajectoryComponent.h"
#include "VBCharacterTrajectoryComponent.generated.h"

/**
 * UVBCharacterTrajectoryComponent
 *
 * UCharacterTrajectoryComponent 확장 — protected `Trajectory` 멤버를 C++에서 접근 가능하도록 public getter 노출.
 *
 * 의도:
 *  - 엔진의 UCharacterTrajectoryComponent는 `Trajectory` 를 protected + BlueprintReadOnly로만 노출.
 *  - VBAnimInstance가 매 tick 이 값을 복사해 ABP가 사용해야 하는데 C++ 직접 접근 불가 → 서브클래스에서 래핑.
 */
UCLASS(ClassGroup=(Vowbound), meta=(BlueprintSpawnableComponent))
class VOWBOUND_API UVBCharacterTrajectoryComponent : public UCharacterTrajectoryComponent
{
	GENERATED_BODY()

public:
	// 현재 trajectory 읽기. 서브클래스 메서드라 protected 부모 멤버 접근 허용.
	FORCEINLINE const FTransformTrajectory& GetTrajectoryData() const { return Trajectory; }
};
