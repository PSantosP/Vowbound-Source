#include "VBEnvironmentInteractionAuthoring.h"
#include "Animation/AnimMontage.h"

bool UVBEnvironmentInteractionAuthoring::ConfigurePushMontage(UAnimMontage* Montage)
{
	if (!Montage || Montage->SlotAnimTracks.Num() != 1
		|| Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 5) return false;
	const auto& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
	const FName Names[] = { TEXT("Grab"), TEXT("Idle"), TEXT("Push"), TEXT("Pull"), TEXT("Release") };
	for (int32 Index = 0; Index < 5; ++Index)
	{
		if (!Segments[Index].GetAnimReference()
			|| !Segments[Index].GetAnimReference()->GetName().EndsWith(Names[Index].ToString())) return false;
	}
	Montage->Modify();
	Montage->CompositeSections.Reset();
	for (int32 Index = 0; Index < 5; ++Index)
	{
		FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = Names[Index];
		Section.Link(Montage, Segments[Index].StartPos);
		Section.SetTime(Segments[Index].StartPos);
		Section.NextSectionName = Index == 0 ? Names[1] : (Index == 4 ? NAME_None : Names[Index]);
	}
	Montage->BlendIn.SetBlendTime(0.15f);
	Montage->BlendOut.SetBlendTime(0.15f);
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}
