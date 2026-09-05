// VowBound: The Oath of the Scalpel Copyright (c) 2025. All Rights Reserved.


#include "Input/VBInputDeviceManager.h"
#include "EnhancedInputSubsystems.h"
#include "Vowbound/Vowbound.h"


UVBInputDeviceManager::UVBInputDeviceManager()
{
	PrimaryComponentTick.bCanEverTick = false;
}

ECommonInputType UVBInputDeviceManager::GetCurrentDeviceType() const
{
	return CurrentDeviceType;
}

bool UVBInputDeviceManager::IsUsingGamepad() const
{
	return CurrentDeviceType == ECommonInputType::Gamepad;
}

void UVBInputDeviceManager::BeginPlay()
{
	Super::BeginPlay();

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn) return;

	// 입력 감지는 로컬 소유 Pawn에서만 의미 있음.
	// Listen Server의 경우 Client Pawn이 서버 측에선 remote-proxy로 spawn되는데
	// 이 때 GetLocalPlayer()가 nullptr → InitializeInputDetection에서 크래시.
	if (!Pawn->IsLocallyControlled()) return;

	CachedPC = Cast<APlayerController>(Pawn->GetController());
	if (!CachedPC.IsValid()) return;

	InitializeInputDetection();
}

void UVBInputDeviceManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CachedPC.IsValid())
	{
		if (ULocalPlayer* LP = CachedPC->GetLocalPlayer())
		{
			if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LP))
			{
				CommonInput->OnInputMethodChangedNative.RemoveAll(this);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UVBInputDeviceManager::InitializeInputDetection()
{
	if (!CachedPC.IsValid()) return;

	ULocalPlayer* LP = CachedPC->GetLocalPlayer();
	if (!LP) return;

	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LP))
	{
		// 현재 입력 타입으로 초기 IMC 설정
		ECommonInputType CurrentType = CommonInput->GetCurrentInputType();
		
		if (CurrentType == ECommonInputType::Gamepad)
		{
			SwitchToGamepad();
		}
		else
		{
			SwitchToKeyboardMouse();
		}
		// 입력 방식 변경 델리게이트 바인딩
		CommonInput->OnInputMethodChangedNative.AddUObject(
		                                                   this, &UVBInputDeviceManager::HandleInputMethodChanged);
	}
	else
	{
		// CommonInput 사용 불가 -> 기본 키보드/마우스 IMC 적용
		SwitchToKeyboardMouse();
	}
}

void UVBInputDeviceManager::HandleInputMethodChanged(ECommonInputType NewInputType)
{
	switch (NewInputType)
	{
		case ECommonInputType::MouseAndKeyboard:
			{
				if (CurrentDeviceType != ECommonInputType::MouseAndKeyboard)
				{
					SwitchToKeyboardMouse();
				}
			}
			break;
		case ECommonInputType::Gamepad:
			{
				if (CurrentDeviceType != ECommonInputType::Gamepad)
				{
					SwitchToGamepad();
				}
			}
			break;
		default:
			break;
	}
}

void UVBInputDeviceManager::SwitchToKeyboardMouse()
{
	// 새 IMC 추가 이전 IMC 제거
	ApplyMappingContext(IMC_KeyboardMouse, IMC_Gamepad);
	CurrentDeviceType = ECommonInputType::MouseAndKeyboard;
	OnInputDeviceChanged.Broadcast(ECommonInputType::MouseAndKeyboard);
	VB_LOG(Log, "Input switched to Keyboard/Mouse");
}

void UVBInputDeviceManager::SwitchToGamepad()
{
	// 새 IMC 추가 이전 IMC 제거
	ApplyMappingContext(IMC_Gamepad, IMC_KeyboardMouse);
	CurrentDeviceType = ECommonInputType::Gamepad;
	OnInputDeviceChanged.Broadcast(ECommonInputType::Gamepad);
	VB_LOG(Log, "Input switched to Gamepad");
}

void UVBInputDeviceManager::ApplyMappingContext(UInputMappingContext* NewIMC, UInputMappingContext* OldIMC)
{
	if (!CachedPC.IsValid()) return;

	ULocalPlayer*                       LP          = CachedPC->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* EISubSystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP);

	if (!EISubSystem) return;

	// 이전 IMC 제거
	if (OldIMC)
	{
		EISubSystem->RemoveMappingContext(OldIMC);
	}

	// 새 IMC 추가
	if (NewIMC)
	{
		EISubSystem->AddMappingContext(NewIMC, InputMappingPriority);
	}
}
