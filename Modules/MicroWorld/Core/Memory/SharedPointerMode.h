#pragma once

#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Names the only reference-counting execution contract the implementation currently supports.
 * Responsibilities: Distinguish the single-threaded contract so callers know all ownership operations share one thread.
 * Example:
 *   TSharedPtr<int, ESharedPointerMode::SingleThreaded> Owner;
 */
enum class ESharedPointerMode : std::uint8_t
{
	/** Motivation: Requires all ownership operations to execute from one caller-controlled thread. */
	SingleThreaded,
};

} // namespace MicroWorld::Core
