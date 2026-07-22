#pragma once

#include <MicroWorld/Time.h>

namespace MicroWorld
{

/** Identifies the only legal lifecycle phases for a runtime object. */
enum class ELifecycleState : std::uint8_t
{
	Constructed, ///< Allows registration and one future begin transition.
	Playing,	 ///< Allows updates and the one legal end transition.
	Failed,		 ///< Makes partial begin failure terminal and non-dispatchable.
	Ended,		 ///< Makes successful shutdown observable and idempotent.
};

/** Validates lifecycle transitions without invoking hooks or platform code. */
class FLifecycleGuard final
{
public:
	/** Enters play exactly once from the constructed state. */
	ERuntimeResult Begin() noexcept
	{
		if (CurrentState != ELifecycleState::Constructed)
		{
			return ERuntimeResult::InvalidLifecycle;
		}
		CurrentState = ELifecycleState::Playing;
		return ERuntimeResult::Success;
	}

	/** Confirms that dispatch is legal in the current state. */
	ERuntimeResult RequirePlaying() const noexcept
	{
		return CurrentState == ELifecycleState::Playing ? ERuntimeResult::Success : ERuntimeResult::InvalidLifecycle;
	}

	/** Makes a failed lifecycle terminal. */
	void Fail() noexcept { CurrentState = ELifecycleState::Failed; }

	/** Leaves play exactly once; ending an ended lifecycle is idempotent. */
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

	/** Exposes lifecycle state so owners can guard registration and idempotence. */
	ELifecycleState State() const noexcept { return CurrentState; }

private:
	/** Centralizes the forward-only invariant instead of scattering boolean flags. */
	ELifecycleState CurrentState{ELifecycleState::Constructed};
};

} // namespace MicroWorld
