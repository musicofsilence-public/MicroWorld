#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Shared demo-network config and probe constants for example 15's two roles
 * (EchoServerMain.cpp, ProbeMain.cpp). Defined once here — DRY within this
 * example. The SoftAP values are fixed demo-only strings, not a secret, so they
 * commit safely; no home router and no real credentials are involved.
 */
namespace Ex15
{
/** SoftAP the echo-server board hosts and the probe board joins. */
constexpr const char* DemoApSsid = "microworld-ex15";

/** Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** The echo server's fixed SoftAP gateway IPv4; the probe addresses this. */
constexpr std::uint8_t EchoServerIpv4[4] = {192, 168, 4, 1};

/** UDP port the echo server binds and the probe targets. */
constexpr std::uint16_t EchoServerPort = 40404;

/** Normal payload the probe sends through the driver and expects echoed byte-for-byte. */
constexpr char NormalPayload[] = "hello microworld";

/**
 * Oversize payload length the probe sends via a RAW socket. It exceeds the
 * driver's UdpMaxPacketBytes (1200) so the echo server's receive path reports
 * the oversize outcome, yet stays under the ~1472-byte single-frame UDP limit so
 * no IP fragmentation clouds the result. The driver itself cannot send this
 * (TrySend rejects > 1200 as Invalid), which is exactly why a raw socket is used.
 */
constexpr std::size_t OversizePayloadBytes = 1300;

/** Poll pace so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 20;
} // namespace Ex15
