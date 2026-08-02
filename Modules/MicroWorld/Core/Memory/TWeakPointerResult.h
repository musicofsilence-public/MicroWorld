#pragma once

#include <MicroWorld/Core/Memory/ESharedPointerResult.h>
#include <MicroWorld/Core/Memory/WeakPtr.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Couples a weak-pointer operation outcome with the observer it acquired.
 * Responsibilities: Carry the result and one weak observer that is valid only when Result is Success.
 * Example:
 *   TWeakPointerResult<int> Weak = Owner.TryAcquireWeak();
 */
template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
struct TWeakPointerResult
{
	/** Motivation: Distinguishes acquisition success from expiry and counter overflow. */
	ESharedPointerResult Result{ESharedPointerResult::Expired};

	/** Motivation: Owns one weak count only when Result is Success. */
	TWeakPtr<ValueType, Mode> Pointer{};
};

} // namespace MicroWorld::Core

// The out-of-line member templates (TryShare, TryAcquireWeak, TryObserve, TryAcquireStrong, Pin, MakeShared) are
// defined here so that any consumer of a weak-pointer operation also receives its instantiation prerequisites.
#include <MicroWorld/Core/Memory/TSharedPointerDefinitions.h>
