// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/VBKeyBindingRules.h"
#include "UserSettings/EnhancedInputUserSettings.h"
#include "VBInputUserSettings.generated.h"

/**
 * 키 다시 지정의 사용자 설정(엔진 Enhanced Input 사용자 설정). 설계 .sw/quickslot-input 3절.
 *
 * 하는 일은 하나 - 줄마다 «어느 장치의 키인가» 를 정한다. 엔진 기본값은 장치를 늘 Invalid 로 둬서, 키보드 IMC 와 패드 IMC 에
 *   같은 줄 이름(입력 액션에서 물려받은 이름)이 있으면 둘 다 «첫 칸» 이 되고 나중에 등록한 IMC 가 앞의 기본 키를 덮어쓴다
 *   (`UEnhancedInputUserSettings::RegisterInputMappingContextInternal` 의 장치별 칸 세기). 엔진 주석이 이 자리를 재정의하라고 적어 두었다.
 * 장치 구분은 `VBKeyBinding::DeviceOf` 와 같다(패드 키 / 그 밖) - 엔진이 키를 적용할 때 맞추는 «기본 키 종류» 도 패드·터치·제스처로 가른다.
 * 프로젝트 설정 `UEnhancedInputDeveloperSettings::UserSettingsClass`(DefaultInput.ini)가 이 클래스를 가리킨다.
 *
 * 키 배치의 주인이 이 객체라서(설계 7절) 설정 화면이 부르는 셋도 여기 둔다 - 줄 읽기, 키 바꾸기, 장치별 기본값. 화면은 읽고 부르기만 한다.
 * 저장(AsyncSaveSettings)은 부르는 쪽이 바꾼 뒤에 한다 - 여기서 저장하면 단위 시험이 디스크에 쓴다.
 */
UCLASS()
class VOWBOUND_API UVBInputUserSettings : public UEnhancedInputUserSettings
{
	GENERATED_BODY()

public:
	virtual FHardwareDeviceIdentifier DetermineHardwareDeviceForActionMapping(const FEnhancedActionKeyMapping& ActionMapping, const UInputMappingContext* IMC = nullptr) const override;

	// 지금 키 프로필에서 이 장치의 줄(등록 순서). 이름 있는 줄만 사용자 설정에 들어오므로 이름 없는 줄(무기 슬롯·시점 축)은 없다.
	//   첫째 칸만 본다 - 한 IMC 에서 같은 이름을 두 줄이 쓰면 둘째 칸이 생기는데, 그건 이름표의 실수다(줄마다 이름을 따로 단다, 설계 3절).
	TArray<FVBKeyBindingRow> GetRows(EVBKeyDevice Device) const;

	// 그 줄에 InNewKey 를 준다. 같은 장치에서 그 키를 쓰던 다른 줄은 «키 없음» 이 된다(user 결정 10-02) - OutCleared 가 그 줄들이다.
	//   겹침 판정은 VBKeyBinding::FindConflicts 하나다(기본 배치가 일부러 나눈 키는 겹침이 아니다). 키는 NormalizeKey 로 모아 저장한다.
	//   받을 수 없는 키(VBKeyBinding::CanBind)나 없는 줄이면 아무것도 바꾸지 않고 false.
	//   바꿀 줄(겹친 줄 포함) 중 하나라도 이 장치의 칸을 못 찾아도 false 이고 아무것도 안 바꾼다 - 엔진 MapPlayerKey 는 그때 실패를 알리지 않는다.
	//   바꾼 뒤 결과가 어긋나면 바꾸기 전 키로 되돌리고 false(2차 검증 1).
	bool RebindKey(FName MappingName, EVBKeyDevice Device, const FKey& InNewKey, TArray<FName>& OutCleared);

	// 이 장치의 줄을 전부 IMC 기본 키로 되돌린다. 다른 장치의 줄은 그대로다.
	void ResetDeviceToDefault(EVBKeyDevice Device);

private:
	// 엔진은 줄을 «칸 + 장치 이름» 으로 찾는다(UEnhancedPlayerMappableKeyProfile::FindKeyMapping). 인자의 칸 기본값이 «미지정» 이라 첫째 칸을 꼭 적는다.
	static void FillArgs(FMapPlayerKeyArgs& Args, FName MappingName, EVBKeyDevice Device, const FKey& NewKey);
};

/**
 * 키 프로필. 하는 일은 하나 - 매핑을 다시 지을 때 줄의 키를 «기본 키와 같은 종류» 가 아니라 «줄의 장치» 로 고른다. 설계 10.1절.
 *
 * 엔진 기본(`UEnhancedPlayerMappableKeyProfile::GetPlayerMappedKeysForRebuildControlMappings`)은 플레이어 키가 기본 키와 종류(패드인가·터치인가)가 같아야 쓴다.
 *   «키 없음»(Invalid)은 패드 키가 아니라서 패드 줄에서 걸러지고, 고를 키가 없으면 `RebuildControlMappings` 가 기본 키를 넣는다 -
 *   화면에는 «없음» 인데 패드 기본 버튼이 그대로 먹는다. 키보드 줄은 Invalid 도 «패드 아님» 이라 통과해서 키보드로만 시험하면 안 보인다.
 * 줄의 장치는 UVBInputUserSettings 가 등록 때 단다(패드 키 = 패드, 그 밖 = 키보드·마우스).
 * 프로젝트 설정 `UEnhancedInputDeveloperSettings::DefaultPlayerMappableKeyProfileClass`(DefaultInput.ini)가 이 클래스를 가리킨다.
 */
UCLASS()
class VOWBOUND_API UVBPlayerMappableKeyProfile : public UEnhancedPlayerMappableKeyProfile
{
	GENERATED_BODY()

public:
	virtual int32 GetPlayerMappedKeysForRebuildControlMappings(const FEnhancedActionKeyMapping& DefaultMapping, TArray<FKey>& OutKeys) const override;
};
