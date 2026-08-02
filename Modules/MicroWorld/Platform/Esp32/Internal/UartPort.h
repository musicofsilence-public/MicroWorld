#pragma once

// =============================================================================
// These Esp32-platform internal headers are the ONLY units that pull ESP-IDF
// UART headers. A public header must never reach them.
// They hide ESP-IDF UART divergence behind shared open/read/write/close helpers
// so platform adapters keep public headers free of vendor types. Example 18's
// two-board ping-pong runtime-verifies the wired-UART full-write and one-byte-drain
// paths on ESP32-S3 (2026-07-23). The short-write would-block mapping remains
// unexercised because that checkpoint never saturates the TX FIFO. See ../AGENTS.md
// for the rule this comment satisfies.
// =============================================================================

#include <driver/uart.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Names the ESP-IDF UART port number type so call sites need no implicit conversion. */
using FUartPort = uart_port_t;

/**
 * Motivation: Restores the ESP-IDF UART port type from the opaque stored port number so the public header never
 *   carries the ESP-IDF enum.
 * Responsibilities: Reinterpret one opaque port number to its ESP-IDF UART port type where the syscalls expect it.
 */
inline FUartPort AsUartPort(const std::int32_t InStored) noexcept
{
	return static_cast<FUartPort>(InStored);
}

} // namespace MicroWorld::Platform::Esp32
