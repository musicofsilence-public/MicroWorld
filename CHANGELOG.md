# Changelog

## Unreleased

- **Wired board-to-board transports (`Modules/PlatformEsp32`).** New `INetDriver`
  implementations behind `docs/Porting.md` seam 2, so the existing byte I/O,
  `FrameCodec` framing, `TNetManager`, and `TNetHost` message design run over a
  plain wire unchanged: `FEsp32UartDriver` (point-to-point UART), the
  `FEsp32I2cMasterDriver`/`FEsp32I2cSlaveDriver` pair (I2C), and the
  `FEsp32SpiMasterDriver`/`FEsp32SpiSlaveDriver` pair (full-duplex SPI), each with
  a 1-byte address helper (`UartAddress`/`I2cAddress`/`SpiAddress`). ADR 0003
  records the design — a wire is just another transport; peripheral device-bus
  access is out of scope. Portable packages are unchanged.
- **ESP32 examples 18–21.** `18-TwoBoardUart` and `19-UartMessaging` (example 16's
  full `TNetHost` client/server design over a wire, zero WiFi), `20-TwoBoardI2c`,
  and `21-TwoBoardSpi` — the same ping-pong volley over UART, I2C, and SPI with
  the driver construction as the only difference. Compile-verified for ESP32-S3;
  the hardware checkpoints are human-gated and pending.

Live state and exact evidence are recorded in
[PROGRESS.md](PROGRESS.md).

## 0.3.0 - 2026-07-23

The 0.3.0 release is the student-facing simplicity refactor. It is
behavior-preserving throughout except one documented change, but it is
**API-breaking for source consumers**: public identifiers were renamed so
every one states its role.

- **Renames so every identifier states its role (Phase 1, source-breaking).**
  Net class templates changed prefix `F`→`T` (`FNetManager`→`TNetManager`,
  `FNetPacketStorage`→`TNetPacketStorage`, `FHostLoopback`→`THostLoopback`);
  the registry "lease" vocabulary became "Reference" (`MakeView()`→
  `MakeReference()`, `F...RegistryBase`→`F...RegistryReference`);
  abbreviations were expanded (`Src`/`Len`/`Hi`/`Lo`→ full words); timer/host
  template params were unified (`GcBudget`→`GarbageCollectionBudget`,
  `SlotBytes`→`SlotSizeBytes`, `InlineCallbackBytes`+`TimerCallbackBytes`→
  `InlineTimerCallbackBytes`); private platform headers `...Glue.h`→
  `...PlatformImplementation.h`.
- **One behavior change (D7).** `Net/FrameCodec.h` `EncodeFrame` now returns
  `Invalid` (previously `Full`) for a payload larger than `0xFFFF` bytes.
- **Readability-only internal work (no public behavior change).** Why-comment
  repairs, long-function decomposition into named ≤2-action helpers,
  cross-cutting ceremony reduction (a shared `Memory/Containers/RawSlot.h`,
  `ERuntimeResult` in its own `Core/RuntimeResult.h`), a written style
  contract in `docs/Style.md` with self-documenting `HostLifecycle`/
  `TwoNodeDemo` examples, and complete per-folder `AGENTS.md` coverage.

Live state and exact evidence are recorded in
[PROGRESS.md](PROGRESS.md).

## 0.2.0 - 2026-07-21

The 0.2.0 release delivers the managed runtime, networking, and platform
support around Core. All six roadmap phases are complete; ESP32-S3 runtime
margins were measured on hardware in Phase 6.2.

- **Core actor-model retirement (Phase 1).** The duplicate Core
  `TWorld`/`TActor`/`FActorComponent`/`FNetwork` actor model is removed; the
  managed Engine (`UWorld`/`AActor`/`UActorComponent`) is the sole
  World/Actor/Component API and Core is lifecycle/tick primitives only
  (`FApplication`, `FTickFunction`, `FLifecycleGuard`, `FTickable`).
- **Runtime spawn/destroy (Phase 2).** `UWorld::SpawnActor`/`DestroyActor`
  queue at the call site and apply at one deferred `ApplyPending` barrier per
  frame (destroys before spawns); capacity counts live + pending; every
  rejection is transactional.
- **Composition root + logging (Phase 3).** `TEngineHost<...>` owns the class
  registry, object store, garbage collector, world, and timer manager behind
  one fixed 7-step per-frame order (PumpReceive → Timers.Advance →
  World.Advance → World.ApplyPending → Store.ApplyPendingDestroy → GC slice →
  PumpSend). `MW_LOG`/`MW_LOG_MSG` add a bounded logging facade with a
  compile-time level floor.
- **Networking with roles (Phase 4).** `TNetHost<MaxPeers, MaxPacketBytes>`
  runs `ENetMode` Standalone / Client / ListenServer / DedicatedServer over a
  bounded peer table, with Hello/Welcome admission, heartbeats, timeout
  eviction, generation-checked `FPeerId`, and channel-based send/receive
  (channel 0 reserved for control). Engine integrates net through an
  engine-owned `INetworkFrame` seam so production Engine stays net-free.
- **Real transports + platform adapters (Phase 5).** Two non-portable adapter
  packages, both excluded from `CheckDependencyBoundaries.py`: `microworld-platform-host`
  (`FHostUdpDriver`) and `microworld-platform-esp32` (`FEsp32TimeSource`,
  `FEsp32UdpDriver`, `FEsp32E32LoraDriver`, `Esp32LogSink`). The portable
  `Net/FrameCodec.h` (`TFrameDecoder`, `EncodeFrame`, CRC-16/CCITT-FALSE)
  provides the LoRa wire framing, host-tested off-target.
- **Two-node demo (Phase 6.1).**
  `lib/microworld-platform-host/examples/TwoNodeDemo` composes a
  dedicated-server `TEngineHost` and a bare `TNetHost` client over real
  localhost UDP in one deterministic interleaved loop.
- **Measured ESP32-S3 runtime margins (Phase 6.2).** On a connected ESP32-S3 @
  160 MHz (release `-Os`, image RAM 43,148 B / Flash 313,269 B), the
  representative world (8 actors / 16 components / 8 timers) measured: tick
  min 62 / mean 73 / max 114 µs; GC Advance-slice min 21 / mean 25 / max 39 µs
  (budget `{root=1,mark=1,sweep=8}`); no-traffic net pump mean 47 µs;
  world-setup heap 580 B; main-task stack 2,476 B free after setup. Recorded
  in `lib/microworld-platform-esp32/benchmarks/Results/Esp32S3N16R8.md`.

Live state and exact evidence are recorded in
[PROGRESS.md](PROGRESS.md).

## 0.1.0 - 2026-07-18

Initial Core release:

- `FApplication`, `TWorld<N>`, `TActor<N>`, and `FActorComponent` provide
  bounded deterministic lifecycle dispatch.
- `FTickFunction` provides caller-time scheduling, saturation, independent
  tick configuration, and no catch-up bursts.
- CMake/CTest, PlatformIO consumer probes, host tests, a host example, and
  benchmark harnesses are included.

The release passed 31 host behavior cases, strict public-header and
no-exceptions/no-RTTI consumers, and recorded ESP32-S3 compile evidence. See
the [Core evidence records](Modules/Core/benchmarks/Results) for exact toolchain and build
facts.
