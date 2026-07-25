#include <MicroWorld/PlatformEsp32/Esp32OutputDevice.h>

#include <esp_log.h>

namespace MicroWorld
{

void WriteEsp32LogRecord(const ELogLevel InLevel, const char* const InCategory, const char* const InMessage) noexcept
{
	const char* const Tag = (InCategory != nullptr) ? InCategory : "MicroWorld";
	const char* const Body = (InMessage != nullptr) ? InMessage : "";
	switch (InLevel)
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
