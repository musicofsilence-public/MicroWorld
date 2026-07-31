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

/** Runs the dedicated-server role; defined in ServerMain.cpp. */
void RunServer() noexcept;

/** Runs the bare-client role; defined in ClientMain.cpp. */
void RunClient() noexcept;

/** Composition root: installs the output device, then runs the role this image was built for. */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);
#if MICROWORLD_EXAMPLE_SERVER
	RunServer();
#else
	RunClient();
#endif
}
