#include <MicroWorld/PlatformEsp32/Esp32E32LoraDriver.h>

#include <cstdint>

namespace
{

/** Uses plain configuration values so the released facade compiles without exposing ESP-IDF UART types. */
constexpr MicroWorld::FEsp32E32LoraConfig RadioProbeConfig{1, 17, 18, 9600, 1};

} // namespace

/**
 * Compiles and links the released ESP32 RadioE32 facade without opening UART hardware at runtime.
 *
 * The default-false volatile gate keeps construction, destruction, and bounded transmit progress link-visible while
 * normal execution performs no radio I/O, leaving hardware behavior to explicit target verification.
 */
extern "C" void app_main()
{
	/**
	 * Keeps the compile/link probe disabled in normal firmware execution.
	 *
	 * This local gate has no configuration surface or persistent state; volatility prevents the compiler from removing
	 * the guarded facade references that the radio-specific profile must compile and link.
	 */
	volatile bool bRunRadioHardwareProbe = false;
	if (bRunRadioHardwareProbe)
	{
		MicroWorld::FEsp32E32LoraDriver Driver(RadioProbeConfig);
		Driver.AdvanceTransmit();
	}
}
