#pragma once

#include <MicroWorld/Log.h>

namespace MicroWorld
{

/**
 * Forwards one MicroWorld log record to the ESP-IDF logging facility.
 *
 * Maps `ELogLevel` to the matching `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`/`ESP_LOGV`
 * emitter, using `Category` as the ESP-IDF tag and `Message` as the literal
 * body. Install it once at startup with `SetLogSink(&Esp32LogSink)` so every
 * `MW_LOG` call site that survives the compile-time floor routes through ESP-IDF.
 *
 * @param Level Severity rank selecting the ESP-IDF emitter.
 * @param Category ESP-IDF tag printed with the record.
 * @param Message Fully formed record body to forward verbatim.
 */
void Esp32LogSink(ELogLevel Level, const char* Category, const char* Message) noexcept;

} // namespace MicroWorld
