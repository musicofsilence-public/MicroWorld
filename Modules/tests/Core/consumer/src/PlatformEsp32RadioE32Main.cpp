#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>

#include <cstdint>

namespace
{

/** Motivation: Uses plain configuration values so the released facade compiles without exposing ESP-IDF UART types. */
constexpr MicroWorld::Platform::Esp32::FEsp32E32LoraConfig RadioProbeConfig{1, 17, 18, 9600, 1};

/** Motivation: Names the stamp handed to a pre-advance turn taken without a clock, so the zero reads as deliberate. */
constexpr MicroWorld::Core::TimePointMilliseconds UnpacedPumpTimeMilliseconds{0};

} // namespace

/**
 * Motivation: Compiles and links the released ESP32 RadioE32 facade without opening UART hardware at runtime.
 * Responsibilities: Keep construction, destruction, and transmit progress link-visible while performing no radio I/O.
 */
extern "C" void app_main()
{
	/** Motivation: Keeps the compile/link probe disabled in normal firmware execution; volatility prevents the compiler from removing the guarded
	 * facade references. */
	volatile bool bRunRadioHardwareProbe = false;
	if (bRunRadioHardwareProbe)
	{
		MicroWorld::Platform::Esp32::FEsp32LoraDevice Device(RadioProbeConfig);
		// A link probe owns no clock, and the E32 radio paces nothing by one, so the turn matters and its stamp does not.
		Device.PreAdvance(UnpacedPumpTimeMilliseconds);
	}
}
