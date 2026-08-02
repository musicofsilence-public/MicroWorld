# 17-TwoBoardLora

Inherits `../AGENTS.md`.

## Architecture

One application entry point (`app_main`) owns one static `FEsp32LoraDevice` and
drives a bare ping-pong directly over its `TrySend`/`TryReceive` — no
`TTransportManager`, no `TTransportHost`, no world. The role (node 1 vs node 2) is a
compile-time constant from `-DMICROWORLD_EXAMPLE_NODE_ID`, so the two build
environments share one source file.

## Concepts

- Makes the `Core::ITransportDevice` interface observable over radio: the volley loop is
  transport-agnostic, so this is example 18 with only the device construction
  line changed.
- LoRa is a broadcast, half-duplex medium: the destination
  `MakeLoraAddress(PeerNodeId)` is validated but not routed on the wire; the
  sender identity a receiver prints comes from the frame's own source-id byte
  via `LoraAddressNodeId`. A module does not hear its own transmission, so the
  reply-on-receive volley logic works unchanged from the wired example.
- Static device and RX buffer, never `app_main` stack locals (§2.2).
- Airtime, not distance, sets the pace: a full LoRa frame costs hundreds of
  milliseconds on air, so the volley period is 1 s rather than the wired
  example's 500 ms, to avoid congesting the channel.
- A LoRa link is radio, not a wire: an occasional gap can be weather
  (distance, interference) rather than a defect to chase.
- Pico pairing keeps this ESP32 runtime source unchanged: the Pico-local
  consumer is node 1, while the `esp32-s3-node-b` image remains node 2 and its
  `mw log` output is the observable proof of the cross-board volley.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d examples/17-TwoBoardLora`
builds both role environments. The hardware checkpoint (§1.3, human-gated,
roadmap task 1.2) flashes node 1 to one board and node 2 to the other,
expects both monitors to show the counter climbing alternately, and flips
this example's "not yet verified on hardware" status once a captured trace is
pasted into the README.

For Pico pairing, build this example's node-B environment, then use
`pico-freertos\\pico.bat build lora` and the human-gated `upload lora` command.
The paired trace must include node B's `rx n=1 from=1` and
`tx n=2 result=Success` lines before claiming Pico interoperability.
