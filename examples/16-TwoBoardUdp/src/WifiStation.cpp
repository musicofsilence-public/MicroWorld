#include "WifiStation.h"

#include "NetworkConfig.h"

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
/** Event-group bit set once the station holds an IPv4 lease; the connect wait blocks on it. */
constexpr EventBits_t GotIpBit = BIT0;

/** Event-group bit set when association fails terminally, so the wait returns instead of hanging. */
constexpr EventBits_t FailedBit = BIT1;

/** Retries before the wait fails, so a wrong password cannot reconnect-loop forever. */
constexpr int MaxConnectRetries = 10;

/** Upper bound on the whole connect wait; a 5 GHz-only or absent SSID fails cleanly here. */
constexpr std::uint32_t ConnectTimeoutMilliseconds = 15000;

/** Signals the connect wait from the event handler; created before the handlers register. */
EventGroupHandle_t GWifiEvents = nullptr;

/** Count of connect retries so far; reset to zero once an IP is bound. */
int GRetryCount = 0;

/**
 * Routes WiFi/IP events to the event group: starts the first connect, retries a
 * bounded number of disconnects, and latches the bound-IP / terminal-failure bits.
 */
void OnWifiEvent(void* /*Arg*/, esp_event_base_t EventBase, int32_t EventId, void* /*EventData*/) noexcept
{
	if (EventBase == WIFI_EVENT && EventId == WIFI_EVENT_STA_START)
	{
		(void)esp_wifi_connect();
	}
	else if (EventBase == WIFI_EVENT && EventId == WIFI_EVENT_STA_DISCONNECTED)
	{
		if (GRetryCount < MaxConnectRetries)
		{
			++GRetryCount;
			(void)esp_wifi_connect();
		}
		else
		{
			xEventGroupSetBits(GWifiEvents, FailedBit);
		}
	}
	else if (EventBase == IP_EVENT && EventId == IP_EVENT_STA_GOT_IP)
	{
		GRetryCount = 0;
		xEventGroupSetBits(GWifiEvents, GotIpBit);
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

/** Prints the station's bound IPv4 once it is up, so the operator can address the board. */
void PrintBoundIp(const char* ExampleTag) noexcept
{
	esp_netif_t* const StaNetif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	esp_netif_ip_info_t IpInfo{};
	if (StaNetif != nullptr && esp_netif_get_ip_info(StaNetif, &IpInfo) == ESP_OK)
	{
		std::printf("[%s] wifi ip=" IPSTR "\n", ExampleTag, IP2STR(&IpInfo.ip));
	}
}
} // namespace

bool ConnectWifiStation(const char* ExampleTag) noexcept
{
	if (!InitNvsFlash())
	{
		return false;
	}
	if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK)
	{
		return false;
	}
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t InitConfig = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&InitConfig) != ESP_OK)
	{
		return false;
	}

	GWifiEvents = xEventGroupCreate();
	if (GWifiEvents == nullptr)
	{
		return false;
	}
	if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &OnWifiEvent, nullptr, nullptr) != ESP_OK
		|| esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnWifiEvent, nullptr, nullptr) != ESP_OK)
	{
		return false;
	}

	// Zero-init leaves the auth threshold at OPEN, so the station accepts whatever
	// the AP offers; only the SSID and password are set from the git-ignored config.
	wifi_config_t StaConfig{};
	std::strncpy(reinterpret_cast<char*>(StaConfig.sta.ssid), kWifiSsid, sizeof(StaConfig.sta.ssid) - 1);
	std::strncpy(reinterpret_cast<char*>(StaConfig.sta.password), kWifiPassword, sizeof(StaConfig.sta.password) - 1);

	if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &StaConfig) != ESP_OK || esp_wifi_start() != ESP_OK)
	{
		return false;
	}

	const EventBits_t Bits = xEventGroupWaitBits(GWifiEvents, GotIpBit | FailedBit, pdFALSE, pdFALSE, pdMS_TO_TICKS(ConnectTimeoutMilliseconds));
	if ((Bits & GotIpBit) == 0)
	{
		return false;
	}
	PrintBoundIp(ExampleTag);
	return true;
}
