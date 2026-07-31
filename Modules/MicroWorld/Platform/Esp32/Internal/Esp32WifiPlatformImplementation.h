#pragma once

// =============================================================================
// src/Esp32WifiPlatformImplementation.h is the SOLE translation unit that pulls ESP-IDF WiFi,
// NVS, netif, event, and FreeRTOS headers. It is included only by Esp32WifiLink.cpp; a public
// header must never reach it. Every ESP-IDF-typed helper (NVS/netif/event-loop bring-up, the
// station event handler) lives here so Esp32WifiLink.cpp reads one platform-free bring-up path.
// =============================================================================

#include <cstdint>
#include <cstring>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace MicroWorld::Platform::Esp32
{

/**
 * Latches to true once the joining station receives its IPv4 lease.
 *
 * Set only from `OnStationEvent` (running on the event-loop task) and polled from the bounded
 * wait slice in `FEsp32WifiLink::JoinAccessPoint`; `volatile` is enough here because the poll
 * never needs anything stronger than "observe the latest write eventually".
 */
inline volatile bool GGotStationIpAddress = false;

/**
 * Station-role WiFi/IP event handler: connects on start, reconnects on every disconnect, and
 * latches the got-IP flag once a lease arrives.
 *
 * @param InEventBase Event family (`WIFI_EVENT` or `IP_EVENT`).
 * @param InEventId Specific event within the family.
 */
inline void OnStationEvent(void* /*Arg*/, esp_event_base_t InEventBase, std::int32_t InEventId, void* /*EventData*/) noexcept
{
	if (InEventBase == WIFI_EVENT && InEventId == WIFI_EVENT_STA_START)
	{
		(void)esp_wifi_connect();
	}
	else if (InEventBase == WIFI_EVENT && InEventId == WIFI_EVENT_STA_DISCONNECTED)
	{
		(void)esp_wifi_connect();
	}
	else if (InEventBase == IP_EVENT && InEventId == IP_EVENT_STA_GOT_IP)
	{
		GGotStationIpAddress = true;
	}
}

/** Brings up NVS (WiFi PHY calibration store); erases and retries once on a version/space fault. */
inline bool InitNvsFlash() noexcept
{
	esp_err_t Result = nvs_flash_init();
	if (Result == ESP_ERR_NVS_NO_FREE_PAGES || Result == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		(void)nvs_flash_erase();
		Result = nvs_flash_init();
	}
	return Result == ESP_OK;
}

/**
 * Brings up NVS, netif, and the default event loop shared by both WiFi roles.
 *
 * `esp_netif_init()` and `esp_event_loop_create_default()` may already be initialized (`Stop()`
 * leaves them up by design, and a consumer may also initialize them); `ESP_ERR_INVALID_STATE` is
 * tolerated as success for both.
 */
inline bool InitNetworkStack() noexcept
{
	if (!InitNvsFlash())
	{
		return false;
	}
	const esp_err_t NetifResult = esp_netif_init();
	if (NetifResult != ESP_OK && NetifResult != ESP_ERR_INVALID_STATE)
	{
		return false;
	}
	const esp_err_t EventLoopResult = esp_event_loop_create_default();
	return EventLoopResult == ESP_OK || EventLoopResult == ESP_ERR_INVALID_STATE;
}

/**
 * Fills a SoftAP `wifi_config_t` from plain SSID/password/channel/station-limit values.
 *
 * SSID and password are copied with a bounded `strncpy` (bounded by the field size minus one),
 * so an oversize input is truncated rather than overrunning the ESP-IDF struct.
 *
 * @param InSsid Network name to advertise.
 * @param InPassword WPA2 passphrase.
 * @param InWifiChannel 2.4 GHz channel to broadcast on.
 * @param InMaxStations Largest number of stations admitted at once.
 * @return SoftAP configuration ready for `esp_wifi_set_config`.
 */
inline wifi_config_t MakeAccessPointConfig(
	const char* const InSsid, const char* const InPassword, const std::uint8_t InWifiChannel, const std::uint8_t InMaxStations) noexcept
{
	wifi_config_t Config{};
	std::strncpy(reinterpret_cast<char*>(Config.ap.ssid), InSsid, sizeof(Config.ap.ssid) - 1);
	Config.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(InSsid));
	std::strncpy(reinterpret_cast<char*>(Config.ap.password), InPassword, sizeof(Config.ap.password) - 1);
	Config.ap.channel = InWifiChannel;
	Config.ap.max_connection = InMaxStations;
	Config.ap.authmode = WIFI_AUTH_WPA2_PSK;
	Config.ap.pmf_cfg.required = false;
	return Config;
}

/**
 * Fills a station `wifi_config_t` from plain SSID/password values.
 *
 * SSID and password are copied with a bounded `strncpy` (bounded by the field size minus one).
 *
 * @param InSsid Network name to join.
 * @param InPassword Passphrase of the network to join.
 * @return Station configuration ready for `esp_wifi_set_config`.
 */
inline wifi_config_t MakeStationConfig(const char* const InSsid, const char* const InPassword) noexcept
{
	wifi_config_t Config{};
	std::strncpy(reinterpret_cast<char*>(Config.sta.ssid), InSsid, sizeof(Config.sta.ssid) - 1);
	std::strncpy(reinterpret_cast<char*>(Config.sta.password), InPassword, sizeof(Config.sta.password) - 1);
	return Config;
}

} // namespace MicroWorld::Platform::Esp32
