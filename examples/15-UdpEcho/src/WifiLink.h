#pragma once

// ESP-IDF WiFi bring-up glue (vendor code, not MicroWorld). One board hosts a
// SoftAP; the other joins it as a station, so the two talk directly with no
// router and no real credentials — the SSID/password are fixed demo values.
// Either call must run once, before any FEsp32UdpDriver is constructed, because
// lwIP must exist before a socket is opened.

/**
 * Brings up a SoftAP hosting the given network. Returns true once the AP is
 * running and prints "[<ExampleTag>] wifi ip=192.168.4.1" (the fixed SoftAP
 * gateway the joining board addresses). Echo-server role.
 *
 * @param ExampleTag Serial-trace tag (e.g. "ex15") prefixed to the ip line.
 * @param Ssid Demo network name to host.
 * @param Password Demo WPA2 password (>= 8 chars).
 * @return True when the access point started.
 */
bool StartSoftAccessPoint(const char* ExampleTag, const char* Ssid, const char* Password) noexcept;

/**
 * Joins the given SoftAP as a station, retrying until an IPv4 address is bound
 * (the peer AP may boot after this board). Returns true and prints
 * "[<ExampleTag>] wifi ip=<a.b.c.d>". Probe role.
 *
 * @param ExampleTag Serial-trace tag prefixed to the ip line.
 * @param Ssid Demo network name to join.
 * @param Password Demo WPA2 password.
 * @return True once the station bound an address.
 */
bool JoinAccessPoint(const char* ExampleTag, const char* Ssid, const char* Password) noexcept;
