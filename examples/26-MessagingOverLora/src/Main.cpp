#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>

// Role is chosen at build time: server (=1) or client (=0). Both roles always
// compile — ServerMain.cpp and ClientMain.cpp are always built — and this
// define only selects which one runs; never build_src_filter, which ESP-IDF
// ignores. This is example 19's full TTransportHost message protocol carried over an
// E32 LoRa radio instead of a wire: the device is the E32 LoRa device and the
// session runs the D8 airtime profile (relaxed heartbeat/timeout, paced broadcast).
#ifndef MICROWORLD_EXAMPLE_SERVER
#error "Define MICROWORLD_EXAMPLE_SERVER=1 (server) or 0 (client) via the build environment."
#endif

/**
 * Motivation: Lets the server role live in its own translation unit so the entry point stays a build-time switch.
 * Responsibilities: Run the dedicated-server role defined in ServerMain.cpp.
 */
void RunServer() noexcept;

/**
 * Motivation: Lets the client role live in its own translation unit so the entry point stays a build-time switch.
 * Responsibilities: Run the bare-client role defined in ClientMain.cpp.
 */
void RunClient() noexcept;

/**
 * Motivation: Application entry point for example 26: the single ESP32 `app_main` stays a thin
 *   build-time role selector rather than carrying role behavior.
 * Responsibilities: Install the output device, then run the role this image was built for.
 */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::Platform::Esp32::WriteEsp32LogRecord);
#if MICROWORLD_EXAMPLE_SERVER
	RunServer();
#else
	RunClient();
#endif
}
