#pragma once

#include <MicroWorld/Platform/Pico/Detail/PicoUartPlatform.h>

namespace MicroWorld::Detail
{

/** Legacy E32 platform spelling retained while compatibility callers migrate to the generic IPicoUartPlatform interface. */
using IPicoE32LoraPlatform = IPicoUartPlatform;

/** Returns the generic process-lifetime Pico UART binding through the legacy E32 compatibility spelling. */
inline IPicoE32LoraPlatform& GetPicoE32LoraPlatform() noexcept
{
	return GetPicoUartPlatform();
}

} // namespace MicroWorld::Detail
