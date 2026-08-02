#pragma once

#include <cstdint>
#include <limits>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets a caller carry one deferred-spawn request as a generation-checked pair without exposing request
 *   storage.
 * Responsibilities: Hold index and generation and report validity; a handle is local to the storage that issued it.
 * Example:
 *   FActorSpawnHandle Handle = Storage.Reserve();
 *   if (Handle.IsValid()) { Storage.Activate(Handle, Ops); }
 */
struct FActorSpawnHandle final
{
	/** Motivation: Reserves the final index value as an invalid request slot. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Motivation: Selects caller-owned request storage, or InvalidIndex when no request exists. */
	std::uint16_t Index{InvalidIndex};

	/** Motivation: Distinguishes each reuse of the same fixed request slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning storage.
	 * Responsibilities: Report true only when index and generation together look like an issued request.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }
};

} // namespace MicroWorld::Engine
