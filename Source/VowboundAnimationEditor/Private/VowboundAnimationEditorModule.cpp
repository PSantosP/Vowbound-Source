#include "Modules/ModuleManager.h"

#include "PropertyEditorModule.h"
#include "VBFinisherCameraAuthoringCustomization.h"
#include "Data/VBFinisherConfig.h"

/**
 * 에디터 모듈. 지금 하는 일은 디테일 패널 커스터마이제이션 등록 하나다.
 *
 * 종전에는 FDefaultModuleImpl 이었다. 처형 카메라 저작 도구가 런타임 자산 안(CallInEditor)에 있던 동안은
 *   등록할 것이 없었다. 1b(2026-10-02)에서 그 도구를 이 모듈로 옮기며 패널이 생겼다.
 */
class FVowboundAnimationEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// 이 모듈은 UncookedOnly 라 에디터가 아닌 실행(-game 헤드리스 시험 등)에서도 올라온다.
		//   디테일 패널은 에디터에만 있으므로 그때는 등록하지 않는다.
		if (!GIsEditor)
		{
			return;
		}

		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomClassLayout(
			UVBFinisherConfig::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FVBFinisherCameraAuthoringCustomization::MakeInstance));
		PropertyEditor.NotifyCustomizationModuleChanged();
		bRegistered = true;
	}

	virtual void ShutdownModule() override
	{
		// 엔진이 내려가는 순서에 따라 PropertyEditor 가 먼저 내려갈 수 있다. 그때는 지울 것이 없다.
		if (!bRegistered || !FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			return;
		}

		FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.UnregisterCustomClassLayout(UVBFinisherConfig::StaticClass()->GetFName());
		PropertyEditor.NotifyCustomizationModuleChanged();
		bRegistered = false;
	}

private:
	bool bRegistered = false;
};

IMPLEMENT_MODULE(FVowboundAnimationEditorModule, VowboundAnimationEditor)
