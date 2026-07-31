#include <MicroWorld/Core/TickFunction.h>

#include <limits>

namespace MicroWorld::Core
{

FTickFunction::FTickFunction(const FTickConfiguration InConfiguration) noexcept
	: IntervalMilliseconds(InConfiguration.TickIntervalMilliseconds)
	, bCanEverTick(InConfiguration.bCanEverTick)
	, bEnabled(InConfiguration.bCanEverTick && InConfiguration.bStartWithTickEnabled)
{
}

void FTickFunction::BeginPlay(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	LastObservedMilliseconds = InNowMilliseconds;
	PreviousTickMilliseconds = InNowMilliseconds;
	NextDueMilliseconds = InNowMilliseconds;
	bPlaying = true;
	bMustResetSchedule = true;
}

void FTickFunction::EndPlay() noexcept
{
	bPlaying = false;
}

ERuntimeResult FTickFunction::SetEnabled(const bool bInEnabled) noexcept
{
	if (bInEnabled && !bCanEverTick)
	{
		return ERuntimeResult::CannotEverTick;
	}
	if (bEnabled == bInEnabled)
	{
		return ERuntimeResult::Success;
	}

	bEnabled = bInEnabled;
	bMustResetSchedule = true;
	return ERuntimeResult::Success;
}

ERuntimeResult FTickFunction::SetInterval(const DurationMilliseconds InIntervalMilliseconds) noexcept
{
	IntervalMilliseconds = InIntervalMilliseconds;
	bMustResetSchedule = true;
	return ERuntimeResult::Success;
}

FTickDecision FTickFunction::Advance(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	if (!bPlaying)
	{
		return FTickDecision::Rejected(ERuntimeResult::InvalidLifecycle);
	}
	if (InNowMilliseconds < LastObservedMilliseconds)
	{
		return FTickDecision::Rejected(ERuntimeResult::NonMonotonicTime);
	}
	LastObservedMilliseconds = InNowMilliseconds;

	if (!bEnabled)
	{
		return FTickDecision::NotDue();
	}
	if (bMustResetSchedule)
	{
		return BeginResetSchedule(InNowMilliseconds);
	}
	if (!IsTickDueNow(InNowMilliseconds))
	{
		return FTickDecision::NotDue();
	}
	return ProduceDueTick(InNowMilliseconds);
}

FTickDecision FTickFunction::BeginResetSchedule(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	bMustResetSchedule = false;
	PreviousTickMilliseconds = InNowMilliseconds;
	NextDueMilliseconds = CalculateNextDueMilliseconds(InNowMilliseconds);
	return FTickDecision::Ticked(InNowMilliseconds, 0);
}

bool FTickFunction::IsTickDueNow(const TimePointMilliseconds InNowMilliseconds) const noexcept
{
	if (IntervalMilliseconds == 0)
	{
		return true;
	}
	const bool bBeforeDeadline = InNowMilliseconds < NextDueMilliseconds;
	const bool bAlreadyTickedAtThisTime = InNowMilliseconds == PreviousTickMilliseconds;
	return !(bBeforeDeadline || bAlreadyTickedAtThisTime);
}

FTickDecision FTickFunction::ProduceDueTick(const TimePointMilliseconds InNowMilliseconds) noexcept
{
	const DurationMilliseconds DeltaMilliseconds = CalculateDeltaMilliseconds(InNowMilliseconds);
	PreviousTickMilliseconds = InNowMilliseconds;
	NextDueMilliseconds = CalculateNextDueMilliseconds(InNowMilliseconds);
	return FTickDecision::Ticked(InNowMilliseconds, DeltaMilliseconds);
}

bool FTickFunction::IsEnabled() const noexcept
{
	return bEnabled;
}

DurationMilliseconds FTickFunction::GetInterval() const noexcept
{
	return IntervalMilliseconds;
}

TimePointMilliseconds FTickFunction::CalculateNextDueMilliseconds(const TimePointMilliseconds InNowMilliseconds) const noexcept
{
	const TimePointMilliseconds MaximumTime = std::numeric_limits<TimePointMilliseconds>::max();
	if (MaximumTime - InNowMilliseconds < IntervalMilliseconds)
	{
		return MaximumTime;
	}
	return InNowMilliseconds + IntervalMilliseconds;
}

DurationMilliseconds FTickFunction::CalculateDeltaMilliseconds(const TimePointMilliseconds InNowMilliseconds) const noexcept
{
	const TimePointMilliseconds DeltaMilliseconds = InNowMilliseconds - PreviousTickMilliseconds;
	const TimePointMilliseconds MaximumDuration = std::numeric_limits<DurationMilliseconds>::max();
	if (DeltaMilliseconds > MaximumDuration)
	{
		return std::numeric_limits<DurationMilliseconds>::max();
	}
	return static_cast<DurationMilliseconds>(DeltaMilliseconds);
}

} // namespace MicroWorld::Core
