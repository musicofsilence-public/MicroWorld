#pragma once

#include <cstddef>

namespace MicroWorld::Core
{

/**
 * Motivation: Carries the byte count produced by one non-blocking receive so a caller distinguishes a short successful
 *   read from a failed one without inspecting destination bytes.
 * Responsibilities: Report bytes written only on Success and remain unchanged on every non-success result.
 * Example:
 *   FReceiveResult Result;
 *   if (Device.TryReceive(From, Destination, Result) == ETransportResult::Success) { Use(Result.BytesReceived); }
 */
struct FReceiveResult
{
	/** Motivation: Counts bytes written to the caller-owned destination, set only on Success. */
	std::size_t BytesReceived{0};
};

} // namespace MicroWorld::Core
