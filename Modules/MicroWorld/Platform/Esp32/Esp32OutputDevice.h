#pragma once

#include <MicroWorld/Core/Log.h>

namespace MicroWorld
{

/**
 * Forwards one MicroWorld log record to the ESP-IDF logging facility.
 *
 * Maps `ELogLevel` to the matching `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`/`ESP_LOGV`
 * emitter, using `InCategory` as the ESP-IDF tag and `InMessage` as the literal
 * body. Install it once at startup with `SetOutputDevice(&WriteEsp32LogRecord)` so every
 * `MW_LOG` call site that survives the compile-time floor routes through ESP-IDF.
 *
 * @param InLevel Severity rank selecting the ESP-IDF emitter.
 * @param InCategory ESP-IDF tag printed with the record.
 * @param InMessage Fully formed record body to forward verbatim.
 */
void WriteEsp32LogRecord(ELogLevel InLevel, const char* InCategory, const char* InMessage) noexcept;

} // namespace MicroWorld
