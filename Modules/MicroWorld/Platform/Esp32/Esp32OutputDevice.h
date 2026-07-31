#pragma once

#include <MicroWorld/Core/Log.h>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Routes one MicroWorld log record through the ESP-IDF logging facility so firmware output appears on the
 *   board's console without each call site reaching for vendor headers.
 * Responsibilities: Map Core::ELogLevel to the matching ESP_LOG emitter, using InCategory as the ESP-IDF tag and
 *   InMessage as the literal body.
 */
void WriteEsp32LogRecord(Core::ELogLevel InLevel, const char* InCategory, const char* InMessage) noexcept;

} // namespace MicroWorld::Platform::Esp32
