// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.

#include "Input/VBInputUserSettings.h"

#include "EnhancedActionKeyMapping.h"
#include "GameFramework/InputSettings.h"
#include "Input/VBKeyBindingRules.h"
#include "Vowbound/Vowbound.h"

namespace
{
	// 장치 -> 엔진 장치 식별자. 줄에 다는 것(등록)과 줄을 찾는 것(MapPlayerKey 인자)과 다시 지을 때 고르는 것(키 프로필)이 이 하나를 같이 쓴다.
	const FHardwareDeviceIdentifier& HardwareOf(EVBKeyDevice Device)
	{
		return Device == EVBKeyDevice::Gamepad ? FHardwareDeviceIdentifier::DefaultGamepad : FHardwareDeviceIdentifier::DefaultKeyboardAndMouse;
	}
}

FHardwareDeviceIdentifier UVBInputUserSettings::DetermineHardwareDeviceForActionMapping(const FEnhancedActionKeyMapping& ActionMapping, const UInputMappingContext* IMC) const
{
	return HardwareOf(VBKeyBinding::DeviceOf(ActionMapping.Key));
}

TArray<FVBKeyBindingRow> UVBInputUserSettings::GetRows(EVBKeyDevice Device) const
{
	TArray<FVBKeyBindingRow> Rows;
	const UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	if (!Profile)
	{
		return Rows;
	}
	const EHardwareDevicePrimaryType DeviceType = HardwareOf(Device).PrimaryDeviceType;
	for (const TPair<FName, FKeyMappingRow>& Pair : Profile->GetPlayerMappingRows())
	{
		for (const FPlayerKeyMapping& Mapping : Pair.Value.Mappings)
		{
			if (Mapping.GetSlot() == EPlayerMappableKeySlot::First && Mapping.GetPrimaryDeviceType() == DeviceType)
			{
				FVBKeyBindingRow& Row = Rows.AddDefaulted_GetRef();
				Row.MappingName = Pair.Key;
				Row.Device = Device;
				Row.Key = Mapping.GetCurrentKey();
				Row.DefaultKey = Mapping.GetDefaultKey();
				Row.DisplayName = Mapping.GetDisplayName();
				Row.DisplayCategory = Mapping.GetDisplayCategory();
			}
		}
	}
	return Rows;
}

bool UVBInputUserSettings::RebindKey(FName MappingName, EVBKeyDevice Device, const FKey& InNewKey, TArray<FName>& OutCleared)
{
	OutCleared.Reset();
	// 트리거 버튼은 트리거 축으로 모아 둔다 - IMC 기본 키가 축이라, 그래야 «기본 키로 돌아왔다» 와 겹침이 맞게 보인다.
	const FKey NewKey = VBKeyBinding::NormalizeKey(InNewKey);
	if (!VBKeyBinding::CanBind(NewKey, Device))
	{
		return false;
	}
	const TArray<FVBKeyBindingRow> Rows = GetRows(Device);
	const auto RowOf = [&Rows](FName Name) -> const FVBKeyBindingRow* { return Rows.FindByPredicate([Name](const FVBKeyBindingRow& Row) { return Row.MappingName == Name; }); };
	const FVBKeyBindingRow* Target = RowOf(MappingName);
	if (!Target)
	{
		return false;
	}

	// 바꿀 줄 전부와 바꾸기 전 키. 겹친 줄은 키 없음 = Invalid 다 - UnMapPlayerKey 는 이름과 달리 기본 키로 되돌린다(FPlayerKeyMapping::ResetToDefault).
	struct FChange
	{
		FName Name;
		FKey Before;
		FKey After;
	};
	TArray<FChange> Changes;
	for (const FName Other : VBKeyBinding::FindConflicts(Rows, MappingName, Device, NewKey))
	{
		const FVBKeyBindingRow* Row = RowOf(Other);
		Changes.Add({Other, Row ? Row->Key : FKey(), EKeys::Invalid});
	}
	Changes.Add({MappingName, Target->Key, NewKey});

	// 하나라도 못 찾으면 아무것도 바꾸지 않는다. MapPlayerKey 는 «줄은 있는데 이 칸·장치가 없다» 를 실패로 알리지 않고 그냥 넘어간다
	//   (UE 5.8 UEnhancedInputUserSettings::MapPlayerKey 의 bCreateMatchingSlotIfNeeded=false 분기) - 실패 표시가 비었다고 바뀐 것이 아니다.
	//   GetRows 는 장치 «종류» 로, MapPlayerKey 는 장치 «이름» 으로 줄을 찾으므로 둘이 어긋나는 줄이 그 경우다. 같은 찾기(FindKeyMapping)로 먼저 본다.
	const UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	FMapPlayerKeyArgs Args;
	const auto CurrentKey = [Profile, &Args, Device](FName Name) -> TOptional<FKey>
	{
		FillArgs(Args, Name, Device, FKey());
		const FPlayerKeyMapping* Mapping = Profile ? Profile->FindKeyMapping(Args) : nullptr;
		return Mapping ? TOptional<FKey>(Mapping->GetCurrentKey()) : TOptional<FKey>();
	};
	for (const FChange& Change : Changes)
	{
		if (!CurrentKey(Change.Name).IsSet())
		{
			VB_LOG(Warning, "InputUserSettings: %s 의 %s 칸을 찾지 못해 아무것도 바꾸지 않는다", *Change.Name.ToString(), *HardwareOf(Device).HardwareDeviceIdentifier.ToString());
			return false;
		}
	}

	FGameplayTagContainer Failure;
	for (const FChange& Change : Changes)
	{
		FillArgs(Args, Change.Name, Device, Change.After);
		MapPlayerKey(Args, Failure);
	}
	// 위에서 같은 찾기로 확인했으니 어긋날 길이 없다. 그래도 결과가 다르면 바꾸기 전 키로 되돌린다 - 반쯤 바뀐 배치가 다음 저장에 실리지 않게(2차 검증 1).
	const bool bApplied = Failure.IsEmpty() && !Changes.ContainsByPredicate([&CurrentKey](const FChange& Change)
	{
		const TOptional<FKey> Now = CurrentKey(Change.Name);
		return !Now.IsSet() || Now.GetValue() != Change.After;
	});
	if (!bApplied)
	{
		VB_LOG(Error, "InputUserSettings: 키 바꾸기 실패 %s -> %s (%s) - 바꾸기 전으로 되돌린다", *MappingName.ToString(), *NewKey.ToString(), *Failure.ToStringSimple());
		FGameplayTagContainer RestoreFailure;
		for (int32 Index = Changes.Num() - 1; Index >= 0; --Index)
		{
			FillArgs(Args, Changes[Index].Name, Device, Changes[Index].Before);
			MapPlayerKey(Args, RestoreFailure);
		}
		VB_CLOG(!RestoreFailure.IsEmpty(), Error, "InputUserSettings: 되돌리기도 일부 실패 (%s)", *RestoreFailure.ToStringSimple());
		return false;
	}
	for (int32 Index = 0; Index < Changes.Num() - 1; ++Index)
	{
		OutCleared.Add(Changes[Index].Name);
	}
	// 한 줄로 «무엇이 무엇으로, 누가 비워졌나» 를 남긴다 - PIE 에서 키가 안 먹을 때 설정이 바뀌었는지부터 가른다.
	VB_LOG(Log, "InputUserSettings: %s [%s] -> %s, 비운 줄 %d", *MappingName.ToString(), Device == EVBKeyDevice::Gamepad ? TEXT("Gamepad") : TEXT("KBM"),
		*NewKey.ToString(), OutCleared.Num());
	return true;
}

void UVBInputUserSettings::ResetDeviceToDefault(EVBKeyDevice Device)
{
	// 여기서는 UnMapPlayerKey 가 원하는 일이다 - 줄을 IMC 기본 키로 되돌린다.
	FGameplayTagContainer Failure;
	FMapPlayerKeyArgs Args;
	for (const FVBKeyBindingRow& Row : GetRows(Device))
	{
		FillArgs(Args, Row.MappingName, Device, FKey());
		UnMapPlayerKey(Args, Failure);
	}
	VB_CLOG(!Failure.IsEmpty(), Warning, "InputUserSettings: 기본값으로 되돌리기 일부 실패 (%s)", *Failure.ToStringSimple());
}

void UVBInputUserSettings::FillArgs(FMapPlayerKeyArgs& Args, FName MappingName, EVBKeyDevice Device, const FKey& NewKey)
{
	Args.MappingName = MappingName;
	Args.Slot = EPlayerMappableKeySlot::First;
	Args.NewKey = NewKey;
	Args.HardwareDeviceId = HardwareOf(Device).HardwareDeviceIdentifier;
	// 없는 칸을 새로 만들지 않는다 - 줄은 IMC 등록이 만들고, 화면은 있는 줄의 키만 바꾼다.
	Args.bCreateMatchingSlotIfNeeded = false;
}

int32 UVBPlayerMappableKeyProfile::GetPlayerMappedKeysForRebuildControlMappings(const FEnhancedActionKeyMapping& DefaultMapping, TArray<FKey>& OutKeys) const
{
	OutKeys.Reset();
	if (!DefaultMapping.IsPlayerMappable())
	{
		return 0;
	}
	// KeyToMatch 를 비워 두면 엔진이 키 종류 견주기를 건너뛴다(DoesMappingPassQueryOptions). 그 대신 줄의 장치로 고른다.
	//   장치가 없는 줄(엔진 기본 설정 클래스로 등록·저장된 것)은 어느 장치에도 안 맞아 비고, 그러면 엔진이 기본 키를 쓴다 - 옛 동작으로 떨어진다.
	FPlayerMappableKeyQueryOptions Options;
	Options.MappingName = DefaultMapping.GetMappingName();
	Options.RequiredDeviceType = HardwareOf(VBKeyBinding::DeviceOf(DefaultMapping.Key)).PrimaryDeviceType;
	return QueryPlayerMappedKeys(Options, OutKeys);
}
