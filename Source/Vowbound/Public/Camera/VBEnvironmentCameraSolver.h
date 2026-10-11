#pragma once

#include "Camera/CameraTypes.h"
#include "Camera/VBEnvironmentCameraPolicy.h"

class ACharacter;
class UVBEnvironmentCameraConfig;

// 환경 카메라의 위치 풀이(후보 고르기·충돌·회복). 런타임에서는 AVBPlayerCameraManager 가 멤버로 들고
//   DoUpdateCamera 에서 돌린다. UVBEnvironmentCameraComponent 는 어느 설정을 쓸지(Config)만 내준다.
// 왜 export 하나: 이 풀이의 시험이 VowboundTests 모듈에 있다(2026-10-02, 구조 정리 1a - 시험은 시험 모듈에 모은다).
class VOWBOUND_API FVBEnvironmentCameraSolver final
{
public:
	void Reset();
	void Update(ACharacter* Pawn, const UVBEnvironmentCameraConfig* Settings, float DeltaTime, bool bCameraCut, FMinimalViewInfo& Pose);
private:
	VBEnvironmentCameraPolicy::FRecovery Recovery;
	FVector LastAnchor = FVector::ZeroVector;
	FVector LastPosition = FVector::ZeroVector;
	FVector LocalAdjustment = FVector::ZeroVector;
	int32 LastCandidate = INDEX_NONE;
	bool bHasHistory = false;
	bool bWasUnresolved = false;
};
