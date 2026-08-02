#pragma once

#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/Time.h>

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

/**
 * Motivation: Confines every ESP-IDF WiFi/NVS/netif/event-loop identifier behind one MicroWorld facade so example
 *   firmware brings up WiFi through one call instead of copy-pasted vendor glue.
 * Responsibilities: Bring up and tear down the radio at startup time only, and report one plain IsUp flag this
 *   facade's own calls set (never a value another task mutates concurrently); hold no background polling, thread,
 *   or timer.
 * Example:
 *   FEsp32WifiLink Link;
 *   if (Link.StartAccessPoint(Config) == Core::ETransportResult::Success) { Link.Stop(); }
 */
class FEsp32WifiLink
{
public:
	/**
	 * Motivation: Leaves the link down so no radio or network stack call happens until the caller explicitly starts it.
	 * Responsibilities: Default-construct an inert link with IsUp false.
	 */
	FEsp32WifiLink() noexcept;

	/**
	 * Motivation: Releases the radio so a link destruction never leaves the radio running.
	 * Responsibilities: Stop the WiFi radio if it is still up.
	 */
	~FEsp32WifiLink() noexcept;

	/**
	 * Motivation: Keeps one link value owning exactly one WiFi radio identity so the radio never aliases.
	 * Responsibilities: Reject copy construction so the link stays the single owner of its radio.
	 */
	FEsp32WifiLink(const FEsp32WifiLink&) = delete;

	/**
	 * Motivation: Keeps one link value owning exactly one WiFi radio identity so the radio never aliases.
	 * Responsibilities: Reject copy assignment so the link stays the single owner of its radio.
	 */
	FEsp32WifiLink& operator=(const FEsp32WifiLink&) = delete;

	/**
	 * Motivation: Keeps the owned WiFi radio identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move construction so the radio identity never relocates.
	 */
	FEsp32WifiLink(FEsp32WifiLink&&) = delete;

	/**
	 * Motivation: Keeps the owned WiFi radio identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move assignment so the radio identity never relocates.
	 */
	FEsp32WifiLink& operator=(FEsp32WifiLink&&) = delete;

	/**
	 * Motivation: Brings the board up as a SoftAP, transactionally, at startup time.
	 * Responsibilities: Validate InConfig before any radio call (Invalid for a null/empty SSID or a passphrase shorter
	 *   than the WPA2 minimum of 8 characters), bring up NVS/netif/the event loop (tolerating already-initialized),
	 *   then configure and start the SoftAP; any ESP-IDF failure returns Unavailable and leaves the link down; never
	 *   print or log.
	 */
	Core::ETransportResult StartAccessPoint(const FEsp32AccessPointConfig& InConfig) noexcept;

	/**
	 * Motivation: Joins an existing network as a station and waits for an IPv4 lease, transactionally, at startup time.
	 * Responsibilities: Validate InConfig before any radio call (Invalid for a null/empty SSID or null password),
	 *   bring up NVS/netif/the event loop (tolerating already-initialized), start the station, and wait in fixed 100 ms
	 *   slices for an IPv4 lease until InConfig.ConnectTimeoutMilliseconds is spent; return Success once the lease
	 *   arrives or Unavailable once the budget is spent; never print or log.
	 */
	Core::ETransportResult JoinAccessPoint(const FEsp32StationConfig& InConfig) noexcept;

	/**
	 * Motivation: Lets a caller gate every op on whether the radio is currently up.
	 * Responsibilities: Report the up flag set by StartAccessPoint/JoinAccessPoint and cleared by Stop.
	 */
	bool IsUp() const noexcept;

	/**
	 * Motivation: Stops the WiFi radio idempotently so a clean shutdown never double-stops.
	 * Responsibilities: No-op when already down; leave NVS, netif, and the default event loop initialized because
	 *   ESP-IDF provides no clean teardown for them, and the next Start tolerates their already-initialized state.
	 */
	void Stop() noexcept;

private:
	/** Motivation: True once a StartAccessPoint/JoinAccessPoint call has brought the radio up; the only observable state. */
	bool bIsUp{false};
};

} // namespace MicroWorld::Platform::Esp32
