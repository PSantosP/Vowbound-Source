// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "VBSettingsWidget.generated.h"

/**
 * 설정 패널 위젯.
 * 이유: 글로벌 설정(FVBSettingsSaveData)을 UI 와 양방향 연결한다.
 *       조회도 갱신도 UVBSaveGameSubsystem 에 위임한다 - 이 위젯은 설정의 홈이 아니라 창구다.
 *       감도/Y반전은 저장 즉시 적용된다(소비처가 홈을 직접 읽는다).
 *       볼륨/언어는 저장만 되고 적용되지 않으므로 WBP 에서 컨트롤을 숨긴다 - 근거는 FVBSettingsSaveData 주석.
 */
UCLASS()
class VOWBOUND_API UVBSettingsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 현재 저장된 설정을 읽어온다(UI 초기값 채우기용). 세이브 없으면 기본값.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	FVBSettingsSaveData GetCurrentSettings() const;

	// UI 값으로 설정 홈을 갱신하고 디스크에 비동기 저장. 이름대로 갱신이 곧 적용이다 -
	// 소비처가 홈을 직접 읽으므로 이 호출 다음 입력부터 새 감도가 먹는다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ApplyAndSave(const FVBSettingsSaveData& NewSettings);

	// 저장된 값을 위젯에 채운다. 화면 진입(Construct)에서 부른다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void SyncWidgetsFromSettings();

	// 위젯의 현재 값을 읽어 적용·저장한다. 적용 버튼에서 부른다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ApplyFromWidgets();

	// 노출된 두 항목만 덮어써서 저장한다. 나머지 네 항목(볼륨 3종/언어)은 읽은 값 그대로 보존된다.
	// 왜 이 함수가 필요한가: BP 에서 구조체를 새로 만들어 넘기면 화면에 없는 네 항목이 기본값으로
	//   초기화돼 사용자 설정이 조용히 날아간다. 그 보존 규칙의 홈은 그래프 배선이 아니라 여기다 -
	//   배선은 눈으로만 검증되지만 여기 있으면 컴파일러와 /verify 가 본다.
	UFUNCTION(BlueprintCallable, Category="Vowbound|UI")
	void ApplyLookSettings(float InLookSensitivity, bool bInInvertYAxis);

protected:
	// UMG 이름 바인딩. WBP 의 위젯 이름이 이 필드명과 같으면 엔진이 자동으로 물려 준다.
	// Optional 인 이유: 바인딩이 끊겨도 컴파일은 통과시키고 런타임에 로그로 알린다 - 필수(BindWidget)로
	//   두면 이름을 바꾸는 순간 WBP 가 컴파일 불가가 되어 편집 중 화면을 못 연다.
	// 노출 키워드가 없으므로 Category 를 붙이지 않는다(붙이면 UHT 경고 -> 빌드 실패).
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class USlider> SliderSens;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<class UCheckBox> ChkInvertY;
};
