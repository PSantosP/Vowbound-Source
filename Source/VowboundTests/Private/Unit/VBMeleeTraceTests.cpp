#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Character/VBMeleeTraceComponent.h"
#include "Data/VBAttackConfig.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifyEndDataContext.h"
#include "Animation/AnimNotifyLibrary.h"
#include "Animation/AnimNotifyQueue.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "Animation/VBANS_ComboWindow.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceSamplingTest, "Vowbound.Combat.MeleeTrace.Sampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceSamplingTest::RunTest(const FString& Parameters)
{
	FVBResolvedMeleeShape Shape;
	Shape.BaseLocal = FVector::ZeroVector;
	Shape.TipLocal = FVector(100.0, 0.0, 0.0);
	Shape.Radius = 10.0f;
	const FTransform Start = FTransform::Identity;
	TestEqual(TEXT("A stationary first pose still receives a query"), UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Start, 16), 1);
	const FTransform Turn(FQuat(FVector::UpVector, FMath::DegreesToRadians(60.0f)));
	const int32 Steps = UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Turn, 16);
	// 반경 100cm 의 60도 호는 약 104.72cm. 끝점 사이 100cm 만 재던 10단계로는 호 간격이 10cm 를 넘는다.
	TestTrue(TEXT("The sampled arc fits within the trace radius"), FMath::DegreesToRadians(60.0f) * 100.0f / Steps <= Shape.Radius);
	TestTrue(TEXT("Long blade rotation uses more than endpoint chord sampling"), Steps > 10);
	const FTransform LargeTurn(FQuat(FVector::UpVector, PI));
	TestEqual(TEXT("Query work remains bounded for a large frame jump"), UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, LargeTurn, 16), 16);
	const FTransform Translated(FVector(45.0, 0.0, 0.0));
	TestEqual(TEXT("Translation spacing follows radius"), UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Translated, 16), 5);
	const FTransform Scaled(FQuat::Identity, FVector::ZeroVector, FVector(1.25));
	TestTrue(TEXT("Scaling without bone translation also needs intermediate queries"), UVBMeleeTraceComponent::CalculateSweepSteps(Shape, Start, Scaled, 16) > 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceNotifyEndTest, "Vowbound.Combat.MeleeTrace.NotifyEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceNotifyEndTest::RunTest(const FString& Parameters)
{
	FAnimNotifyEventReference Interrupted;
	TestFalse(TEXT("An end without a natural-end context cannot add final damage"), UAnimNotifyLibrary::NotifyStateReachedEnd(Interrupted));
	FAnimNotifyEventReference Cancelled;
	Cancelled.AddContextData<UE::Anim::FAnimNotifyEndDataContext>(false);
	TestFalse(TEXT("Explicit interruption cannot add final damage"), UAnimNotifyLibrary::NotifyStateReachedEnd(Cancelled));
	FAnimNotifyEventReference Completed;
	Completed.AddContextData<UE::Anim::FAnimNotifyEndDataContext>(true);
	TestTrue(TEXT("Natural completion retains the last pose query"), UAnimNotifyLibrary::NotifyStateReachedEnd(Completed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeTraceAssetsTest, "Vowbound.Combat.MeleeTrace.AuthoredWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBMeleeTraceAssetsTest::RunTest(const FString& Parameters)
{
	const UVBAttackConfig* Config = LoadObject<UVBAttackConfig>(nullptr, TEXT("/Game/Vowbound/Data/DA_Attack_BigSword_Light.DA_Attack_BigSword_Light"));
	if (!TestNotNull(TEXT("Greatsword attack config"), Config)) return false;
	TestFalse(TEXT("Repeated input preserves greatsword follow-through"), Config->bChainAtWindowOpen);
	for (const TCHAR* Name : {TEXT("AM_LightAttack"), TEXT("AM_BigSword_LightAttack"), TEXT("AM_Fighter_LightAttack")})
	{
		const FString Path = FString::Printf(TEXT("/Game/Vowbound/Animations/Montage/%s.%s"), Name, Name);
		const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *Path);
		if (!TestNotNull(Name, Montage)) continue;
		TArray<const FAnimNotifyEvent*> Traces;
		TArray<const FAnimNotifyEvent*> Combos;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UVBANS_AttackTraceWindow>())
			{
				TestTrue(TEXT("Trace window reads finalized bones through queued notifies"), Event.MontageTickType == EMontageNotifyTickType::Queued);
				Traces.Add(&Event);
			}
			if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UVBANS_ComboWindow>()) Combos.Add(&Event);
		}
		Traces.Sort([](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.GetTriggerTime() < B.GetTriggerTime(); });
		Combos.Sort([](const FAnimNotifyEvent& A, const FAnimNotifyEvent& B) { return A.GetTriggerTime() < B.GetTriggerTime(); });
		if (FCString::Strcmp(Name, TEXT("AM_BigSword_LightAttack")) == 0)
		{
			TestEqual(TEXT("All seven greatsword swings have a trace window"), Traces.Num(), 7);
			TestEqual(TEXT("All seven greatsword swings have a combo window"), Combos.Num(), 7);
			if (Traces.Num() == 7 && Combos.Num() == 7)
			{
				for (int32 Swing = 0; Swing < 7; ++Swing)
				{
					TestTrue(FString::Printf(TEXT("Swing %d cannot chain before its trace finishes"), Swing + 1),
						Combos[Swing]->GetTriggerTime() + Combos[Swing]->GetDuration() >= Traces[Swing]->GetTriggerTime() + Traces[Swing]->GetDuration());
				}
			}
		}
		else
		{
			TestEqual(TEXT("Every katana and fist swing retains its trace window"), Traces.Num(), 5);
		}
	}
	return true;
}

#endif
