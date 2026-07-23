#pragma once

#include <cstdint>

// Copy this file to NetworkConfig.h and fill in real values. NetworkConfig.h is
// git-ignored (examples/*/src/NetworkConfig.h in the repo .gitignore) so real
// credentials never land in git. The build fails helpfully if you forget to copy.

/** WiFi network the board joins (2.4 GHz — the ESP32-S3 has no 5 GHz radio). */
constexpr const char* kWifiSsid = "YOUR_SSID";

/** WiFi password; never printed to the serial console. */
constexpr const char* kWifiPassword = "YOUR_PASSWORD";

/** UDP port the board binds and the PC EchoClient.py targets. */
constexpr std::uint16_t kServerPort = 40404;
