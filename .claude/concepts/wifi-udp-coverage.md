# WiFi UDP Coverage (examples 15 & 16)

## Problem

WiFi/UDP is the one MicroWorld transport never proven on hardware. `FEsp32UdpDriver`
(lwIP UDP) is compile-verified only — the Phase 5.2 proof brought up the socket but
deliberately never associated WiFi, so **no datagram has ever crossed the radio**, and
one receive behavior (oversize-datagram `MSG_TRUNC` sizing) is documented as
runtime-UNVERIFIED. Unlike the wired sweep (UART/I2C/SPI), the examples that would
prove WiFi (`15-UdpEcho`, `16-TwoBoardUdp`) **do not exist yet** — they must be built
from scratch, including net-new WiFi station bring-up glue. The user wants full WiFi
coverage: both examples built and hardware-verified.

## Proposed Approach

Build two examples, simplest-first, each compile-verified before its human-gated
hardware checkpoint:

- **`15-UdpEcho`** — one board joins WiFi and echoes UDP datagrams back to a PC on the
  same network, using the raw `FEsp32UdpDriver` (`TrySend`/`TryReceive`/`PollReadable`).
  This is the *transport* proof: it exercises WiFi association + the driver's send/receive
  on real silicon, and is the natural place to finally exercise the oversize `MSG_TRUNC`
  path (PC sends a >1200-byte datagram). Ships a `tools/EchoClient.py` for the PC side.
- **`16-TwoBoardUdp`** — two boards run the full networked engine over WiFi UDP: a
  dedicated-server `TEngineHost` bound to `TNetHost` via `TNetHostFrame`, and a bare
  `TNetHost` client. This is the *protocol-over-transport* proof — a direct port of the
  proven host `TwoNodeDemo` (ch1 spawn request, ch2 state broadcast, `MaxSpawns=2`) onto
  two ESP32 boards. The WiFi twin of example 19 (which proved the same stack over UART).

Both reuse the same net-new pieces (duplicated per-example, which §2.4 explicitly allows):
`WifiStation.h/.cpp` (the §3.8 station-bring-up contract), a committed
`NetworkConfig.example.h` template with the real git-ignored `NetworkConfig.h`, and the
`PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event` build variant. Two hardware
lessons are already paid for and baked in: **all composition objects `static`** (main-task
stack is 3,584 B) and **`nvs_flash_init` + `esp_netif_init` + `esp_event_loop_create_default`
before any driver is constructed**.

Hardware stays human-gated: I compile-verify (§1.1 Build Verify) and STOP; no `-t upload`
or `device monitor` runs without explicit per-example authorization in that turn.

## Open Questions

None — all resolved (see Decisions Log).

## Decisions Log

- 2026-07-23: WiFi coverage = examples 15 + 16 only; E32 LoRa (17) is a separate radio
  transport, not WiFi, and is out of scope here.
- 2026-07-23: Compile-first, hardware human-gated per §1.2 — unchanged from the wired sweep.
- 2026-07-23: Scope = **both** 15 and 16, built 15-first — 15 de-risks WiFi + driver
  before the full engine rides on top.
- 2026-07-23: A shared **2.4 GHz** network reachable by both boards and this PC is
  available — example 15's PC↔board echo is hardware-verifiable.
- 2026-07-23: Example 15 **will exercise the oversize `MSG_TRUNC` path** (PC sends a
  >1200-byte datagram) to close the last unverified driver branch.
- 2026-07-23: Capture is **one-sided** on this rig (only the CH343 console is readable);
  example 16 gets a clean single-side capture, as example 19 did. Accepted default.
- 2026-07-23: **Out-of-order build authorized** — 15/16 build ahead of the unbuilt 02–14
  ladder, same precedent as the wired 18–21 set; 02–14 remain separate future work.
- 2026-07-23 (PIVOT): **SoftAP topology, no home router.** The server board hosts its own
  WiFi (SoftAP); the client joins it as a station. This erases the home-WiFi dependency
  *and* the real-credential problem — the AP SSID/password are fixed non-secret demo
  values baked into the committed shared header, and the server IP is the fixed SoftAP
  gateway `192.168.4.1` (no discovery step). Deviates from the roadmap's stated "shared
  2.4 GHz WiFi" (station) design; noted in the example READMEs. `NetworkConfig.h` /
  `NetworkConfig.example.h` are removed (no secrets to hide).
- 2026-07-23 (PIVOT): **Example 15 becomes a two-board echo.** A second board plays the
  "PC": one role hosts the AP and runs the raw-driver UDP echo (and reports the oversize
  outcome); the other joins and sends a normal datagram + a >1200-byte oversize probe.
  No PC, so this session's connectivity is never at risk. `tools/EchoClient.py` is removed.
