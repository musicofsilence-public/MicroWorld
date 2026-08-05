# UE-Style Concept Map

MicroWorld borrows a few useful C++ concepts from UE. It is not source, binary,
editor, or asset compatible with UE.

| Familiar concept | MicroWorld | State | Difference |
| --- | --- | --- | --- |
| Application root | `FApplication` | Application (0.5.0) | Holds one `IEngineRuntime&`; begin/tick/end forwarding is sealed behind private methods, while `OnConfigure` uses retained concrete dependencies explicitly |
| Run loop / entry point | `FApplication::Run` | Application (0.5.0) | Owns the begin/advance/end sequence as a member template on an injected clock and pacing function; a platform supplies a clock and a sleep instead of a hand-rolled `for (;;)` |
| Primary tick | `FTickFunction` | Core (0.4.0) | Caller supplies time; no tick groups or catch-up bursts |
| Managed object | `UObject`, handles, roots, GC | Engine (0.5.0) | Fixed caller-owned storage and explicit tracing |
| Managed World / Actor / Component | `UWorld`, `AActor`, `UActorComponent` | Engine (0.5.0) | Application roots World; World/Actor trace children; parent references are weak |
| World Subsystem | `UWorldSubsystem`, `UWorld::RegisterSubsystem`, `UWorld::GetSubsystem<T>` | Engine (0.5.0) | Explicit bounded registration and exact-type lookup; no reflection discovery, automatic construction, dependencies, or Tick |
| Dynamic spawn / destroy | `UWorld::SpawnActor` / `DestroyActor` / `ApplyPending` | Engine (0.5.0) | Queue at the call site either before or during play: a pre-play queue drains when the world begins, and during play one deferred barrier per frame (destroys before spawns); capacity counts live + pending; transactional rejections |
| Timers | `TTimerManager<MaxTimers, InlineTimerCallbackBytes>` | Engine (0.5.0) | Fixed capacity, caller time, explicit OneShot/Looping mode allowlist, single-pass post-dispatch compaction, deterministic insertion-order dispatch, no catch-up bursts |
| Application entry point / game instance | `TEngine<TTraits>` behind `IEngineRuntime` | Engine (0.6.0) | Owns registry/store/GC/world/timers and optional Messaging then Networking; lifecycle order is bound devices → Messaging → Network → World on begin/pre, then World → Network → Messaging → bound devices on post/end |
| Tickable engine subsystem | `IPlaySystem`, `TPlaySystemSet` | Core / Engine (0.6.0) | Four turns: BeginPlay, PreAdvance, PostAdvance, EndPlay. The interface lives in Core so Messaging, Networking, and Transport can implement it without seeing Engine; composition owns device advancement |
| Networking with roles | `FNetworkSystem`, `ENetworkRole`, `FPeerId` | Networking (0.6.0) | Client or Server only; four generation-checked peers; admission, heartbeat/timeout, addressed send and server broadcast over Messaging. Routes and device addresses remain internal; no authentication or replication |
| Network byte I/O | `Core::ITransportDevice`, `TTransportManager`, `FByteWriter`/`FByteReader`, `Core::FDeviceAddress`, `THostLoopback` | Core / Transport (0.4.0) | One non-blocking addressed device contract owned by Core so Messaging can send without depending on Transport; fixed-capacity caller-storage-backed manager, bounded bytes over caller-owned spans, transactional failure semantics, deterministic host loopback independent of Engine |
| Wire framing | `MicroWorld/Transport/FrameCodec.h` (`TFrameDecoder`, `EncodeFrame`, CRC-16/CCITT-FALSE) | Transport (0.4.0) | Portable, host-tested; RadioE32 owns the E32 transport state that uses it |
| Gameplay Message Subsystem (broadcast/targeted message bus) + channels | `FMessagingSystem`, `FChannelInformation`, `FMessage` | Messaging (0.6.0) | Fixed links and explicit routes, named channels, bounded payload codecs, and synchronous local delivery. Network application channels remain local-only; Networking chooses private wire channels and remote routes |
| Reliable channel / `bReliable` RPC flag | `FChannelInformation::bIsReliable` | Messaging (0.6.0) | One declaration selects bounded at-least-once delivery. A never-reused 64-bit id and complete route scope ACK/retry state, so delayed or wrong-route ACKs cannot release another send |
| Optional E32 transport | `MicroWorld::Transport::FE32LoraDevice`, `MicroWorld::Platform::Esp32::FEsp32LoraDevice`, `MicroWorld::Platform::Pico::FPicoLoraDevice` | Transport + platform facades | RadioE32 owns portable framing; ESP32/Pico own UART SDK lifetime and compatibility facades; the composition owner advances TX through `PreAdvance` |

`TObjectPtr` is a traced managed reference, `TWeakObjectPtr` observes without
retaining, and `TStrongObjectPtr` is an explicit external root. They are not
general-purpose replacements for normal ownership.

`F`, `T`, `E`, `I`, and `b` follow the local naming style. `U` and `A` are
reserved for real MicroWorld managed types; they do not claim Unreal inheritance
or compatibility.

Not part of the engine: reflection generation, replication/RPC, background
tasks, universal hardware APIs, editor tooling, rendering, physics, audio,
navigation, or asset systems.
