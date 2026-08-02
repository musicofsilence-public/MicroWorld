#pragma once

#include <MicroWorld/Core/LifecycleState.h>
#include <MicroWorld/Core/Time.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Lets an owner treat "is framed work legal right now?" as one query instead of
 *   scattering phase checks across every subsystem that might tick or render.
 * Responsibilities: Track the current phase and reject any move that skips or rewinds it,
 *   without calling hooks or touching hardware, so an owner can ask what is legal before
 *   doing any real work.
 * Example:
 *   FLifecycleGuard Guard;
 *   if (Guard.Begin() == ERuntimeResult::Success) { RunFrame(); }
 */
class FLifecycleGuard final
{
public:
	/**
	 * Motivation: Lets an owner start framed work exactly once from a clean construct.
	 * Responsibilities: Move Constructed to Playing and refuse from any other phase.
	 */
	ERuntimeResult Begin() noexcept
	{
		if (CurrentState != ELifecycleState::Constructed)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		CurrentState = ELifecycleState::Playing;
		return ERuntimeResult::Success;
	}

	/**
	 * Motivation: Lets a caller guard one piece of framed work behind a single readable check.
	 * Responsibilities: Succeed only while Playing, so a non-success result means stop now.
	 */
	ERuntimeResult RequirePlaying() const noexcept
	{
		return CurrentState == ELifecycleState::Playing ? ERuntimeResult::Success : ERuntimeResult::InvalidLifecycle;
	}

	/**
	 * Motivation: Lets an owner mark the lifecycle permanently dead after a fatal failure.
	 * Responsibilities: Move to Failed; nothing recovers from it.
	 */
	void Fail() noexcept { CurrentState = ELifecycleState::Failed; }

	/**
	 * Motivation: Lets an owner stop framed work cleanly and idempotently on shutdown.
	 * Responsibilities: Move Playing to Ended; a second call succeeds without doing anything.
	 */
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

	/**
	 * Motivation: Lets owners branch on the current phase without exposing mutation.
	 * Responsibilities: Report the stored phase and nothing else.
	 */
	ELifecycleState GetState() const noexcept { return CurrentState; }

private:
	/** Motivation: One enum instead of several bool flags, so no impossible combination exists. */
	ELifecycleState CurrentState{ELifecycleState::Constructed};
};

} // namespace MicroWorld::Core
