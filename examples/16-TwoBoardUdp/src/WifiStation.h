#pragma once

/**
 * Joins the configured WiFi as a station and blocks until an IPv4 address is
 * bound or ~15 s elapse. Returns true only when the interface has an address;
 * prints "[<ExampleTag>] wifi ip=<a.b.c.d>" on success. Must be called once,
 * before any FEsp32UdpDriver is constructed (the lwIP stack must exist before a
 * socket is opened). This is ESP-IDF vendor glue, not MicroWorld.
 *
 * On the server board the printed ip is the address the operator copies into
 * the client's NetworkConfig.h (kServerIpv4).
 *
 * @param ExampleTag Serial-trace tag (e.g. "ex16") prefixed to the ip line.
 * @return True when the station bound an IPv4 address within the timeout.
 */
bool ConnectWifiStation(const char* ExampleTag) noexcept;
