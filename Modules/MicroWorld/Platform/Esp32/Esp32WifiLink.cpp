#include <MicroWorld/Platform/Esp32/Esp32WifiLink.h>

#include "Internal/Esp32WifiPlatformImplementation.h"

#include <cstring>

namespace MicroWorld::Platform::Esp32
{

namespace
{

	/** Motivation: WPA2's shortest allowed passphrase length, in characters. */
	constexpr std::size_t MinimumWpa2PasswordLength = 8;

	/** Motivation: One slice of the station join wait; the poll sums these until the timeout budget is spent. */
	constexpr Core::DurationMilliseconds JoinWaitSliceMilliseconds = 100;

	/**
	 * Motivation: Guards StartAccessPoint against an unusable config before any radio call so a rejection is truly
	 *   transactional.
	 * Responsibilities: Return the first reason a SoftAP config cannot be used, or Success.
	 */
	Transport::ETransportResult ValidateAccessPointConfig(const FEsp32AccessPointConfig& InConfig) noexcept
	{
		if (InConfig.Ssid == nullptr || InConfig.Ssid[0] == '\0')
		{
			return Transport::ETransportResult::Invalid;
		}
		if (InConfig.Password == nullptr || std::strlen(InConfig.Password) < MinimumWpa2PasswordLength)
		{
			return Transport::ETransportResult::Invalid;
		}
		return Transport::ETransportResult::Success;
	}

	/**
	 * Motivation: Guards JoinAccessPoint against an unusable config before any radio call so a rejection is truly
	 *   transactional.
	 * Responsibilities: Return the first reason a station config cannot be used, or Success.
	 */
	Transport::ETransportResult ValidateStationConfig(const FEsp32StationConfig& InConfig) noexcept
	{
		if (InConfig.Ssid == nullptr || InConfig.Ssid[0] == '\0')
		{
			return Transport::ETransportResult::Invalid;
		}
		if (InConfig.Password == nullptr)
		{
			return Transport::ETransportResult::Invalid;
		}
		return Transport::ETransportResult::Success;
	}

} // namespace

FEsp32WifiLink::FEsp32WifiLink() noexcept = default;

FEsp32WifiLink::~FEsp32WifiLink() noexcept
{
	Stop();
}

Transport::ETransportResult FEsp32WifiLink::StartAccessPoint(const FEsp32AccessPointConfig& InConfig) noexcept
{
	const Transport::ETransportResult ValidationResult = ValidateAccessPointConfig(InConfig);
	if (ValidationResult != Transport::ETransportResult::Success)
	{
		return ValidationResult;
	}

	if (!InitNetworkStack())
	{
		return Transport::ETransportResult::Unavailable;
	}
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t InitConfig = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&InitConfig) != ESP_OK)
	{
		return Transport::ETransportResult::Unavailable;
	}

	wifi_config_t ApConfig = MakeAccessPointConfig(InConfig.Ssid, InConfig.Password, InConfig.WifiChannel, InConfig.MaxStations);
	if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &ApConfig) != ESP_OK || esp_wifi_start() != ESP_OK)
	{
		return Transport::ETransportResult::Unavailable;
	}

	bIsUp = true;
	return Transport::ETransportResult::Success;
}

Transport::ETransportResult FEsp32WifiLink::JoinAccessPoint(const FEsp32StationConfig& InConfig) noexcept
{
	const Transport::ETransportResult ValidationResult = ValidateStationConfig(InConfig);
	if (ValidationResult != Transport::ETransportResult::Success)
	{
		return ValidationResult;
	}

	if (!InitNetworkStack())
	{
		return Transport::ETransportResult::Unavailable;
	}
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t InitConfig = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&InitConfig) != ESP_OK)
	{
		return Transport::ETransportResult::Unavailable;
	}

	if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &OnStationEvent, nullptr, nullptr) != ESP_OK
		|| esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnStationEvent, nullptr, nullptr) != ESP_OK)
	{
		return Transport::ETransportResult::Unavailable;
	}

	wifi_config_t StaConfig = MakeStationConfig(InConfig.Ssid, InConfig.Password);
	if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &StaConfig) != ESP_OK || esp_wifi_start() != ESP_OK)
	{
		return Transport::ETransportResult::Unavailable;
	}

	// Bounded poll: sleep in fixed 100 ms slices, checking the got-IP flag each slice, until either
	// the flag latches or the accumulated wait reaches the caller's timeout budget. The slice count
	// is the budget; no real clock is read.
	GGotStationIpAddress = false;
	Core::DurationMilliseconds ElapsedMilliseconds = 0;
	while (!GGotStationIpAddress && ElapsedMilliseconds < InConfig.ConnectTimeoutMilliseconds)
	{
		vTaskDelay(pdMS_TO_TICKS(JoinWaitSliceMilliseconds));
		ElapsedMilliseconds += JoinWaitSliceMilliseconds;
	}
	if (!GGotStationIpAddress)
	{
		return Transport::ETransportResult::Unavailable;
	}

	bIsUp = true;
	return Transport::ETransportResult::Success;
}

bool FEsp32WifiLink::IsUp() const noexcept
{
	return bIsUp;
}

void FEsp32WifiLink::Stop() noexcept
{
	if (!bIsUp)
	{
		return;
	}
	// ESP-IDF provides no clean teardown for netif, the default event loop, or NVS, so this
	// facade intentionally leaves them initialized; a later StartAccessPoint/JoinAccessPoint
	// call tolerates their already-initialized state (see Esp32WifiPlatformImplementation.h).
	(void)esp_wifi_stop();
	bIsUp = false;
}

} // namespace MicroWorld::Platform::Esp32
