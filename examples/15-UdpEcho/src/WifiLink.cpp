#include "WifiLink.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

namespace
{
/** Event-group bit set once the joining station holds an IPv4 lease. */
constexpr EventBits_t StaGotIpBit = BIT0;

/** SoftAP channel; 1 is a safe default in the 2.4 GHz band for a two-board demo. */
constexpr std::uint8_t AccessPointChannel = 1;

/** Peers the SoftAP admits; one client is enough, a little headroom is harmless. */
constexpr std::uint8_t AccessPointMaxConnections = 4;

/** One slice of the station's join wait; it loops until connected so a late AP still joins. */
constexpr std::uint32_t JoinWaitSliceMilliseconds = 15000;

/** Signals the station join wait from the event handler; created before the handlers register. */
EventGroupHandle_t GStationEvents = nullptr;

/**
 * Station-role WiFi/IP events: connect on start, reconnect on every disconnect
 * (retry indefinitely so a peer AP that boots later is still joined), and latch
 * the bound-IP bit for the join wait.
 */
void OnStationEvent(void* /*Arg*/, esp_event_base_t EventBase, int32_t EventId, void* /*EventData*/) noexcept
{
	if (EventBase == WIFI_EVENT && EventId == WIFI_EVENT_STA_START)
	{
		(void)esp_wifi_connect();
	}
	else if (EventBase == WIFI_EVENT && EventId == WIFI_EVENT_STA_DISCONNECTED)
	{
		(void)esp_wifi_connect();
	}
	else if (EventBase == IP_EVENT && EventId == IP_EVENT_STA_GOT_IP)
	{
		xEventGroupSetBits(GStationEvents, StaGotIpBit);
	}
}

/** Brings up NVS (WiFi PHY calibration store); erases and retries once on a version/space fault. */
bool InitNvsFlash() noexcept
{
	esp_err_t Result = nvs_flash_init();
	if (Result == ESP_ERR_NVS_NO_FREE_PAGES || Result == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		(void)nvs_flash_erase();
		Result = nvs_flash_init();
	}
	return Result == ESP_OK;
}

/** Brings up NVS, the lwIP stack, and the default event loop shared by both roles. */
bool InitNetworkStack() noexcept
{
	if (!InitNvsFlash())
	{
		return false;
	}
	return esp_netif_init() == ESP_OK && esp_event_loop_create_default() == ESP_OK;
}

/** Prints the bound IPv4 of the named default netif ("WIFI_AP_DEF" or "WIFI_STA_DEF"). */
void PrintNetifIp(const char* ExampleTag, const char* InterfaceKey) noexcept
{
	esp_netif_t* const Netif = esp_netif_get_handle_from_ifkey(InterfaceKey);
	esp_netif_ip_info_t IpInfo{};
	if (Netif != nullptr && esp_netif_get_ip_info(Netif, &IpInfo) == ESP_OK)
	{
		std::printf("[%s] wifi ip=" IPSTR "\n", ExampleTag, IP2STR(&IpInfo.ip));
	}
}
} // namespace

bool StartSoftAccessPoint(const char* ExampleTag, const char* Ssid, const char* Password) noexcept
{
	if (!InitNetworkStack())
	{
		return false;
	}
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t InitConfig = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&InitConfig) != ESP_OK)
	{
		return false;
	}

	wifi_config_t ApConfig{};
	std::strncpy(reinterpret_cast<char*>(ApConfig.ap.ssid), Ssid, sizeof(ApConfig.ap.ssid) - 1);
	ApConfig.ap.ssid_len = static_cast<std::uint8_t>(std::strlen(Ssid));
	std::strncpy(reinterpret_cast<char*>(ApConfig.ap.password), Password, sizeof(ApConfig.ap.password) - 1);
	ApConfig.ap.channel = AccessPointChannel;
	ApConfig.ap.max_connection = AccessPointMaxConnections;
	ApConfig.ap.authmode = WIFI_AUTH_WPA2_PSK;
	ApConfig.ap.pmf_cfg.required = false;

	if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &ApConfig) != ESP_OK || esp_wifi_start() != ESP_OK)
	{
		return false;
	}
	PrintNetifIp(ExampleTag, "WIFI_AP_DEF");
	return true;
}

bool JoinAccessPoint(const char* ExampleTag, const char* Ssid, const char* Password) noexcept
{
	if (!InitNetworkStack())
	{
		return false;
	}
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t InitConfig = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&InitConfig) != ESP_OK)
	{
		return false;
	}

	GStationEvents = xEventGroupCreate();
	if (GStationEvents == nullptr)
	{
		return false;
	}
	if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &OnStationEvent, nullptr, nullptr) != ESP_OK
		|| esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnStationEvent, nullptr, nullptr) != ESP_OK)
	{
		return false;
	}

	wifi_config_t StaConfig{};
	std::strncpy(reinterpret_cast<char*>(StaConfig.sta.ssid), Ssid, sizeof(StaConfig.sta.ssid) - 1);
	std::strncpy(reinterpret_cast<char*>(StaConfig.sta.password), Password, sizeof(StaConfig.sta.password) - 1);

	if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &StaConfig) != ESP_OK || esp_wifi_start() != ESP_OK)
	{
		return false;
	}

	// Retry-forever join: the peer AP may boot after this board, so wait in 15 s
	// slices (each yields to the idle task) until the station binds an address.
	for (;;)
	{
		const EventBits_t Bits = xEventGroupWaitBits(GStationEvents, StaGotIpBit, pdFALSE, pdFALSE, pdMS_TO_TICKS(JoinWaitSliceMilliseconds));
		if ((Bits & StaGotIpBit) != 0)
		{
			break;
		}
		std::printf("[%s] waiting for AP '%s'...\n", ExampleTag, Ssid);
	}
	PrintNetifIp(ExampleTag, "WIFI_STA_DEF");
	return true;
}
