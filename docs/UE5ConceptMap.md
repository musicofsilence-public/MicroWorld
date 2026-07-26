# UE-Style Concept Map

MicroWorld borrows a few useful C++ concepts from UE. It is not source, binary,
editor, or asset compatible with UE.

| Familiar concept | MicroWorld | State | Difference |
| --- | --- | --- | --- |
| Application root | `FApplication` | Application (0.3.0) | Holds one `IEngine&`; begin/tick/end forwarding is sealed behind private methods and `OnConfigure` is the only hook that reaches the world |
| Run loop / entry point | `TApplicationRunner` | Application (0.3.0) | Owns the begin/advance/end sequence on an injected clock and pacing function; a platform supplies a clock and a sleep instead of a hand-rolled `for (;;)` |
| Primary tick | `FTickFunction` | Core (0.3.0) | Caller supplies time; no tick groups or catch-up bursts |
| Managed object | `UObject`, handles, roots, GC | Object (0.3.0) | Fixed caller-owned storage and explicit tracing |
| Managed World / Actor / Component | `UWorld`, `AActor`, `UActorComponent` | Engine (0.3.0) | Application roots World; World/Actor trace children; parent references are weak |
| Dynamic spawn / destroy | `UWorld::SpawnActor` / `DestroyActor` / `ApplyPending` | Engine (0.3.0) | Queue at the call site either before or during play: a pre-play queue drains when the world begins, and during play one deferred barrier per frame (destroys before spawns); capacity counts live + pending; transactional rejections |
| Timers | `TTimerManager<MaxTimers, InlineTimerCallbackBytes>` | Engine (0.3.0) | Fixed capacity, caller time, explicit OneShot/Looping mode allowlist, single-pass post-dispatch compaction, deterministic insertion-order dispatch, no catch-up bursts |
| Composition root / game instance | `TEngine<TTraits>` behind `IEngine` | Engine (0.3.0) | Owns registry/store/GC/world/timers and one bound engine system; fixed 7-step frame order (system PreAdvance → Timers → World.Advance → ApplyPending → Store.ApplyPendingDestroy → GC slice → system PostAdvance) |
| Tickable engine subsystem | `IEngineSystem`, `TEngineSystemSet`, `TNetSystem` | Core / Engine / Integration (0.3.0) | Four turns: BeginPlay, PreAdvance, PostAdvance, EndPlay. `TEngine` binds exactly one; a set composes several with add-order start and reverse-order shutdown. The interface lives in Core so Messaging and Net can implement it without seeing Engine, and `TNetSystem` is the only place Engine and Net meet |
| Networking with roles | `ENetMode`, `TNetHost<MaxPeers, MaxPacketBytes>`, peers, channels | Net (0.3.0) | Standalone / Client / ListenServer / DedicatedServer; bounded peer table; Hello/Welcome admission, heartbeats, timeout eviction; channel 0 reserved for control; simple messages, not replication |
| Network byte I/O | `INetDriver`, `TNetManager`, `FByteWriter`/`FByteReader`, `FNetAddress`, `THostLoopback` | Net (0.3.0) | One non-blocking addressed driver, fixed-capacity caller-storage-backed manager, bounded bytes over caller-owned spans, transactional failure semantics, deterministic host loopback independent of Engine |
| Wire framing | `Net/FrameCodec.h` (`TFrameDecoder`, `EncodeFrame`, CRC-16/CCITT-FALSE) | Net (0.3.0) | Portable, host-tested; used by the E32 LoRa adapter |
| Actor messaging / channels | `IMessageChannel`, `TMessageChannelBinding`, `FMessageChannelId` | Messaging (0.3.0) | Resembles UE `UChannel` / NetDriver channels; bounded typed byte messages keyed by type+actor id, no replication or property sync; channel 0 is local delivery; a wire channel is one `TMessageChannelBinding` over a `TNetHost` |
| Gameplay Message Subsystem (broadcast/targeted message bus) | `TMessageRouter` | Messaging (0.3.0) | Fixed-capacity handler table, two ring FIFOs, one-frame local latency, no reflection; registration-order dispatch; Net-free (channels bind through an interface) |
| Reliable channel / `bReliable` RPC flag | `TReliableChannel` | Messaging (0.3.0) | Opt-in per channel by wrapping the binding; sequence + ack + retry (bounded by `MaxSendAttempts`, then counted lost) + a 32-wide duplicate window, so a delivered message arrives exactly once; no ordering across messages, no reliable broadcast |
| Platform adapters (time / UDP / LoRa) | `FEsp32TimeSource`, `FHostUdpDriver`, `FEsp32UdpDriver`, `FEsp32E32LoraDriver`, `WriteEsp32LogRecord` | platform-host / platform-esp32 (0.3.0, non-portable) | Supply the real transports and clock behind `INetDriver` / `TimePointMilliseconds` / `FOutputDeviceFunction`; depend inward on portable packages, never the reverse |

`TObjectPtr` is a traced managed reference, `TWeakObjectPtr` observes without
retaining, and `TStrongObjectPtr` is an explicit external root. They are not
general-purpose replacements for normal ownership.

`F`, `T`, `E`, `I`, and `b` follow the local naming style. `U` and `A` are
reserved for real MicroWorld managed types; they do not claim Unreal inheritance
or compatibility.

Not part of the engine: reflection generation, replication/RPC, background
tasks, universal hardware APIs, editor tooling, rendering, physics, audio,
navigation, or asset systems.
