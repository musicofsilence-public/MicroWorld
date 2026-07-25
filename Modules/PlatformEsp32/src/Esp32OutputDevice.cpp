#include <MicroWorld/PlatformEsp32/Esp32OutputDevice.h>

#include <esp_log.h>

namespace MicroWorld
{

void WriteEsp32LogRecord(const ELogLevel Level, const char* const Category, const char* const Message) noexcept
{
	const char* const Tag = (Category != nullptr) ? Category : "MicroWorld";
	const char* const Body = (Message != nullptr) ? Message : "";
	switch (Level)
	{
		case ELogLevel::Error:
			ESP_LOGE(Tag, "%s", Body);
			break;
		case ELogLevel::Warning:
			ESP_LOGW(Tag, "%s", Body);
			break;
		case ELogLevel::Log:
			ESP_LOGI(Tag, "%s", Body);
			break;
		case ELogLevel::Verbose:
			ESP_LOGV(Tag, "%s", Body);
			break;
	}
}

} // namespace MicroWorld
