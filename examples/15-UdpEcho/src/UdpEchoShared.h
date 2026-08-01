#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Motivation: Holds one demo-network config for example 15's single echo role (Main.cpp),
 *   so the SoftAP values are defined once. These are fixed demo-only strings, not a
 *   secret, so they commit safely; no home router and no real credentials are involved.
 */
namespace Ex15
{
/** Motivation: SoftAP this board hosts. */
constexpr const char* DemoApSsid = "microworld-ex15";

/** Motivation: Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** Motivation: UDP port the echo server binds. */
constexpr std::uint16_t EchoServerPort = 40404;

/** Motivation: Poll pace so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 20;
} // namespace Ex15
