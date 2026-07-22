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
		return {ERuntimeResult::InvalidLifecycle, false, {}};
	}
	if (NowMilliseconds < LastObservedMilliseconds)
	{
		return {ERuntimeResult::NonMonotonicTime, false, {}};
	}

	LastObservedMilliseconds = NowMilliseconds;
	if (!bEnabled)
	{
		return {ERuntimeResult::Success, false, {}};
	}

	if (bMustResetSchedule)
	{
		bMustResetSchedule = false;
		PreviousTickMilliseconds = NowMilliseconds;
		NextDueMilliseconds = CalculateNextDueMilliseconds(NowMilliseconds);
		return {ERuntimeResult::Success, true, {NowMilliseconds, 0}};
	}

	if (IntervalMilliseconds != 0)
	{
		const bool bBeforeDeadline = NowMilliseconds < NextDueMilliseconds;
		const bool bAlreadyTickedAtThisTime = NowMilliseconds == PreviousTickMilliseconds;
		if (bBeforeDeadline || bAlreadyTickedAtThisTime)
		{
			return {ERuntimeResult::Success, false, {}};
		}
	}

	const DurationMilliseconds DeltaMilliseconds = CalculateDeltaMilliseconds(NowMilliseconds);
	PreviousTickMilliseconds = NowMilliseconds;
	NextDueMilliseconds = CalculateNextDueMilliseconds(NowMilliseconds);
	return {
		ERuntimeResult::Success,
		true,
		{NowMilliseconds, DeltaMilliseconds},
	};
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
