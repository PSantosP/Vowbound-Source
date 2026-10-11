// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * 처형 설정 자산(UVBFinisherConfig)의 디테일 패널에 카메라 저작 칸과 버튼을 붙인다.
 *
 * 상태를 갖지 않는다. 디테일 패널은 다시 그릴 때마다 이 객체를 버리고 새로 만든다
 *   (엔진 SDetailsViewBase::UpdatePropertyMaps) - 그래서 재생·꺼낸 카메라 같은 상태는
 *   UVBFinisherCameraAuthoringSession 이 쥐고, 여기서는 그 세션을 찾아 칸을 끼우고 버튼을 단다.
 * 버튼은 세션 함수를 부르기만 한다. 판단을 여기 두면 패널이 바뀔 때마다 판단이 둘이 된다.
 */
class FVBFinisherCameraAuthoringCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
