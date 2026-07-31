# Transport Conforms To The Model

## Problem

The C3 Transport model and the Transport code describe different systems.

The model says Transport is **flat**: one `Device Interface` — `Setup`,
`SendDataTo`, `OnDataReceived`, plus the four play-system turns — realised by four
medium devices, with a frame codec used by the two stream media. Nothing sits above
a device.

The code is a **three-layer stack**:

```
TNetHost      peer table, Hello/Welcome admission, heartbeats, timeout eviction,
              ENetMode roles (Standalone/Client/ListenServer/DedicatedServer),
              channel 0 reserved for control
   |
TNetManager   outbound FIFO + TNetPacketStorage
   |
INetDriver    TrySend / TryReceive
```

That is precisely the layer the model deleted, and `ENetMode` is UE5 net-mode
vocabulary the model never adopted. Three further gaps:

- **No `Setup`, no callback, no turns.** `INetDriver` is poll-shaped: `TryReceive`
  is pulled, not subscribed, and nothing configures a driver through the contract.
- **Only one medium is portable.** The model draws four peer devices inside
  Transport. In code only LoRa has one (`RadioE32Driver`); Wi-Fi and the three wired
  buses exist solely as per-platform drivers under `Platform/Esp32`, `Platform/Host`
  and `Platform/Pico`, sharing nothing but `UdpAddressCodec.h`. Bluetooth is absent.
- **`Net` is everywhere.** 147 files, led by `ENetResult` (868), `FNetAddress` (250),
  `ENetMode` (123), `TNetHost` (117). The model dropped the word when `Net` became
  `Transport` and `Net System` became `Networking`; the code did not follow.

The folder is also unstructured — 18 files flat in `Transport/`, plus `Detail/` —
which ADR 0004 says should be downstream of the model.

## Proposed Approach

**Split it in two, and do only the first stage here.**

**Stage 1 — folder and naming.** Mechanical, reviewable as a diff, no behaviour
change. Rename `Net` to the model's words, and give the folder a shape the model
explains. Nothing moves between systems and no contract changes.

**Stage 2 — shape.** A separate concept: collapse `TNetHost`/`TNetManager`/
`INetDriver` into the one `Device Interface`, decide where peer admission and
heartbeats go, and decide whether Wi-Fi and Wired get portable devices or stay
per-platform.

Why split: a 147-file rename landing in the same commit as a contract redesign is
unreviewable, and Stage 2 has open design questions Stage 1 does not need answered.
Stage 1 also makes Stage 2 smaller, because the surviving names are already the ones
the model uses.

Proposed folder for Stage 1 — one directory per model element, flat files for what
the whole system shares:

```
Transport/
  Device.h          the one contract              (was NetDriver.h)
  Address.h         the address shape             (was NetAddress.h)
  Result.h          explicit outcomes             (was NetResult.h)
  FrameCodec.h      unchanged
  ByteReader.h ByteWriter.h
  Lora/             RadioE32Driver, E32Lora, Detail/E32LoraTransportState
  Wifi/             UdpAddressCodec
  Testing/          HostLoopback, PacketDropDriver
```

`Wired/` and `Bluetooth/` get folders when they get portable code, not before —
an empty directory asserts a device that does not exist. Everything the shape stage
will delete (`NetHost`, `NetManager`, `NetPacketStorage`, `NetProtocol`) stays flat
and keeps its name until Stage 2 removes it; renaming a thing we are about to delete
is work that gets thrown away.

## Open Questions

None outstanding. The four that opened this concept are resolved below.

## Decisions Log

- 2026-07-31: Split into a naming/folder stage and a shape stage — a mechanical
  rename mixed with a contract redesign cannot be reviewed, and only the second
  needs design decisions.
- 2026-07-31: Do not rename what Stage 2 deletes (`NetHost`, `NetManager`,
  `NetPacketStorage`, `NetProtocol`) — thrown-away work.
- 2026-07-31: No empty `Wired/` or `Bluetooth/` folders — a directory asserts code
  that exists.
- 2026-07-31: Names come from the model, not from a uniform prefix. `INetDriver` ->
  `IDevice`, `FNetAddress` -> `FDeviceAddress`, `FNetDriverHandle` -> `FDeviceHandle`,
  `FNetReceiveResult` -> `FReceiveResult`, `FNetFrame` -> `FWireFrame` ("wire framing"
  is the model's phrase, and a bare `FFrame` would collide with the frame loop).
- 2026-07-31: `ENetResult` -> `ETransportResult`, keyed on the system, because the
  other six result enums already follow `E<Subject>Result` (`EEngineResult`,
  `EMessageResult`, `ETimerResult`, ...). A `Device`-keyed name would have broken the
  only naming convention the codebase actually has.
- 2026-07-31: Scope covers Networking as well as Transport, so no half-renamed state
  survives: `TNetSystem` -> `TNetworking`, `FDefaultNetSystemTraits` ->
  `FDefaultNetworkingTraits`, `NetSystem.h` -> `Networking.h`.
- 2026-07-31: **Reversed** — nothing is frozen; every `Net` identifier is renamed.
  An adversarial pass on the plan showed the freeze depended on predicting Stage 2,
  and the prediction was wrong in both directions: `FNetReceiveResult` dies (a span
  carries its own length, so a callback contract has no out-param struct) while
  `ENetMode` and `FNetHostConfig` survive inside `TNetworking`'s public signature.
  The work saved was illusory — one scripted substitution costs the same either way.
- 2026-07-31: Types with no obvious new name take a mechanical `Net` -> `Transport`,
  since they live in the Transport system. `ENetMode` -> `ENetworkMode` (owner's
  choice), `TNetHostSystem` -> `THostPlaySystem` because it lives in Engine and a
  Transport-flavoured name there would be worse than the one it replaces.
- 2026-07-31: `NetifResult` (lwIP) and the words `Network`/`Networking` are protected.
- 2026-07-31: **Corrected** — `docs/UE5ConceptMap.md` is *not* protected wholesale. Its
  "MicroWorld equivalent" column names our own types (`TNetSystem`, `ENetMode`,
  `TNetHost`, `INetDriver`), so the file misdirects until they are renamed. Only the
  phrase "UChannel / NetDriver channels" names Unreal's API and stays.
- 2026-07-31: A document that says what to do next is updated; one that records a
  measurement or a finished run is not. So `docs/RADIO_TRANSPORTS_ROADMAP.md` changes —
  it is a live worker protocol with open phases naming shipped API — while
  `Modules/benchmarks/**/Results/*.md` keeps the names its runs were made under.
- 2026-07-31: The 26 examples rename in the same commit. They name `ENetResult`
  directly, so any split leaves the build broken.
- 2026-07-31: `model.c4` changes with the code. Networking's contract still reads
  "TNetSystem ... owns net drivers", and the model must not be the last place using a
  word it retired.
