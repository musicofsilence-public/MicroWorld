#include <MicroWorld/TickFunction.h>

#include <limits>

namespace MicroWorld
{

FTickFunction::FTickFunction(const FTickConfiguration Configuration) noexcept
	: IntervalMilliseconds(Configuration.TickIntervalMilliseconds)
	, bCanEverTick(Configuration.bCanEverTick)
	, bEnabled(Configuration.bCanEverTick && Configuration.bStartWithTickEnabled)
{
}

void FTickFunction::BeginPlay(const TimePointMilliseconds NowMilliseconds) noexcept
{
	LastObservedMilliseconds = NowMilliseconds;
	PreviousTickMilliseconds = NowMilliseconds;
	NextDueMilliseconds = NowMilliseconds;
	bPlaying = true;
	bMustResetSchedule = true;
}

void FTickFunction::EndPlay() noexcept
{
	bPlaying = false;
}

ERuntimeResult FTickFunction::SetEnabled(const bool bNewEnabled) noexcept
{
	if (bNewEnabled && !bCanEverTick)
	{
		return ERuntimeResult::CannotEverTick;
	}
	if (bEnabled == bNewEnabled)
	{
		return ERuntimeResult::Success;
	}

	bEnabled = bNewEnabled;
	bMustResetSchedule = true;
	return ERuntimeResult::Success;
}

ERuntimeResult FTickFunction::SetInterval(const DurationMilliseconds NewIntervalMilliseconds) noexcept
{
	IntervalMilliseconds = NewIntervalMilliseconds;
	bMustResetSchedule = true;
	return ERuntimeResult::Success;
}

FTickDecision FTickFunction::Advance(const TimePointMilliseconds NowMilliseconds) noexcept
{
	if (!bPlaying)
	{
		return FTickDecision::Rejected(ERuntimeResult::InvalidLifecycle);
	}
	if (NowMilliseconds < LastObservedMilliseconds)
	{
		return FTickDecision::Rejected(ERuntimeResult::NonMonotonicTime);
	}
	LastObservedMilliseconds = NowMilliseconds;

	if (!bEnabled)
	{
		return FTickDecision::NotDue();
	}
	if (bMustResetSchedule)
	{
		return BeginResetSchedule(NowMilliseconds);
	}
	if (!IsTickDueNow(NowMilliseconds))
	{
		return FTickDecision::NotDue();
	}
	return ProduceDueTick(NowMilliseconds);
}

FTickDecision FTickFunction::BeginResetSchedule(const TimePointMilliseconds NowMilliseconds) noexcept
{
	bMustResetSchedule = false;
	PreviousTickMilliseconds = NowMilliseconds;
	NextDueMilliseconds = CalculateNextDueMilliseconds(NowMilliseconds);
	return FTickDecision::Ticked(NowMilliseconds, 0);
}

bool FTickFunction::IsTickDueNow(const TimePointMilliseconds NowMilliseconds) const noexcept
{
	if (IntervalMilliseconds == 0)
	{
		return true;
	}
	const bool bBeforeDeadline = NowMilliseconds < NextDueMilliseconds;
	const bool bAlreadyTickedAtThisTime = NowMilliseconds == PreviousTickMilliseconds;
	return !(bBeforeDeadline || bAlreadyTickedAtThisTime);
}

FTickDecision FTickFunction::ProduceDueTick(const TimePointMilliseconds NowMilliseconds) noexcept
{
	const DurationMilliseconds DeltaMilliseconds = CalculateDeltaMilliseconds(NowMilliseconds);
	PreviousTickMilliseconds = NowMilliseconds;
	NextDueMilliseconds = CalculateNextDueMilliseconds(NowMilliseconds);
	return FTickDecision::Ticked(NowMilliseconds, DeltaMilliseconds);
}

bool FTickFunction::IsEnabled() const noexcept
{
	return bEnabled;
}

DurationMilliseconds FTickFunction::GetInterval() const noexcept
{
	return IntervalMilliseconds;
}

TimePointMilliseconds FTickFunction::CalculateNextDueMilliseconds(const TimePointMilliseconds NowMilliseconds) const noexcept
{
	const TimePointMilliseconds MaximumTime = std::numeric_limits<TimePointMilliseconds>::max();
	if (MaximumTime - NowMilliseconds < IntervalMilliseconds)
	{
		return MaximumTime;
	}
	return NowMilliseconds + IntervalMilliseconds;
}

DurationMilliseconds FTickFunction::CalculateDeltaMilliseconds(const TimePointMilliseconds NowMilliseconds) const noexcept
{
	const TimePointMilliseconds DeltaMilliseconds = NowMilliseconds - PreviousTickMilliseconds;
	const TimePointMilliseconds MaximumDuration = std::numeric_limits<DurationMilliseconds>::max();
	if (DeltaMilliseconds > MaximumDuration)
	{
		return std::numeric_limits<DurationMilliseconds>::max();
	}
	return static_cast<DurationMilliseconds>(DeltaMilliseconds);
}

} // namespace MicroWorld
