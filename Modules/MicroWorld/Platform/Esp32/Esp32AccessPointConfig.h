#pragma once

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the SoftAP settings one board needs to host a network as the access point.
 * Responsibilities: Hold SSID, WPA2 passphrase, channel, and station limit; SSID must be non-empty and the
 *   passphrase at least the WPA2 minimum.
 * Example:
 *   FEsp32AccessPointConfig Config;
 *   Config.Ssid = "MyAp";
 */
struct FEsp32AccessPointConfig
{
	/** Motivation: Network name the SoftAP advertises; must be non-null and non-empty. */
	const char* Ssid;

	/** Motivation: WPA2 passphrase; must be non-null and at least 8 characters (WPA2 minimum). */
	const char* Password;

	/** Motivation: 2.4 GHz channel the SoftAP broadcasts on. */
	std::uint8_t WifiChannel{1};

	/** Motivation: Largest number of stations the SoftAP admits at once. */
	std::uint8_t MaxStations{2};
};

} // namespace MicroWorld::Platform::Esp32
