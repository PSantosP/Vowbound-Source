#pragma once

#include "CoreMinimal.h"

class UAnimInstance;
struct FVBInteractionIKRuntime;

// 기존 손 IK 출력/Control Rig를 재사용한다. 처형 IK가 활성일 때는 그 소유권을 유지한다.
namespace VBEnvironmentInteractionIK
{
	VOWBOUND_API void Update(UAnimInstance* AnimInstance, FVBInteractionIKRuntime& InOutRuntime);
}
