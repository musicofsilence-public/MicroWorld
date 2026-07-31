#include "CoreTickExample.h"

void FCoreTickExample::Begin(const MicroWorld::Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	TickCount = 0;
	bFinished = false;
	SensorTick.BeginPlay(InNowMilliseconds);
}

FCoreTickExampleStep FCoreTickExample::Advance(const MicroWorld::Core::TimePointMilliseconds InNowMilliseconds) noexcept
{
	FCoreTickExampleStep Step;
	Step.bFinished = bFinished;
	if (bFinished)
	{
		return Step;
	}

	Step.Decision = SensorTick.Advance(InNowMilliseconds);
	if (Step.Decision.bShouldTick)
	{
		++TickCount;
		if (TickCount == TargetTickCount)
		{
			SensorTick.EndPlay();
			bFinished = true;
		}
	}

	Step.bFinished = bFinished;
	return Step;
}

bool FCoreTickExample::IsFinished() const noexcept
{
	return bFinished;
}
