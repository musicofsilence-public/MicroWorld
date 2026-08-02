#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Gives the device one vocabulary for a non-consuming peek that is free of lwIP error codes.
 * Responsibilities: Distinguish a queued datagram, a would-block, and a socket error.
 * Example:
 *   EPeekStatus S = ProbeReadableDatagram(Sock).Status;
 */
enum class EPeekStatus : std::uint8_t
{
	Ready,		///< Motivation: A datagram is queued; BytesReady carries its observed length.
	WouldBlock, ///< Motivation: No datagram is ready right now.
	Error,		///< Motivation: A socket error occurred.
};

} // namespace MicroWorld::Platform::Esp32
