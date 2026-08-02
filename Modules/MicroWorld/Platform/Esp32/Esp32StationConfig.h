#pragma once

#include <MicroWorld/Core/Time.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the credentials and timeout one board needs to join an existing network as a station.
 * Responsibilities: Hold SSID, passphrase, and the connect timeout bound.
 * Example:
 *   FEsp32StationConfig Config;
 *   Config.Ssid = "MyNet";
 */
struct FEsp32StationConfig
{
	/** Motivation: Network name to join; must be non-null and non-empty. */
	const char* Ssid;

	/** Motivation: Passphrase of the network to join; must be non-null. */
	const char* Password;

	/** Motivation: Upper bound on how long JoinAccessPoint waits for an IPv4 lease before giving up. */
	Core::DurationMilliseconds ConnectTimeoutMilliseconds{15000};
};

} // namespace MicroWorld::Platform::Esp32
