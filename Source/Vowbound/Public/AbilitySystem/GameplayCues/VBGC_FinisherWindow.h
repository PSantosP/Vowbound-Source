// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "Data/VBHitFlashConfig.h"   // FVBHitFlashProfile - 발광 프로필을 그대로 저작한다
#include "VBGC_FinisherWindow.generated.h"

/**
 * 처형 창이 열린 동안 대상 몸을 빛나게 하는 GameplayCue.
 *
 * 무엇: OnActive 에 UVBHitFlashComponent::BeginGlow, OnRemove 에 EndGlow. 그게 전부다.
 *   발광 자체는 그 컴포넌트가 이미 한다 - 여기서 다시 짓지 않는다(VBGC_Telegraph 와 같은 구조).
 *
 * 왜 Cue 인가: 「처형할 수 있다」는 보는 모든 사람에게 보여야 한다. 능력 본문에서 부르면 서버에서만
 *   돈다. 루프 큐는 엔진이 모든 클라에 복제하므로 원격 클라와 관전·리플레이에서도 뜬다.
 *
 * 상태를 갖지 않는다(GameplayCueNotify_Static 은 CDO 로 실행된다) - 발광 상태는 대상의
 *   UVBHitFlashComponent 가 든다.
 *
 * 생성자에서 GameplayCueTag 를 설정하지 않는다(VBGC_Telegraph 와 같은 이유 - 네이티브 부모가 태그를
 *   들면 DeriveGameplayCueTagFromClass 가 레지스트리 키를 None 으로 만든다).
 *   태그는 BP 자식(GCN_FinisherWindow)의 클래스 기본값에서 준다.
 */
UCLASS()
class VOWBOUND_API UVBGC_FinisherWindow : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnActive_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
	virtual bool OnRemove_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;

protected:
	/**
	 * 발광 프로필. 저작 지점 = GCN_FinisherWindow 클래스 기본값.
	 *
	 * 색을 코드에 박지 않는 이유: 「약속의 색」은 연출 자산이 드는 값이다. 빨강이 아니게 되는 날
	 *   코드를 고치면 안 된다.
	 * 바깥선을 따라 빛나게 하는 것은 FresnelStrength / FresnelExponent 다.
	 *   그 둘을 머티리얼이 실제로 읽는지 확인하고 값을 넣어라 - 안 읽으면 몸 전체가 균일하게 빛난다.
	 * Duration 은 「한 번 걸었을 때 살아 있는 길이」다. 창은 그보다 길 수 있으므로 넉넉히 둔다 -
	 *   끄는 것은 시간이 아니라 OnRemove 다.
	 */
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Finisher")
	FVBHitFlashProfile GlowProfile;

	/**
	 * 외곽선을 그리기 위해 대상 메시에 찍는 스텐실 번호. 0 이면 안 찍는다.
	 *
	 * 무엇에 쓰나: 포스트프로세스 머티리얼이 화면의 스텐실 값을 이웃 화소와 비교해 «값이 바뀌는 자리»
	 *   를 찾는다. 그 자리가 곧 실루엣이고, 거기에 선을 그린다.
	 * 왜 데이터인가: 나중에 다른 표시(상호작용·퀘스트 대상)가 생기면 번호가 겹치면 안 된다.
	 *   그 조정은 연출의 일이지 코드의 일이 아니다.
	 * 켜려면 프로젝트 설정의 Custom Depth-Stencil Pass 가 EnabledWithStencil 이어야 한다
	 *   (Config/DefaultEngine.ini 의 r.CustomDepth=3).
	 */
	UPROPERTY(EditDefaultsOnly, Category="Vowbound|Finisher",
	          meta=(ClampMin="0", ClampMax="255"))
	int32 OutlineStencilValue = 1;

private:
	// 대상의 모든 스켈레탈 메시에 커스텀 뎁스·스텐실을 켜거나 끈다.
	void SetOutline(AActor* Target, bool bEnable) const;
};
