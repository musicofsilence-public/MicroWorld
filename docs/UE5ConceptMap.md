# UE-Style Concept Map

MicroWorld borrows a few useful C++ concepts from UE. It is not source, binary,
editor, or asset compatible with UE.

| Familiar concept | MicroWorld | State | Difference |
| --- | --- | --- | --- |
| Application root | `FApplication` | Core (0.2.0) | Consumer owns and orders concrete services; no global engine |
| Primary tick | `FTickFunction` | Core (0.2.0) | Caller supplies time; no tick groups or catch-up bursts |
| Managed object | `UObject`, handles, roots, GC | Object (0.2.0) | Fixed caller-owned storage and explicit tracing |
| Managed World / Actor / Component | `UWorld`, `AActor`, `UActorComponent` | Engine (0.2.0) | Application roots World; World/Actor trace children; parent references are weak |
| Dynamic spawn / destroy | `UWorld::SpawnActor` / `DestroyActor` / `ApplyPending` | Engine (0.2.0) | Queue at call site; one deferred barrier per frame (destroys before spawns); capacity counts live + pending; transactional rejections |
| Timers | `TTimerManager<MaxTimers, InlineCallbackBytes>` | Engine (0.2.0) | Fixed capacity, caller time, explicit OneShot/Looping mode allowlist, single-pass post-dispatch compaction, deterministic insertion-order dispatch, no catch-up bursts |
| Composition root / game instance | `TEngineHost<...>` | Engine (0.2.0) | Owns registry/store/GC/world/timers; fixed 7-step frame order (PumpReceive → Timers → World.Advance → ApplyPending → Store.ApplyPendingDestroy → GC slice → PumpSend) |
| Networking with roles | `ENetMode`, `TNetHost<MaxPeers, MaxPacketBytes>`, peers, channels | Net (0.2.0) | Standalone / Client / ListenServer / DedicatedServer; bounded peer table; Hello/Welcome admission, heartbeats, timeout eviction; channel 0 reserved for control; simple messages, not replication |
| Network byte I/O | `INetDriver`, `FNetManager`, `FByteWriter`/`FByteReader`, `FNetAddress`, `FHostLoopback` | Net (0.2.0) | One non-blocking addressed driver, fixed-capacity caller-storage-backed manager, bounded bytes over caller-owned spans, transactional failure semantics, deterministic host loopback independent of Engine |
| Wire framing | `Net/FrameCodec.h` (`TFrameDecoder`, `EncodeFrame`, CRC-16/CCITT-FALSE) | Net (0.2.0) | Portable, host-tested; used by the E32 LoRa adapter |
| Platform adapters (time / UDP / LoRa) | `FEsp32TimeSource`, `FHostUdpDriver`, `FEsp32UdpDriver`, `FEsp32E32LoraDriver`, `Esp32LogSink` | platform-host / platform-esp32 (0.2.0, non-portable) | Supply the real transports and clock behind `INetDriver` / `TimePointMilliseconds` / `FLogSink`; depend inward on portable packages, never the reverse |

`TObjectPtr` is a traced managed reference, `TWeakObjectPtr` observes without
retaining, and `TStrongObjectPtr` is an explicit external root. They are not
general-purpose replacements for normal ownership.

`F`, `T`, `E`, `I`, and `b` follow the local naming style. `U` and `A` are
reserved for real MicroWorld managed types; they do not claim Unreal inheritance
or compatibility.

Not part of the engine: reflection generation, replication/RPC, background
tasks, universal hardware APIs, editor tooling, rendering, physics, audio,
navigation, or asset systems.
