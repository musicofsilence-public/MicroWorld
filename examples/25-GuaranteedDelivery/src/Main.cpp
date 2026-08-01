#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>

// Role is chosen at build time: server (=1) or client (=0). Both roles always
// compile -- ServerMain.cpp and ClientMain.cpp are always built -- and this
// define only selects which one runs; never build_src_filter, which ESP-IDF
// ignores. This example runs one engine-owned Messaging system over ONE WiFi-UDP
// link carrying TWO channels at once -- one best-effort and one reliable -- with
// the client injecting deterministic packet loss via FPacketDropDevice.
#ifndef MICROWORLD_EXAMPLE_SERVER
#error "Define MICROWORLD_EXAMPLE_SERVER=1 (server) or 0 (client) via the build environment."
#endif

/**
 * Motivation: Lets the server role live in its own translation unit so the entry point stays a build-time switch.
 * Responsibilities: Run the server role (Board A): FLedgerActor, defined in ServerMain.cpp.
 */
void RunServer() noexcept;

/**
 * Motivation: Lets the client role live in its own translation unit so the entry point stays a build-time switch.
 * Responsibilities: Run the client role (Board B): FCounterActor behind FPacketDropDevice, defined in ClientMain.cpp.
 */
void RunClient() noexcept;

/**
 * Motivation: Application entry point for example 25: the single ESP32 `app_main` stays a thin
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
