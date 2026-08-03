# MicroWorld Messaging System

Inherits `../../AGENTS.md`.

## Architecture

Messaging is the portable actor-messaging system. Its dependency direction is
`Core <- Messaging`: it may depend only on Core and the C++17 standard library.
Engine, Transport, platform, SDK, and transport-device headers must not appear
here.

The system owns message vocabulary, named channels, subscriptions, wire framing,
and bounded reliable delivery. It is transport-agnostic without depending on
Transport: a channel holds a `Core::ITransportDevice*`, so each medium realises
Core's interface and Messaging never names one. A channel with no device is the
local mode, not a degraded one.

`FMessagingSystem` is a `Core::IPlaySystem`, and the engine that created it
drives its turns. It never drives a device's own `PreAdvance` or `PostAdvance`:
those belong to whoever composed the device, so a device shared by several
channels is not ticked twice.

`FMessagingSystem` is one fixed-capacity compiled system. Its declarations live
in `MessagingSystem.h`, while its definitions are split across exactly three
source partitions: `MessagingSystem.cpp`, `MessagingSystem_WireReceive.cpp`, and
`MessagingSystem_FrameCodec.cpp`. The `microworld_messaging` static target
compiles those partitions and depends only on Core. They must introduce no
hidden Transport, Engine, clock, heap, platform, or SDK dependency.

### Fixed-capacity rationale

The values preserve the shipped contract; they are not claims that every limit
is optimal or measured.

| Capacity | Value | Current rationale | Revisit trigger |
| --- | ---: | --- | --- |
| `MaxChannels` | 4 | Preserves the shipped contract; current repository scenarios use no more than two simultaneous channels, so four is compatibility headroom rather than a measured minimum. | A supported application needs more than four, or resource measurement justifies reducing it. |
| `MaxSubscriptions` | 16 | Preserves the shipped subscription/storage contract; no measured minimum currently justifies sixteen. | Measured RAM pressure or a supported world requiring more than sixteen live registrations. |
| `MaxSubscriberCallableBytes` | 32 | Preserves the callable shapes accepted by current examples/tests without heap allocation; the exact margin is not measured. | A supported callable is rejected, or object-size measurement shows the inline storage is excessive. |
| `MaxMessageBytes` | 96 | Preserves the general Messaging payload contract; narrower transports such as LoRa enforce their own smaller packet limit. | A supported transport/application needs a different common payload contract, backed by measurement. |
| `MaxReliablePendingMessages` | 8 | Preserves the shipped reliable in-flight budget; no measured concurrency requirement currently proves eight. | Observed reliable-send exhaustion or measured RAM pressure in pending-frame storage. |

## Concepts and boundaries

- All channel, subscription, message-size, and reliable-pending capacities are
  fixed compile-time constants on `FMessagingSystem`; steady-state message work
  allocates nothing and reads no hidden clock.
- **Local delivery is synchronous.** `SendMessageToChannel` runs matching local
  subscribers inside the send call, before it touches any device. A subscriber
  may send from inside its own callback: the slot being dispatched is marked, so
  a nested delivery cannot re-enter it.
- **Reliable delivery is point-to-point and at-least-once.** A channel
  acknowledges to its own configured address, not to whoever sent the message,
  so both ends must be known when the channel is created. There is no
  receiver-side duplicate suppression.
- A subscription may carry a weak owner, so an owner that dies makes its
  subscription inert and its slot reclaimable instead of dangling.
- Public symbols live in `MicroWorld::Messaging`, matching the `Messaging/`
  include layout; `tools/CheckNamespaces.py` enforces it. The system never owns
  worlds, actors, engines, devices, or platform resources.

## Composition recipes

Every wiring is the same three calls: create the system on the engine, create
the channels, subscribe. There is no binding, no wrapper, and no frame-order
rule for the caller to get right.

Local messaging only — a channel with no device:

```cpp
Engine.CreateMessagingSystem(FMessagingSystemInformation{});
FMessagingSystem* const Messaging = Engine.GetMessagingSystem();
Messaging->CreateChannel({"Local", /*bIsReliable*/ false, /*Device*/ nullptr, /*Address*/ {}});
// actors take FMessagingSystem& by constructor injection and subscribe in BeginPlay
```

Over a wire — name the device the channel sends through:

```cpp
static MicroWorld::Platform::Esp32::FEsp32UartDevice Device{{.UartPort = 1, .TxGpio = 17, .RxGpio = 18,
                                .BaudRate = 115200, .LocalNodeId = 1}};
Messaging->CreateChannel({"App", false, &Device, {}});   // point-to-point ignores the address
```

Two media at once: create a second channel naming the second device. Several
channels may share one device; Messaging drains it once per turn.

Guaranteed delivery is one boolean, and both ends must name each other because
acknowledgements go to the channel's configured address:

```cpp
Messaging->CreateChannel({"Guaranteed", /*bIsReliable*/ true, &Device, PeerAddress});
```

Subscribing, with the optional message-name filter and a weak owner:

```cpp
FMessagingSystem::FSubscriberDelegate Subscriber;
Subscriber.Bind([this](const FMessage& Message) noexcept { this->OnMessage(Message); });
Messaging.SubscribeToChannel("App", "SetLampState", std::move(Subscriber),
    MicroWorld::Engine::MakeWeakOwner(*GetObjectStore(), GetObjectHandle()));
```

## Verification

Build the engine from the repo root; Messaging is the compiled
`microworld_messaging` static target. Run the dependency-boundary checker with
the Messaging system root and the Messaging behavior tests after changes. This
guide owns durable boundaries; the system's headers and tests define its current
behavior.
