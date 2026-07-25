#include <MicroWorld/PlatformEsp32/Esp32WifiLink.h>

#include "Esp32WifiPlatformImplementation.h"

#include <cstring>

namespace MicroWorld
{

namespace
{

	/** WPA2's shortest allowed passphrase length, in characters. */
	constexpr std::size_t MinimumWpa2PasswordLength = 8;

	/** One slice of the station join wait; the poll sums these until the timeout budget is spent. */
	constexpr DurationMilliseconds JoinWaitSliceMilliseconds = 100;

	/** Reports the first reason a SoftAP config cannot be used, or `Success`. */
	ENetResult ValidateAccessPointConfig(const FEsp32AccessPointConfig& InConfig) noexcept
	{
		if (InConfig.Ssid == nullptr || InConfig.Ssid[0] == '\0')
		{
			return ENetResult::Invalid;
		}
		if (InConfig.Password == nullptr || std::strlen(InConfig.Password) < MinimumWpa2PasswordLength)
		{
			return ENetResult::Invalid;
		}
		return ENetResult::Success;
	}

	/** Reports the first reason a station config cannot be used, or `Success`. */
	ENetResult ValidateStationConfig(const FEsp32StationConfig& InConfig) noexcept
	{
		if (InConfig.Ssid == nullptr || InConfig.Ssid[0] == '\0')
		{
			return ENetResult::Invalid;
		}
		if (InConfig.Password == nullptr)
		{
			return ENetResult::Invalid;
		}
		return ENetResult::Success;
	}

} // namespace

FEsp32WifiLink::FEsp32WifiLink() noexcept = default;

FEsp32WifiLink::~FEsp32WifiLink() noexcept
{
	Stop();
}

ENetResult FEsp32WifiLink::StartAccessPoint(const FEsp32AccessPointConfig& InConfig) noexcept
{
	const ENetResult ValidationResult = ValidateAccessPointConfig(InConfig);
	if (ValidationResult != ENetResult::Success)
	{
		return ValidationResult;
	}

	if (!Detail::InitNetworkStack())
	{
		return ENetResult::Unavailable;
	}
	esp_netif_create_default_wifi_ap();

	wifi_init_config_t InitConfig = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&InitConfig) != ESP_OK)
	{
		return ENetResult::Unavailable;
	}

	wifi_config_t ApConfig = Detail::MakeAccessPointConfig(InConfig.Ssid, InConfig.Password, InConfig.WifiChannel, InConfig.MaxStations);
	if (esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &ApConfig) != ESP_OK || esp_wifi_start() != ESP_OK)
	{
		return ENetResult::Unavailable;
	}

	bIsUp = true;
	return ENetResult::Success;
}

ENetResult FEsp32WifiLink::JoinAccessPoint(const FEsp32StationConfig& InConfig) noexcept
{
	const ENetResult ValidationResult = ValidateStationConfig(InConfig);
	if (ValidationResult != ENetResult::Success)
	{
		return ValidationResult;
	}

	if (!Detail::InitNetworkStack())
	{
		return ENetResult::Unavailable;
	}
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t InitConfig = WIFI_INIT_CONFIG_DEFAULT();
	if (esp_wifi_init(&InitConfig) != ESP_OK)
	{
		return ENetResult::Unavailable;
	}

	if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &Detail::OnStationEvent, nullptr, nullptr) != ESP_OK
		|| esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &Detail::OnStationEvent, nullptr, nullptr) != ESP_OK)
	{
		return ENetResult::Unavailable;
	}

	wifi_config_t StaConfig = Detail::MakeStationConfig(InConfig.Ssid, InConfig.Password);
	if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK || esp_wifi_set_config(WIFI_IF_STA, &StaConfig) != ESP_OK || esp_wifi_start() != ESP_OK)
	{
		return ENetResult::Unavailable;
	}

	// Bounded poll: sleep in fixed 100 ms slices, checking the got-IP flag each slice, until either
	// the flag latches or the accumulated wait reaches the caller's timeout budget. The slice count
	// is the budget; no real clock is read.
	Detail::GGotStationIpAddress = false;
	DurationMilliseconds ElapsedMilliseconds = 0;
	while (!Detail::GGotStationIpAddress && ElapsedMilliseconds < InConfig.ConnectTimeoutMilliseconds)
	{
		vTaskDelay(pdMS_TO_TICKS(JoinWaitSliceMilliseconds));
		ElapsedMilliseconds += JoinWaitSliceMilliseconds;
	}
	if (!Detail::GGotStationIpAddress)
	{
		return ENetResult::Unavailable;
	}

	bIsUp = true;
	return ENetResult::Success;
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

} // namespace MicroWorld
