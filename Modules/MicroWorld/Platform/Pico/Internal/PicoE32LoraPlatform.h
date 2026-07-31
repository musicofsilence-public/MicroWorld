#pragma once

#include <MicroWorld/Platform/Pico/Internal/PicoUartPlatform.h>

namespace MicroWorld::Platform::Pico
{

/** Motivation: Legacy E32 platform spelling retained while compatibility callers migrate to the generic IPicoUartPlatform interface. */
using IPicoE32LoraPlatform = IPicoUartPlatform;

/**
 * Motivation: Hands the generic process-lifetime Pico UART binding to callers that still use the legacy E32 spelling.
 * Responsibilities: Delegate to GetPicoUartPlatform so legacy and new code share the one SDK binding.
 */
inline IPicoE32LoraPlatform& GetPicoE32LoraPlatform() noexcept
{
	return GetPicoUartPlatform();
}

} // namespace MicroWorld::Platform::Pico
