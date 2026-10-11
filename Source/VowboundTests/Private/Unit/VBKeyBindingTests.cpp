#include "Misc/AutomationTest.h"
#include "Input/VBInputUserSettings.h"
#include "Input/VBKeyBindingRules.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/Package.h"

// 키 다시 지정 판정의 단위 시험. 설계 .sw/quickslot-input 3·10·10.1절.
// 엔진 MapPlayerKey 는 겹침을 막지 않으므로 «받을 수 있는 키» 와 «겹치는 줄» 을 여기서 덮는다.
// 사용자 설정 시험은 실제 UVBInputUserSettings 에 시험용 IMC 둘(키보드·패드)을 등록해 본다 - 로컬 플레이어·디스크 없이 돈다(저장은 부르지 않는다).
// 장치 구분과 패드 «키 없음» 은 엔진 기본 클래스를 대조군으로 같이 돌린다: 대조군이 틀리지 않으면 우리 재정의가 막은 것이 아니다.
// 화면(키 받기·확인 칸)은 PIE 가 본다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyBindingCanBindTest, "Vowbound.Unit.KeyBinding.CanBind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyBindingCanBindTest::RunTest(const FString& Parameters)
{
	using namespace VBKeyBinding;
	TestTrue(TEXT("pad key is a pad key"), DeviceOf(EKeys::Gamepad_FaceButton_Bottom) == EVBKeyDevice::Gamepad);
	TestTrue(TEXT("mouse button is keyboard-mouse"), DeviceOf(EKeys::LeftMouseButton) == EVBKeyDevice::KeyboardMouse);

	// 받는 것
	TestTrue(TEXT("keyboard row takes a letter"), CanBind(EKeys::V, EVBKeyDevice::KeyboardMouse));
	TestTrue(TEXT("keyboard row takes a mouse button"), CanBind(EKeys::MiddleMouseButton, EVBKeyDevice::KeyboardMouse));
	TestTrue(TEXT("keyboard row takes the wheel (target switch uses it)"), CanBind(EKeys::MouseScrollUp, EVBKeyDevice::KeyboardMouse));
	TestTrue(TEXT("pad row takes a face button"), CanBind(EKeys::Gamepad_FaceButton_Right, EVBKeyDevice::Gamepad));
	TestTrue(TEXT("pad row takes a trigger axis (purify, execute)"), CanBind(EKeys::Gamepad_LeftTriggerAxis, EVBKeyDevice::Gamepad));

	// 못 받는 것
	TestFalse(TEXT("empty key"), CanBind(FKey(), EVBKeyDevice::KeyboardMouse));
	TestFalse(TEXT("pad key on a keyboard row"), CanBind(EKeys::Gamepad_FaceButton_Bottom, EVBKeyDevice::KeyboardMouse));
	TestFalse(TEXT("keyboard key on a pad row"), CanBind(EKeys::SpaceBar, EVBKeyDevice::Gamepad));
	TestFalse(TEXT("Escape is menu back"), CanBind(EKeys::Escape, EVBKeyDevice::KeyboardMouse));
	TestFalse(TEXT("mouse 2D axis is look"), CanBind(EKeys::Mouse2D, EVBKeyDevice::KeyboardMouse));
	TestFalse(TEXT("mouse X is look"), CanBind(EKeys::MouseX, EVBKeyDevice::KeyboardMouse));
	TestFalse(TEXT("stick 2D axis is move"), CanBind(EKeys::Gamepad_Left2D, EVBKeyDevice::Gamepad));
	TestFalse(TEXT("stick X is move"), CanBind(EKeys::Gamepad_RightX, EVBKeyDevice::Gamepad));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyBindingConflictTest, "Vowbound.Unit.KeyBinding.Conflicts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyBindingConflictTest::RunTest(const FString& Parameters)
{
	using namespace VBKeyBinding;
	const FName Jump(TEXT("Vowbound.Move.Jump"));
	const FName Dodge(TEXT("Vowbound.Combat.Dodge"));
	const FName Interact(TEXT("Vowbound.World.Interact"));
	const TArray<FVBKeyBindingRow> Rows = {
		{Jump, EVBKeyDevice::KeyboardMouse, EKeys::SpaceBar},
		{Dodge, EVBKeyDevice::KeyboardMouse, EKeys::V},
		{Interact, EVBKeyDevice::KeyboardMouse, EKeys::E},
		{Jump, EVBKeyDevice::Gamepad, EKeys::Gamepad_FaceButton_Bottom},
		{Dodge, EVBKeyDevice::Gamepad, EKeys::Gamepad_FaceButton_Right},
	};

	TestEqual(TEXT("free key = no conflict"), FindConflicts(Rows, Dodge, EVBKeyDevice::KeyboardMouse, EKeys::G).Num(), 0);
	TestEqual(TEXT("own key = no conflict"), FindConflicts(Rows, Jump, EVBKeyDevice::KeyboardMouse, EKeys::SpaceBar).Num(), 0);

	const TArray<FName> Taken = FindConflicts(Rows, Dodge, EVBKeyDevice::KeyboardMouse, EKeys::SpaceBar);
	TestEqual(TEXT("taken key = one conflict"), Taken.Num(), 1);
	TestTrue(TEXT("the conflict is the jump row"), Taken.Num() == 1 && Taken[0] == Jump);

	// 장치가 다르면 겹치지 않는다: 패드 줄을 바꾸는데 키보드 줄에 같은 이름이 있어도, 패드 키끼리만 본다.
	const TArray<FName> PadTaken = FindConflicts(Rows, Dodge, EVBKeyDevice::Gamepad, EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("pad conflict is the pad jump row"), PadTaken.Num() == 1 && PadTaken[0] == Jump);
	TestEqual(TEXT("keyboard key never conflicts with pad rows"), FindConflicts(Rows, Jump, EVBKeyDevice::Gamepad, EKeys::V).Num(), 0);

	// 같은 키를 쓰는 줄이 둘이면(저작 실수 또는 앞서 겹친 채 둔 것) 둘 다 알린다.
	TArray<FVBKeyBindingRow> Twice = Rows;
	Twice.Add({Interact, EVBKeyDevice::KeyboardMouse, EKeys::V});
	TestEqual(TEXT("two rows on the key = two conflicts"), FindConflicts(Twice, Jump, EVBKeyDevice::KeyboardMouse, EKeys::V).Num(), 2);
	TestEqual(TEXT("empty key = no conflict"), FindConflicts(Rows, Jump, EVBKeyDevice::KeyboardMouse, FKey()).Num(), 0);

	// 기본 배치가 일부러 나눈 키: 패드 웅크리기(누름)·달리기(길게)가 L3 를 같이 쓴다. 사용자가 웅크리기를 옮겼다가 L3 로 되돌리는 것은 겹침이 아니다.
	const FName Crouch(TEXT("Vowbound.Move.Crouch"));
	const FName Sprint(TEXT("Vowbound.Move.Sprint"));
	const FKey L3 = EKeys::Gamepad_LeftThumbstick;
	const TArray<FVBKeyBindingRow> Shared = {
		{Crouch, EVBKeyDevice::Gamepad, EKeys::Gamepad_DPad_Up, L3},
		{Sprint, EVBKeyDevice::Gamepad, L3, L3},
		{Interact, EVBKeyDevice::Gamepad, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down},
	};
	TestEqual(TEXT("back to a shared default = no conflict"), FindConflicts(Shared, Crouch, EVBKeyDevice::Gamepad, L3).Num(), 0);
	const TArray<FName> Taken3 = FindConflicts(Shared, Interact, EVBKeyDevice::Gamepad, L3);
	TestTrue(TEXT("another row taking the shared key = conflict"), Taken3.Num() == 1 && Taken3[0] == Sprint);

	// 패드 트리거: 키 받기는 버튼(LeftTrigger), IMC 는 축(LeftTriggerAxis) - 같은 버튼으로 본다.
	TestTrue(TEXT("trigger button -> trigger axis"), NormalizeKey(EKeys::Gamepad_RightTrigger) == EKeys::Gamepad_RightTriggerAxis);
	TestTrue(TEXT("other keys unchanged"), NormalizeKey(EKeys::V) == EKeys::V);
	const FName Purify(TEXT("Vowbound.Combat.Purify"));
	const FName Execute(TEXT("Vowbound.Combat.Execute"));
	const TArray<FVBKeyBindingRow> Triggers = {
		{Purify, EVBKeyDevice::Gamepad, EKeys::Gamepad_LeftTriggerAxis, EKeys::Gamepad_LeftTriggerAxis},
		{Execute, EVBKeyDevice::Gamepad, EKeys::Gamepad_RightTriggerAxis, EKeys::Gamepad_RightTriggerAxis},
	};
	const TArray<FName> TriggerTaken = FindConflicts(Triggers, Purify, EVBKeyDevice::Gamepad, EKeys::Gamepad_RightTrigger);
	TestTrue(TEXT("trigger button conflicts with the axis row"), TriggerTaken.Num() == 1 && TriggerTaken[0] == Execute);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyBindingOrderTest, "Vowbound.Unit.KeyBinding.OrderByCategory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyBindingOrderTest::RunTest(const FString& Parameters)
{
	auto MakeRow = [](const TCHAR* Name, const TCHAR* Category)
	{
		FVBKeyBindingRow Row;
		Row.MappingName = FName(Name);
		Row.DisplayCategory = FText::FromString(Category);
		return Row;
	};
	TArray<FVBKeyBindingRow> Rows = {
		MakeRow(TEXT("A"), TEXT("Move")), MakeRow(TEXT("B"), TEXT("Combat")), MakeRow(TEXT("C"), TEXT("Move")), MakeRow(TEXT("D"), TEXT("Combat")),
	};
	VBKeyBinding::OrderByCategory(Rows);
	const TArray<FName> Order = {Rows[0].MappingName, Rows[1].MappingName, Rows[2].MappingName, Rows[3].MappingName};
	TestTrue(TEXT("first-seen category first, original order kept inside"),
		Order == TArray<FName>({FName(TEXT("A")), FName(TEXT("C")), FName(TEXT("B")), FName(TEXT("D"))}));
	return true;
}

namespace VBKeyBindingTestUtil
{
	// 이름 있는 입력 액션. 입력 액션의 다시 지정 설정은 보호 칸이라(UInputAction::PlayerMappableKeySettings) 시험에서만 리플렉션으로 채운다.
	UInputAction* MakeAction(const TCHAR* MappingName, const TCHAR* DisplayName)
	{
		UInputAction* Action = NewObject<UInputAction>(GetTransientPackage());
		UPlayerMappableKeySettings* KeySettings = NewObject<UPlayerMappableKeySettings>(Action);
		KeySettings->Name = FName(MappingName);
		KeySettings->DisplayName = FText::FromString(DisplayName);
		FObjectProperty* Property = FindFProperty<FObjectProperty>(UInputAction::StaticClass(), TEXT("PlayerMappableKeySettings"));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Action, KeySettings);
		return Action;
	}

	// 점프(Space / A)·회피(V / B) 두 줄을 키보드 IMC 와 패드 IMC 에 나눠 두고 등록한 사용자 설정.
	struct FFixture
	{
		UInputMappingContext* KeyboardIMC = nullptr;
		UInputMappingContext* GamepadIMC = nullptr;
		UEnhancedInputUserSettings* Settings = nullptr;

		FFixture(TSubclassOf<UEnhancedInputUserSettings> SettingsClass, TSubclassOf<UEnhancedPlayerMappableKeyProfile> ProfileClass)
		{
			UInputAction* Jump = MakeAction(TEXT("Test.Jump"), TEXT("Jump"));
			UInputAction* Dodge = MakeAction(TEXT("Test.Dodge"), TEXT("Dodge"));
			KeyboardIMC = NewObject<UInputMappingContext>(GetTransientPackage());
			KeyboardIMC->MapKey(Jump, EKeys::SpaceBar);
			KeyboardIMC->MapKey(Dodge, EKeys::V);
			GamepadIMC = NewObject<UInputMappingContext>(GetTransientPackage());
			GamepadIMC->MapKey(Jump, EKeys::Gamepad_FaceButton_Bottom);
			GamepadIMC->MapKey(Dodge, EKeys::Gamepad_FaceButton_Right);

			Settings = NewObject<UEnhancedInputUserSettings>(GetTransientPackage(), SettingsClass);
			FPlayerMappableKeyProfileCreationArgs Args;
			Args.ProfileType = ProfileClass;
			Args.ProfileStringIdentifier = TEXT("VBTest");
			Args.bSetAsCurrentProfile = true;
			Settings->CreateNewKeyProfile(Args);
			Settings->RegisterInputMappingContext(KeyboardIMC);
			Settings->RegisterInputMappingContext(GamepadIMC);
		}

		UVBInputUserSettings* VB() const { return Cast<UVBInputUserSettings>(Settings); }

		// 다시 지을 때 이 IMC 의 이 줄에 실제로 들어갈 키들. 비면 엔진이 기본 키를 넣는다(RebuildControlMappings).
		TArray<FKey> RebuildKeys(const UInputMappingContext* IMC, int32 MappingIndex) const
		{
			TArray<FKey> Keys;
			Settings->GetActiveKeyProfile()->GetPlayerMappedKeysForRebuildControlMappings(IMC->GetMappings()[MappingIndex], Keys);
			return Keys;
		}
	};

	FKey KeyOf(const TArray<FVBKeyBindingRow>& Rows, const TCHAR* MappingName)
	{
		const FVBKeyBindingRow* Row = Rows.FindByPredicate([MappingName](const FVBKeyBindingRow& Candidate) { return Candidate.MappingName == FName(MappingName); });
		return Row ? Row->Key : FKey(TEXT("RowMissing"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyBindingDeviceSplitTest, "Vowbound.Unit.KeyBinding.DeviceSplit",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyBindingDeviceSplitTest::RunTest(const FString& Parameters)
{
	using namespace VBKeyBindingTestUtil;
	const FName Jump(TEXT("Test.Jump"));

	// 대조군: 엔진 기본 설정 클래스는 장치를 늘 Invalid 로 둬서, 패드 IMC 의 점프가 키보드 점프의 칸을 덮는다 - 줄 하나.
	const FFixture Engine(UEnhancedInputUserSettings::StaticClass(), UEnhancedPlayerMappableKeyProfile::StaticClass());
	const FKeyMappingRow* EngineRow = Engine.Settings->GetActiveKeyProfile()->FindKeyMappingRow(Jump);
	TestTrue(TEXT("control: engine default keeps one jump mapping (pad overwrote keyboard)"), EngineRow && EngineRow->Mappings.Num() == 1);

	const FFixture Ours(UVBInputUserSettings::StaticClass(), UVBPlayerMappableKeyProfile::StaticClass());
	const FKeyMappingRow* OurRow = Ours.Settings->GetActiveKeyProfile()->FindKeyMappingRow(Jump);
	TestTrue(TEXT("ours: one jump mapping per device"), OurRow && OurRow->Mappings.Num() == 2);

	const TArray<FVBKeyBindingRow> Keyboard = Ours.VB()->GetRows(EVBKeyDevice::KeyboardMouse);
	const TArray<FVBKeyBindingRow> Gamepad = Ours.VB()->GetRows(EVBKeyDevice::Gamepad);
	TestEqual(TEXT("keyboard rows"), Keyboard.Num(), 2);
	TestEqual(TEXT("gamepad rows"), Gamepad.Num(), 2);
	TestTrue(TEXT("keyboard jump = Space"), KeyOf(Keyboard, TEXT("Test.Jump")) == EKeys::SpaceBar);
	TestTrue(TEXT("gamepad jump = A"), KeyOf(Gamepad, TEXT("Test.Jump")) == EKeys::Gamepad_FaceButton_Bottom);
	TestTrue(TEXT("display name comes from the action"), Keyboard.Num() > 0 && Keyboard[0].DisplayName.ToString() == TEXT("Jump"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyBindingRebindTest, "Vowbound.Unit.KeyBinding.Rebind",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyBindingRebindTest::RunTest(const FString& Parameters)
{
	using namespace VBKeyBindingTestUtil;
	const FName Jump(TEXT("Test.Jump"));
	const FName Dodge(TEXT("Test.Dodge"));
	const FFixture Fixture(UVBInputUserSettings::StaticClass(), UVBPlayerMappableKeyProfile::StaticClass());
	UVBInputUserSettings* Settings = Fixture.VB();
	TArray<FName> Cleared;

	// 받을 수 없는 키는 아무것도 안 바꾼다.
	TestFalse(TEXT("pad key on a keyboard row is refused"), Settings->RebindKey(Jump, EVBKeyDevice::KeyboardMouse, EKeys::Gamepad_FaceButton_Top, Cleared));
	TestTrue(TEXT("refused = jump still Space"), KeyOf(Settings->GetRows(EVBKeyDevice::KeyboardMouse), TEXT("Test.Jump")) == EKeys::SpaceBar);
	TestFalse(TEXT("unknown row is refused"), Settings->RebindKey(FName(TEXT("Test.Missing")), EVBKeyDevice::KeyboardMouse, EKeys::G, Cleared));

	// 겹치면 그 줄은 키 없음, 다른 장치는 그대로.
	TestTrue(TEXT("dodge -> Space"), Settings->RebindKey(Dodge, EVBKeyDevice::KeyboardMouse, EKeys::SpaceBar, Cleared));
	TestTrue(TEXT("jump was cleared"), Cleared.Num() == 1 && Cleared[0] == Jump);
	const TArray<FVBKeyBindingRow> Keyboard = Settings->GetRows(EVBKeyDevice::KeyboardMouse);
	TestTrue(TEXT("keyboard dodge = Space"), KeyOf(Keyboard, TEXT("Test.Dodge")) == EKeys::SpaceBar);
	TestFalse(TEXT("keyboard jump = no key"), KeyOf(Keyboard, TEXT("Test.Jump")).IsValid());
	TestTrue(TEXT("pad jump untouched"), KeyOf(Settings->GetRows(EVBKeyDevice::Gamepad), TEXT("Test.Jump")) == EKeys::Gamepad_FaceButton_Bottom);

	// 장치별 기본값: 키보드만 돌아오고 패드에서 바꾼 것은 남는다.
	TestTrue(TEXT("pad dodge -> Y"), Settings->RebindKey(Dodge, EVBKeyDevice::Gamepad, EKeys::Gamepad_FaceButton_Top, Cleared));
	Settings->ResetDeviceToDefault(EVBKeyDevice::KeyboardMouse);
	const TArray<FVBKeyBindingRow> KeyboardAfterReset = Settings->GetRows(EVBKeyDevice::KeyboardMouse);
	TestTrue(TEXT("reset: keyboard jump = Space"), KeyOf(KeyboardAfterReset, TEXT("Test.Jump")) == EKeys::SpaceBar);
	TestTrue(TEXT("reset: keyboard dodge = V"), KeyOf(KeyboardAfterReset, TEXT("Test.Dodge")) == EKeys::V);
	TestTrue(TEXT("reset keeps pad change"), KeyOf(Settings->GetRows(EVBKeyDevice::Gamepad), TEXT("Test.Dodge")) == EKeys::Gamepad_FaceButton_Top);

	// 키 받기로 들어온 트리거 버튼은 축으로 저장된다.
	TestTrue(TEXT("pad jump -> LT button"), Settings->RebindKey(Jump, EVBKeyDevice::Gamepad, EKeys::Gamepad_LeftTrigger, Cleared));
	TestTrue(TEXT("stored as LT axis"), KeyOf(Settings->GetRows(EVBKeyDevice::Gamepad), TEXT("Test.Jump")) == EKeys::Gamepad_LeftTriggerAxis);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyBindingUnboundRebuildTest, "Vowbound.Unit.KeyBinding.UnboundRebuild",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyBindingUnboundRebuildTest::RunTest(const FString& Parameters)
{
	using namespace VBKeyBindingTestUtil;
	const FName Jump(TEXT("Test.Jump"));
	const FName Dodge(TEXT("Test.Dodge"));
	TArray<FName> Cleared;

	// 패드 회피를 A 로 -> 패드 점프는 키 없음. 다시 지을 때 점프(패드 IMC 0번 줄)에 들어갈 키를 본다.
	// 대조군: 엔진 기본 프로필은 Invalid 를 «패드 키 아님» 으로 걸러 0개 -> 엔진이 기본 키 A 를 다시 넣는다(화면은 «없음» 인데 A 가 먹는다).
	const FFixture Engine(UVBInputUserSettings::StaticClass(), UEnhancedPlayerMappableKeyProfile::StaticClass());
	TestTrue(TEXT("control: rebind ok"), Engine.VB()->RebindKey(Dodge, EVBKeyDevice::Gamepad, EKeys::Gamepad_FaceButton_Bottom, Cleared));
	TestEqual(TEXT("control: engine profile drops the unbound pad key (default comes back)"), Engine.RebuildKeys(Engine.GamepadIMC, 0).Num(), 0);

	const FFixture Ours(UVBInputUserSettings::StaticClass(), UVBPlayerMappableKeyProfile::StaticClass());
	TestTrue(TEXT("rebind ok"), Ours.VB()->RebindKey(Dodge, EVBKeyDevice::Gamepad, EKeys::Gamepad_FaceButton_Bottom, Cleared));
	const TArray<FKey> PadJump = Ours.RebuildKeys(Ours.GamepadIMC, 0);
	TestTrue(TEXT("ours: unbound pad jump stays unbound"), PadJump.Num() == 1 && !PadJump[0].IsValid());
	const TArray<FKey> PadDodge = Ours.RebuildKeys(Ours.GamepadIMC, 1);
	TestTrue(TEXT("ours: pad dodge = A"), PadDodge.Num() == 1 && PadDodge[0] == EKeys::Gamepad_FaceButton_Bottom);
	const TArray<FKey> KeyboardJump = Ours.RebuildKeys(Ours.KeyboardIMC, 0);
	TestTrue(TEXT("ours: keyboard jump untouched"), KeyboardJump.Num() == 1 && KeyboardJump[0] == EKeys::SpaceBar);

	// 키보드 줄의 키 없음은 엔진 기본도 지킨다(Invalid 도 «패드 아님» 이라 통과) - 그래서 키보드만 보면 함정이 안 보인다.
	TestTrue(TEXT("keyboard dodge -> Space"), Ours.VB()->RebindKey(Dodge, EVBKeyDevice::KeyboardMouse, EKeys::SpaceBar, Cleared));
	const TArray<FKey> KeyboardJumpCleared = Ours.RebuildKeys(Ours.KeyboardIMC, 0);
	TestTrue(TEXT("ours: unbound keyboard jump stays unbound"), KeyboardJumpCleared.Num() == 1 && !KeyboardJumpCleared[0].IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyBindingRebindAllOrNothingTest, "Vowbound.Unit.KeyBinding.RebindAllOrNothing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyBindingRebindAllOrNothingTest::RunTest(const FString& Parameters)
{
	using namespace VBKeyBindingTestUtil;
	const FName Dodge(TEXT("Test.Dodge"));
	TArray<FName> Cleared;

	// 키보드 회피 줄의 장치 «이름» 만 바꾼다(종류는 키보드 그대로) - 화면 줄(GetRows)에는 보이는데 MapPlayerKey 는 그 칸을 못 찾는 줄.
	const FFixture Fixture(UVBInputUserSettings::StaticClass(), UVBPlayerMappableKeyProfile::StaticClass());
	UVBInputUserSettings* Settings = Fixture.VB();
	FKeyMappingRow* DodgeRow = Settings->GetActiveKeyProfile()->FindKeyMappingRowMutable(Dodge);
	if (!TestNotNull(TEXT("dodge row"), DodgeRow))
	{
		return false;
	}
	for (FPlayerKeyMapping& Mapping : DodgeRow->Mappings)
	{
		if (Mapping.GetPrimaryDeviceType() == EHardwareDevicePrimaryType::KeyboardAndMouse)
		{
			Mapping.SetHardwareDeviceId(FHardwareDeviceIdentifier(TEXT("VBTestInput"), TEXT("VBTestKeyboard"), EHardwareDevicePrimaryType::KeyboardAndMouse));
		}
	}
	TestTrue(TEXT("dodge still listed on the keyboard tab"), KeyOf(Settings->GetRows(EVBKeyDevice::KeyboardMouse), TEXT("Test.Dodge")) == EKeys::V);

	// 대조군: 엔진 MapPlayerKey 는 이 칸을 못 찾아도 실패를 알리지 않고 아무것도 안 한다 - 실패 표시만 보면 «바뀌었다» 로 읽힌다.
	//   칸 새로 만들기는 우리 RebindKey 와 같이 끈다(FillArgs). 인자의 기본값은 켜짐이라(FMapPlayerKeyArgs 생성자) 안 적으면 엔진이 칸을 새로 만든다 -
	//   그러면 뒤의 RebindKey 가 그 새 칸을 찾아 버려 이 시험이 아무것도 못 가른다(2026-10-02 첫 실행이 그렇게 실패했다).
	const int32 DodgeMappingsBefore = DodgeRow->Mappings.Num();
	FMapPlayerKeyArgs Args;
	Args.MappingName = Dodge;
	Args.Slot = EPlayerMappableKeySlot::First;
	Args.NewKey = EKeys::G;
	Args.HardwareDeviceId = FHardwareDeviceIdentifier::DefaultKeyboardAndMouse.HardwareDeviceIdentifier;
	Args.bCreateMatchingSlotIfNeeded = false;
	FGameplayTagContainer Failure;
	Settings->MapPlayerKey(Args, Failure);
	TestTrue(TEXT("control: engine reports no failure"), Failure.IsEmpty());
	TestEqual(TEXT("control: engine made no new slot"), DodgeRow->Mappings.Num(), DodgeMappingsBefore);
	TestTrue(TEXT("control: engine changed nothing"), KeyOf(Settings->GetRows(EVBKeyDevice::KeyboardMouse), TEXT("Test.Dodge")) == EKeys::V);

	// 회피 -> Space 는 점프와 겹친다. 회피 칸을 못 찾으니 점프도 비우지 않는다(고치기 전에는 점프만 비우고 true 였다 - 2차 검증 1).
	AddExpectedMessagePlain(TEXT("칸을 찾지 못해"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestFalse(TEXT("rebind refused"), Settings->RebindKey(Dodge, EVBKeyDevice::KeyboardMouse, EKeys::SpaceBar, Cleared));
	TestEqual(TEXT("nothing reported cleared"), Cleared.Num(), 0);
	const TArray<FVBKeyBindingRow> Keyboard = Settings->GetRows(EVBKeyDevice::KeyboardMouse);
	TestTrue(TEXT("jump kept Space"), KeyOf(Keyboard, TEXT("Test.Jump")) == EKeys::SpaceBar);
	TestTrue(TEXT("dodge kept V"), KeyOf(Keyboard, TEXT("Test.Dodge")) == EKeys::V);
	return true;
}
#endif
