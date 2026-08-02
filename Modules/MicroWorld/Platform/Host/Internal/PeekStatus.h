#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Host
{

/**
 * Motivation: Gives the sizing peek one platform-free vocabulary for what it observed at the head of the queue.
 * Responsibilities: Distinguish a queued datagram from an empty queue and a socket error.
 * Example:
 *   if (ProbeReadableDatagram(Socket).Status == EPeekStatus::Ready) { Consume(); }
 */
enum class EPeekStatus : std::uint8_t
{
	Ready,		///< Motivation: A datagram is queued; BytesReady carries its true length.
	WouldBlock, ///< Motivation: No datagram is ready right now.
	Error,		///< Motivation: A socket error occurred.
};

} // namespace MicroWorld::Platform::Host
