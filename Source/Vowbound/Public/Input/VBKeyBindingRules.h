// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

/** 키 줄이 어느 장치의 것인가. 키보드·마우스 IMC 와 패드 IMC 가 따로라 줄도 장치를 하나씩 갖는다. */
enum class EVBKeyDevice : uint8
{
	KeyboardMouse,
	Gamepad,
};

/** 키 다시 지정 화면이 보는 한 줄 - 사용자 설정의 줄 이름, 장치, 지금 키, IMC 기본 키, 화면에 적을 이름과 분류(입력 액션의 다시 지정 설정에서 온다). */
struct FVBKeyBindingRow
{
	FName MappingName;
	EVBKeyDevice Device = EVBKeyDevice::KeyboardMouse;
	FKey Key;
	FKey DefaultKey;
	FText DisplayName;
	FText DisplayCategory;
};

/**
 * 키 다시 지정의 판정. 설계 .sw/quickslot-input 3·10절.
 *
 * 엔진 UEnhancedInputUserSettings::MapPlayerKey 는 겹치는 키를 막지 않는다(5.8 소스 - 실패 사유는 매핑 이름 잘못·키 프로필 없음 둘뿐).
 *   그래서 «받을 수 있는 키인가» 와 «누구와 겹치나» 는 여기 한 곳이 답한다. 설정 화면이 묻고 단위 시험이 덮는다 - 사용자 설정·월드 없이 부를 수 있다.
 */
namespace VBKeyBinding
{
	// 키가 속한 장치. 패드 키면 패드, 그 밖(키보드·마우스 버튼·휠)은 키보드·마우스.
	VOWBOUND_API EVBKeyDevice DeviceOf(const FKey& Key);

	// 이 장치의 줄이 받을 수 있는 키인가. 못 받는 것: 빈 키, 다른 장치의 키, 터치,
	//   2D 축(마우스 이동·스틱)과 그 한 축(마우스 X·Y, 스틱 X·Y) - 시점·이동 줄은 다시 지정하지 않는다,
	//   Esc - 메뉴 «뒤로» 라 게임 줄에 주면 메뉴로 돌아올 길이 엉킨다.
	//   패드 트리거 축은 받는다(지금 정화·처형이 트리거 축이다).
	VOWBOUND_API bool CanBind(const FKey& Key, EVBKeyDevice RowDevice);

	// 같은 물리 버튼을 한 키로 모은다. 패드 트리거는 키가 둘이다 - 버튼(Gamepad_LeftTrigger, 문턱을 넘을 때 눌림)과 축(Gamepad_LeftTriggerAxis).
	//   우리 IMC 는 축을 쓰고 키 받기에는 버튼이 들어온다. 그대로 두면 같은 트리거가 다른 키로 보여 겹침을 못 잡는다 - 그래서 축으로 모은다.
	VOWBOUND_API FKey NormalizeKey(const FKey& Key);

	// NewKey 를 Editing 줄(같은 장치)에 주면 겹치는 다른 줄 이름들. 같은 장치의 줄만 본다(키보드의 Space 와 패드의 A 는 겹치지 않는다).
	//   자기 줄은 빼고, 순서는 Rows 순서. 같은 이름이 둘 이상이면 한 번만 넣는다. 키는 NormalizeKey 로 모아 견준다.
	//   기본 배치가 일부러 나눠 쓰는 키는 겹침이 아니다: 고치는 줄의 기본 키가 NewKey 이고 상대 줄의 기본 키도 NewKey 면 뺀다
	//   (패드 L3 = 웅크리기 누름·달리기 길게, Tab = 소환 짧게·바퀴 길게처럼 트리거로 갈린다). 그 키를 다른 줄이 새로 쓰려 하면 그때는 겹침이다.
	VOWBOUND_API TArray<FName> FindConflicts(const TArray<FVBKeyBindingRow>& Rows, FName Editing, EVBKeyDevice Device, const FKey& NewKey);

	// 화면에 놓을 순서로 줄을 다시 세운다: 분류가 처음 나온 순서대로 묶고, 묶음 안은 원래 순서(등록 순서 = IMC 순서)를 지킨다.
	//   분류는 표시 글자로 견준다 - 분류에는 표시 글자 말고 열쇠가 없다.
	VOWBOUND_API void OrderByCategory(TArray<FVBKeyBindingRow>& Rows);
}
