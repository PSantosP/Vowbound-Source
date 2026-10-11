#include "Narrative/Cinematic/VBCinematicRules.h"

#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "VBCinematicDefinition.h"
#include "Vowbound/Vowbound.h"

void FVBEffect_PlayCinematic::Apply(const FVBRuleContext& Context) const
{
	if (!IsValid(Context.World) || !Context.CanApplyEffects()) return;
	UVBCinematicDirectorSubsystem* Director = Context.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
	if (!Director) return;
	const FVBGameCinematicStartResult Result = Director->RequestPlay(
		Definition, Context.Instigator, PlayerBindingTag, OnCompletedEffects, Context.Counterpart);
	if (Result.Status != EVBGameCinematicStartStatus::Accepted)
	{
		VB_LOG(Warning, "Cinematic request refused: %d", static_cast<int32>(Result.Status));
	}
}

bool FVBCondition_CinematicCompleted::Evaluate(const FVBRuleContext& Context) const
{
	if (!IsValid(Context.World) || !IsValid(Context.Instigator) || !IsValid(Definition)) return false;
	const UVBCinematicDirectorSubsystem* Director = Context.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
	return Director && Director->HasCompletedFor(Context.Instigator->GetPawn(), Definition);
}
