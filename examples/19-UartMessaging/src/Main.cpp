#include <MicroWorld/Log.h>
#include <MicroWorld/PlatformEsp32/Esp32OutputDevice.h>

// Role is chosen at build time: server (=1) or client (=0). Both roles always
// compile — ServerMain.cpp and ClientMain.cpp are always built — and this
// define only selects which one runs; never build_src_filter, which ESP-IDF
// ignores. This is example 16's full TNetHost message protocol with every WiFi
// step deleted: the whole link is one wire, and only the driver construction
// line differs from example 16.
#ifndef MICROWORLD_EXAMPLE_SERVER
#error "Define MICROWORLD_EXAMPLE_SERVER=1 (server) or 0 (client) via the build environment."
#endif

/** Runs the dedicated-server role; defined in ServerMain.cpp. */
void RunServer() noexcept;

/** Runs the bare-client role; defined in ClientMain.cpp. */
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
