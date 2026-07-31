#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>

#include <esp_log.h>

namespace MicroWorld::Platform::Esp32
{

void WriteEsp32LogRecord(const Core::ELogLevel InLevel, const char* const InCategory, const char* const InMessage) noexcept
{
	const char* const Tag = (InCategory != nullptr) ? InCategory : "MicroWorld";
	const char* const Body = (InMessage != nullptr) ? InMessage : "";
	switch (InLevel)
	{
		case Core::ELogLevel::Error:
			ESP_LOGE(Tag, "%s", Body);
			break;
		case Core::ELogLevel::Warning:
			ESP_LOGW(Tag, "%s", Body);
			break;
		case Core::ELogLevel::Log:
			ESP_LOGI(Tag, "%s", Body);
			break;
		case Core::ELogLevel::Verbose:
			ESP_LOGV(Tag, "%s", Body);
			break;
	}
}

} // namespace MicroWorld::Platform::Esp32
