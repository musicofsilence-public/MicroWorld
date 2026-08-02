# UE-Style Concept Map

MicroWorld borrows a few useful C++ concepts from UE. It is not source, binary,
editor, or asset compatible with UE.

| Familiar concept | MicroWorld | State | Difference |
| --- | --- | --- | --- |
| Application root | `FApplication` | Application (0.4.0) | Holds one `IEngine&`; begin/tick/end forwarding is sealed behind private methods and `OnConfigure` is the only hook that reaches the world |
| Run loop / entry point | `FApplication::Run` | Application (0.4.0) | Owns the begin/advance/end sequence as a member template on an injected clock and pacing function; a platform supplies a clock and a sleep instead of a hand-rolled `for (;;)` |
| Primary tick | `FTickFunction` | Core (0.4.0) | Caller supplies time; no tick groups or catch-up bursts |
| Managed object | `UObject`, handles, roots, GC | Engine (0.4.0) | Fixed caller-owned storage and explicit tracing |
| Managed World / Actor / Component | `UWorld`, `AActor`, `UActorComponent` | Engine (0.4.0) | Application roots World; World/Actor trace children; parent references are weak |
| Dynamic spawn / destroy | `UWorld::SpawnActor` / `DestroyActor` / `ApplyPending` | Engine (0.4.0) | Queue at the call site either before or during play: a pre-play queue drains when the world begins, and during play one deferred barrier per frame (destroys before spawns); capacity counts live + pending; transactional rejections |
| Timers | `TTimerManager<MaxTimers, InlineTimerCallbackBytes>` | Engine (0.4.0) | Fixed capacity, caller time, explicit OneShot/Looping mode allowlist, single-pass post-dispatch compaction, deterministic insertion-order dispatch, no catch-up bursts |
| Application entry point / game instance | `TEngine<TTraits>` behind `IEngine` | Engine (0.4.0) | Owns registry/store/GC/world/timers and one bound engine system; fixed 7-step frame order (system PreAdvance → Timers → World.Advance → ApplyPending → Store.ApplyPendingDestroy → GC slice → system PostAdvance) |
| Tickable engine subsystem | `IPlaySystem`, `TPlaySystemSet` | Core / Engine (0.4.0) | Four turns: BeginPlay, PreAdvance, PostAdvance, EndPlay. `TEngine` binds exactly one; a set composes several with add-order start and reverse-order shutdown. The interface lives in Core so Messaging and Transport can implement it without seeing Engine |
| Networking with roles | `ENetworkMode`, `TTransportHost<MaxPeers, MaxPacketBytes>`, peers, channels | Transport (0.4.0) | Standalone / Client / ListenServer / DedicatedServer; bounded peer table; Hello/Welcome admission, heartbeats, timeout eviction; channel 0 reserved for control; simple messages, not replication |
| Network byte I/O | `Core::ITransportDevice`, `TTransportManager`, `FByteWriter`/`FByteReader`, `Core::FDeviceAddress`, `THostLoopback` | Core / Transport (0.4.0) | One non-blocking addressed device contract owned by Core so Messaging can send without depending on Transport; fixed-capacity caller-storage-backed manager, bounded bytes over caller-owned spans, transactional failure semantics, deterministic host loopback independent of Engine |
| Wire framing | `MicroWorld/Transport/FrameCodec.h` (`TFrameDecoder`, `EncodeFrame`, CRC-16/CCITT-FALSE) | Transport (0.4.0) | Portable, host-tested; RadioE32 owns the E32 transport state that uses it |
| Gameplay Message Subsystem (broadcast/targeted message bus) + channels | `FMessagingSystem`, `FChannelInformation`, `FMessage` | Messaging (0.4.0) | Created and driven by the engine. A channel is a name plus a reliability flag plus an optional device and address; a message is a name id plus an opaque payload. Subscriptions take an optional message-name filter and an optional weak owner, so an owner that dies makes its subscription inert. Local delivery is synchronous inside the send call — a subscriber may send from its own callback. No reflection, no property sync |
| Reliable channel / `bReliable` RPC flag | `FChannelInformation::bIsReliable` | Messaging (0.4.0) | One boolean at channel creation, not a wrapper. Sequence + ack + retry bounded by an attempt budget, then counted abandoned. Point-to-point: a channel acknowledges to its own configured address, so both ends must be known up front. At-least-once — there is no receiver-side duplicate suppression, and no reliable broadcast |
| Optional E32 transport | `MicroWorld::Transport::FE32LoraDevice`, `MicroWorld::Platform::Esp32::FEsp32LoraDevice`, `MicroWorld::Platform::Pico::FPicoLoraDevice` | Transport + platform facades | RadioE32 owns portable framing; ESP32/Pico own UART SDK lifetime and compatibility facades; direct callers advance TX through `PreAdvance`, while `TTransportHost` already does |

`TObjectPtr` is a traced managed reference, `TWeakObjectPtr` observes without
retaining, and `TStrongObjectPtr` is an explicit external root. They are not
general-purpose replacements for normal ownership.

`F`, `T`, `E`, `I`, and `b` follow the local naming style. `U` and `A` are
reserved for real MicroWorld managed types; they do not claim Unreal inheritance
or compatibility.

Not part of the engine: reflection generation, replication/RPC, background
tasks, universal hardware APIs, editor tooling, rendering, physics, audio,
navigation, or asset systems.
