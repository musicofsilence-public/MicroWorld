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

/** Motivation: Latches true once the joining station receives its IPv4 lease, polled by the bounded wait in JoinAccessPoint; volatile is enough
 * because the poll only needs to observe the latest write eventually. */
inline volatile bool GGotStationIpAddress = false;

/**
 * Motivation: Drives the station-role WiFi/IP lifecycle from the ESP-IDF event loop so JoinAccessPoint can wait on a
 *   simple flag instead of registering its own callbacks.
 * Responsibilities: Connect on start, reconnect on every disconnect, and latch the got-IP flag once a lease arrives.
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

/**
 * Motivation: Brings up NVS (the WiFi PHY calibration store) behind one helper so the bring-up path tolerates a
 *   version or space fault.
 * Responsibilities: Erase and retry once on a version/space fault, then report whether NVS is usable.
 */
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
 * Motivation: Brings up the shared NVS/netif/event-loop stack both WiFi roles need behind one helper.
 * Responsibilities: Tolerate ESP_ERR_INVALID_STATE for netif and the default event loop because Stop leaves them up by
 *   design and a consumer may also initialize them.
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
 * Motivation: Fills a SoftAP wifi_config_t from plain values so the facade never hand-builds the ESP-IDF struct.
 * Responsibilities: Copy SSID and password with a bounded strncpy (field size minus one) so an oversize input is
 *   truncated rather than overrunning the struct.
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
 * Motivation: Fills a station wifi_config_t from plain values so the facade never hand-builds the ESP-IDF struct.
 * Responsibilities: Copy SSID and password with a bounded strncpy (field size minus one) so an oversize input is
 *   truncated rather than overrunning the struct.
 */
inline wifi_config_t MakeStationConfig(const char* const InSsid, const char* const InPassword) noexcept
{
	wifi_config_t Config{};
	std::strncpy(reinterpret_cast<char*>(Config.sta.ssid), InSsid, sizeof(Config.sta.ssid) - 1);
	std::strncpy(reinterpret_cast<char*>(Config.sta.password), InPassword, sizeof(Config.sta.password) - 1);
	return Config;
}

} // namespace MicroWorld::Platform::Esp32
