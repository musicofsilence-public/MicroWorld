#pragma once

#include <cstdint>

// Copy this file to NetworkConfig.h and fill in real values. NetworkConfig.h is
// git-ignored (examples/*/src/NetworkConfig.h in the repo .gitignore) so real
// credentials never land in git. The build fails helpfully if you forget to copy.

/** WiFi network both boards join (2.4 GHz — the ESP32-S3 has no 5 GHz radio). */
constexpr const char* kWifiSsid = "YOUR_SSID";

/** WiFi password; never printed to the serial console. */
constexpr const char* kWifiPassword = "YOUR_PASSWORD";

/** UDP port the server binds; the client targets this port at kServerIpv4. */
constexpr std::uint16_t kServerPort = 40404;

/** Server board's IPv4 octets — used by the CLIENT build only. Read them from the
 *  server board's "[ex16] wifi ip=<a.b.c.d>" boot line and fill them in here. */
constexpr std::uint8_t kServerIpv4[4] = {192, 168, 1, 50};
