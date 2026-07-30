#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>

// Role is chosen at build time: server (=1) or client (=0). Both roles always
// compile -- ServerMain.cpp and ClientMain.cpp are always built -- and this
// define only selects which one runs; never build_src_filter, which ESP-IDF
// ignores. This example runs one world's router over TWO simultaneous physical
// links (WiFi UDP telemetry + a UART command wire) behind one TNetSystem.
#ifndef MICROWORLD_EXAMPLE_SERVER
#error "Define MICROWORLD_EXAMPLE_SERVER=1 (server) or 0 (client) via the build environment."
#endif

/** Runs the server role (Board A): FTelemetrySinkActor + FCommanderActor; defined in ServerMain.cpp. */
void RunServer() noexcept;

/** Runs the client role (Board B): FSensorActor; defined in ClientMain.cpp. */
void RunClient() noexcept;

/** Composition root: installs the output device, then runs the role this image was built for. */
extern "C" void app_main(void)
{
	MicroWorld::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);
#if MICROWORLD_EXAMPLE_SERVER
	RunServer();
#else
	RunClient();
#endif
}
