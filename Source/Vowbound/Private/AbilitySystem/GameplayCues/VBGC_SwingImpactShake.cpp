// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/GameplayCues/VBGC_SwingImpactShake.h"

#include "Camera/CameraShakeBase.h"      // TSubclassOf<UCameraShakeBase> 완전 타입
#include "Camera/CameraTypes.h"          // ECameraShakePlaySpace
#include "Camera/PlayerCameraManager.h"  // StartCameraShake
#include "Data/VBSwingImpactProfile.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Vowbound/Vowbound.h"

// 생성자에서 GameplayCueTag 를 세우지 않는다. 태그는 GCN_SwingImpactShake BP 의 Class Defaults 가 소유한다.
//
// 왜: 네이티브 부모가 태그를 들면 BP 자식 저장 시 UAbilitySystemGlobals::DeriveGameplayCueTagFromClass 가
// "부모와 태그가 같다"를 감지해 자산명에서 태그를 유도하려 하고, 유도에 실패하면
// GameplayCueName 을 갱신하지 않은 채 return 한다. GameplayCueName 은 에셋 레지스트리 검색 키라
// 비어 있으면 BuildCuesToAddToGlobalSet 이 그 BP 를 건너뛰고, 큐는 아무 에러 없이 죽는다(FIND-077).
//
// 애초에 네이티브 태그만으로는 등록 자체가 안 된다: InitObjectLibrary 는 BP 에셋만 스캔하므로
// (GameplayCueManager.cpp::UGameplayCueManager::LoadBlueprintAssetDataFromPaths) 네이티브 GCN 은 큐 세트에 들어갈 경로가 없다. BP 자식이 필수 부품이다.
UVBGC_SwingImpactShake::UVBGC_SwingImpactShake()
{
}

bool UVBGC_SwingImpactShake::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const
{
	// 큐는 멀티캐스트라 모든 머신에서 이 함수가 돈다. 흔들려야 할 카메라는 휘두른 본인 것 하나뿐이다.
	// 이 검사가 없으면 남의 스윙에 내 화면이 흔들린다 — 엔진 PlayWorldCameraShake 가 전 PlayerController 를
	//  순회하는 것이 정확히 그 실패이고, 이 큐가 그것을 대체하는 이유다.
	// 리슨서버의 이중 발동(서버 실행 + 로컬 클라 실행)도 여기서 함께 막힌다.
	if (!Parameters.IsInstigatorLocallyControlledPlayer(MyTarget))
	{
		return false;
	}

	// 쉐이크 자산과 세기의 데이터 홈. GA 가 SourceObject 로 통째로 실어 보냈다.
	const UVBSwingImpactProfile* Profile = Cast<UVBSwingImpactProfile>(Parameters.SourceObject.Get());
	if (!Profile || !Profile->ShakeClass)
	{
		// Warning 이 아니라 Verbose 인 이유: 여기까지 왔다는 것은 프로필 배선 자체는 살아 있다는 뜻이고
		//  (미배선은 GA 가 Warning 으로 이미 잡는다), 쉐이크 자산만 비운 프로필은 "이 스윙은 안 흔든다"는
		//  유효한 저작 선택일 수 있다. 매 타격 경고를 찍으면 전투 로그가 이것 하나로 가득 찬다.
		VB_LOG(Verbose, "VBGC_SwingImpactShake: 프로필/ShakeClass 없음 - 쉐이크 스킵");
		return false;
	}

	// 로컬 게이트를 통과한 시점에서 '나'는 instigator 본인이다. 그러므로 흔들 카메라는
	//  instigator 폰이 들고 있는 컨트롤러의 것이어야 한다.
	// GetPlayerController(0) 를 쓰지 않는 이유: 스플릿스크린이나 분리 클라 환경에서 0번은 '나'라는 보장이 없고,
	//  그 순간 엉뚱한 화면이 흔들린다. 판정 근거와 적용 대상이 같은 객체에서 나와야 어긋날 자리가 없다.
	APawn* InstigatorPawn = Cast<APawn>(Parameters.GetInstigator());
	APlayerController* PC = InstigatorPawn ? Cast<APlayerController>(InstigatorPawn->GetController()) : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return false;
	}

	// UserDefined 가 이 기능의 전부다. 엔진이 UserPlaySpaceRot 로 회전행렬을 만들고(CameraShakeBase.cpp)
	//  패턴의 로컬 오프셋을 그 행렬로 돌려 월드에 얹는다(CameraAnimationHelper.cpp).
	// Normal.Rotation() 은 X 축이 스윙 방향을 향하는 회전이다 — 쉐이크 자산이 X 로만 진동하면
	//  카메라는 정확히 그 방향으로 밀린다. CameraLocal 이면 화면 기준이라 캐릭터가 돌아도 밀림이 안 돈다.
	// 밀림 거리/지속/주파수/블렌드/위상은 전부 쉐이크 자산의 값이다 — 여기에 수치가 없는 것이 의도다.
	PC->PlayerCameraManager->StartCameraShake(
		Profile->ShakeClass,
		Profile->ShakeScale,
		ECameraShakePlaySpace::UserDefined,
		Parameters.Normal.Rotation());

	return true;
}
