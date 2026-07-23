#include <MicroWorld/Log.h>
#include <MicroWorld/PlatformEsp32/Esp32LogSink.h>

// Role is chosen at build time: server (=1) or client (=0). Both roles always
// compile — ServerMain.cpp and ClientMain.cpp are always built — and this define
// only selects which one runs; never build_src_filter, which ESP-IDF ignores.
// This is the full TNetHost message protocol of example 19 carried over WiFi UDP
// instead of a wire: only the driver construction and the server address differ.
#ifndef MICROWORLD_EXAMPLE_SERVER
#error "Define MICROWORLD_EXAMPLE_SERVER=1 (server) or 0 (client) via the build environment."
#endif

/** Runs the dedicated-server role; defined in ServerMain.cpp. */
void RunServer() noexcept;

/** Runs the bare-client role; defined in ClientMain.cpp. */
void RunClient() noexcept;

/** Composition root: installs the log sink, then runs the role this image was built for. */
extern "C" void app_main(void)
{
	MicroWorld::SetLogSink(&MicroWorld::Esp32LogSink);
#if MICROWORLD_EXAMPLE_SERVER
	RunServer();
#else
	RunClient();
#endif
}
