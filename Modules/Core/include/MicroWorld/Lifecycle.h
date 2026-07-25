#pragma once

#include <MicroWorld/Time.h>

namespace MicroWorld
{

/** The phases an application can be in; it never returns to an earlier one. */
enum class ELifecycleState : std::uint8_t
{
	Constructed, ///< Built but not started; Begin may still be called once.
	Playing,	 ///< Started; updates run and End may be called once.
	Failed,		 ///< Begin failed; nothing runs again and there is no way back.
	Ended,		 ///< Stopped cleanly; a further End succeeds without doing anything.
};

/**
 * Tracks the current phase and rejects any illegal move between phases.
 *
 * Pure state: it calls no hooks and touches no hardware, so an owner can ask what
 * is legal before doing any real work.
 */
class FLifecycleGuard final
{
public:
	/** Moves Constructed to Playing; fails from any other phase. */
	ERuntimeResult Begin() noexcept
	{
		if (CurrentState != ELifecycleState::Constructed)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		CurrentState = ELifecycleState::Playing;
		return ERuntimeResult::Success;
	}

	/** Succeeds only while Playing, so a caller can guard work in one line. */
	ERuntimeResult RequirePlaying() const noexcept
	{
		return CurrentState == ELifecycleState::Playing ? ERuntimeResult::Success : ERuntimeResult::InvalidLifecycle;
	}

	/** Marks the lifecycle dead for good; nothing recovers from Failed. */
	void Fail() noexcept { CurrentState = ELifecycleState::Failed; }

	/** Moves Playing to Ended; a second call succeeds without doing anything. */
	ERuntimeResult End() noexcept
	{
		if (CurrentState == ELifecycleState::Ended)
		{
			return ERuntimeResult::Success;
		}
		if (CurrentState != ELifecycleState::Playing)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		CurrentState = ELifecycleState::Ended;
		return ERuntimeResult::Success;
	}

	/** Reports the current phase for owners that branch on it. */
	ELifecycleState GetState() const noexcept { return CurrentState; }

private:
	/** One enum instead of several bool flags, so no impossible combination exists. */
	ELifecycleState CurrentState{ELifecycleState::Constructed};
};

} // namespace MicroWorld
