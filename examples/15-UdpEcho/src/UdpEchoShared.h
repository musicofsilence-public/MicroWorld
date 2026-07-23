#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Shared demo-network config for example 15's single echo role (Main.cpp).
 * Defined once here — DRY within this example. The SoftAP values are fixed
 * demo-only strings, not a secret, so they commit safely; no home router and
 * no real credentials are involved.
 */
namespace Ex15
{
/** SoftAP this board hosts. */
constexpr const char* DemoApSsid = "microworld-ex15";

/** Demo WPA2 password (>= 8 chars); throwaway, never a real network's password. */
constexpr const char* DemoApPassword = "microworld";

/** UDP port the echo server binds. */
constexpr std::uint16_t EchoServerPort = 40404;

/** Poll pace so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 20;
} // namespace Ex15
