#pragma once

#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/Time.h>

#include <cstdint>

namespace MicroWorld
{

/** Settings for hosting a SoftAP network (board is the access point). */
struct FEsp32AccessPointConfig
{
	/** Network name the SoftAP advertises; must be non-null and non-empty. */
	const char* Ssid;

	/** WPA2 passphrase; must be non-null and at least 8 characters (WPA2 minimum). */
	const char* Password;

	/** 2.4 GHz channel the SoftAP broadcasts on. */
	std::uint8_t WifiChannel{1};

	/** Largest number of stations the SoftAP admits at once. */
	std::uint8_t MaxStations{2};
};

/** Settings for joining an existing network (board is a station). */
struct FEsp32StationConfig
{
	/** Network name to join; must be non-null and non-empty. */
	const char* Ssid;

	/** Passphrase of the network to join; must be non-null. */
	const char* Password;

	/** Upper bound on how long `JoinAccessPoint` waits for an IPv4 lease before giving up. */
	DurationMilliseconds ConnectTimeoutMilliseconds{15000};
};

/**
 * One-per-firmware WiFi bring-up facade; blocking, startup-time only (D12).
 *
 * Confines every ESP-IDF WiFi/NVS/netif/event-loop identifier behind this class so example
 * firmware brings up WiFi through one MicroWorld call instead of copy-pasted vendor glue. Holds
 * no background polling, thread, or timer: `IsUp()` reports one plain flag this facade's own
 * calls set, never a value another task mutates concurrently.
 */
class FEsp32WifiLink
{
public:
	/** Leaves the link down; no radio or network stack call happens until `StartAccessPoint`/`JoinAccessPoint`. */
	FEsp32WifiLink() noexcept;

	/** Stops the WiFi radio if it is still up. */
	~FEsp32WifiLink() noexcept;

	/** Prevents copying so one link value owns exactly one WiFi radio identity. */
	FEsp32WifiLink(const FEsp32WifiLink&) = delete;

	/** Prevents copying so one link value owns exactly one WiFi radio identity. */
	FEsp32WifiLink& operator=(const FEsp32WifiLink&) = delete;

	/** Prevents moving so the owned WiFi radio identity stays fixed. */
	FEsp32WifiLink(FEsp32WifiLink&&) = delete;

	/** Prevents moving so the owned WiFi radio identity stays fixed. */
	FEsp32WifiLink& operator=(FEsp32WifiLink&&) = delete;

	/**
	 * Brings the board up as a SoftAP, transactionally.
	 *
	 * Validates `Config` before touching the radio: a null/empty SSID or a password shorter than
	 * the WPA2 minimum of 8 characters returns `Invalid` with no network stack or radio call made.
	 * Otherwise brings up NVS/netif/the default event loop (tolerating already-initialized), then
	 * configures and starts the SoftAP. Any ESP-IDF failure returns `Unavailable` and leaves the
	 * link down. This call does not print or log; that is the caller's responsibility.
	 *
	 * @param Config SoftAP SSID, password, channel, and station limit.
	 * @return `Success` once the SoftAP is broadcasting; `Invalid` or `Unavailable` otherwise.
	 */
	ENetResult StartAccessPoint(const FEsp32AccessPointConfig& Config) noexcept;

	/**
	 * Joins an existing network as a station and waits for an IPv4 lease, transactionally.
	 *
	 * Validates `Config` before touching the radio: a null/empty SSID or a null password returns
	 * `Invalid` with no network stack or radio call made. Otherwise brings up NVS/netif/the default
	 * event loop (tolerating already-initialized), starts the station, and waits in fixed 100 ms
	 * slices for an IPv4 lease until `Config.ConnectTimeoutMilliseconds` is spent. Returns `Success`
	 * once the lease arrives, or `Unavailable` once the budget is spent with no lease. Any ESP-IDF
	 * start failure also returns `Unavailable`. This call does not print or log.
	 *
	 * @param Config Target SSID, password, and connect timeout.
	 * @return `Success` once an IPv4 lease is held; `Invalid` or `Unavailable` otherwise.
	 */
	ENetResult JoinAccessPoint(const FEsp32StationConfig& Config) noexcept;

	/** Reports whether a prior `StartAccessPoint`/`JoinAccessPoint` call is still up. */
	bool IsUp() const noexcept;

	/**
	 * Stops the WiFi radio, idempotently.
	 *
	 * A no-op when already down. NVS, netif, and the default event loop are left initialized: ESP-IDF
	 * provides no clean teardown for them, so this facade intentionally leaves them up; the next
	 * `StartAccessPoint`/`JoinAccessPoint` call tolerates their already-initialized state.
	 */
	void Stop() noexcept;

private:
	/** True once a `StartAccessPoint`/`JoinAccessPoint` call has brought the radio up; the only observable state. */
	bool bIsUp{false};
};

} // namespace MicroWorld
