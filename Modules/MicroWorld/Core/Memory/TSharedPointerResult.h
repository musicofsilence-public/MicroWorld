#pragma once

#include <MicroWorld/Core/Memory/ESharedPointerResult.h>
#include <MicroWorld/Core/Memory/SharedPtr.h>

namespace MicroWorld::Core
{

/**
 * Motivation: Couples a shared-pointer operation outcome with the strong owner it acquired.
 * Responsibilities: Carry the result and one strong owner that is valid only when Result is Success.
 * Example:
 *   TSharedPointerResult<int> Shared = Owner.TryShare();
 */
template<typename ValueType, ESharedPointerMode Mode = ESharedPointerMode::SingleThreaded>
struct TSharedPointerResult
{
	/** Motivation: Distinguishes acquisition success from allocation, expiry, and overflow. */
	ESharedPointerResult Result{ESharedPointerResult::OutOfMemory};

	/** Motivation: Owns one strong count only when Result is Success. */
	TSharedPtr<ValueType, Mode> Pointer{};
};

} // namespace MicroWorld::Core
