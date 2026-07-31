#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>

// Role is chosen at build time: server (=1) or client (=0). Both roles always
// compile -- ServerMain.cpp and ClientMain.cpp are always built -- and this
// define only selects which one runs; never build_src_filter, which ESP-IDF
// ignores. This example runs one world's router over TWO simultaneous physical
// links (WiFi UDP telemetry + a UART command wire) behind one TNetworking.
#ifndef MICROWORLD_EXAMPLE_SERVER
#error "Define MICROWORLD_EXAMPLE_SERVER=1 (server) or 0 (client) via the build environment."
#endif

/**
 * Motivation: Lets the server role live in its own translation unit so the composition root stays a build-time switch.
 * Responsibilities: Run the server role (Board A): FTelemetrySinkActor + FCommanderActor, defined in ServerMain.cpp.
 */
void RunServer() noexcept;

/**
 * Motivation: Lets the client role live in its own translation unit so the composition root stays a build-time switch.
 * Responsibilities: Run the client role (Board B): FSensorActor, defined in ClientMain.cpp.
 */
void RunClient() noexcept;

/**
 * Motivation: Composition root for example 24, so the single ESP32 entry point stays a thin
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
