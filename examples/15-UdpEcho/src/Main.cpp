// Role is chosen at build time: echo server (=1, hosts the SoftAP and echoes) or
// probe (=0, joins the AP and sends). Both roles always compile — EchoServerMain.cpp
// and ProbeMain.cpp are always built — and this define only selects which one runs;
// never build_src_filter, which ESP-IDF ignores.
#ifndef MICROWORLD_EXAMPLE_SERVER
#error "Define MICROWORLD_EXAMPLE_SERVER=1 (echo server) or 0 (probe) via the build environment."
#endif

/** Runs the echo-server role (hosts the SoftAP); defined in EchoServerMain.cpp. */
void RunEchoServer() noexcept;

/** Runs the probe role (joins the SoftAP); defined in ProbeMain.cpp. */
void RunProbe() noexcept;

/** Composition root: runs the role this image was built for. */
extern "C" void app_main(void)
{
#if MICROWORLD_EXAMPLE_SERVER
	RunEchoServer();
#else
	RunProbe();
#endif
}
