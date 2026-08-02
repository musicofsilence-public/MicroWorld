#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives one application a single, irrevocable phase so every subsystem can ask
 *   "is work legal right now?" without each re-deriving it from its own flags.
 * Responsibilities: Name the phase and never let it move backward; carry no behavior beyond
 *   the value it holds.
 * Example:
 *   ELifecycleState Phase = ELifecycleState::Constructed;
 *   if (Phase == ELifecycleState::Playing) { RunFrame(); }
 */
enum class ELifecycleState : std::uint8_t
{
	Constructed, ///< Motivation: Built but not started; Begin may still be called once.
	Playing,	 ///< Motivation: Started; updates run and End may be called once.
	Failed,		 ///< Motivation: Begin failed; nothing runs again and there is no way back.
	Ended,		 ///< Motivation: Stopped cleanly; a further End succeeds without doing anything.
};

} // namespace MicroWorld::Core
