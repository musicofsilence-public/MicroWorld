# MicroWorld Messaging System

Inherits `../../AGENTS.md`.

## Architecture

Messaging is the portable actor-messaging system. Its dependency direction is
`Core <- Messaging`: it may depend only on Core and the C++17 standard library.
Engine, Transport, Networking, platform, SDK, and transport-driver headers must
not appear here.

The system owns message vocabulary and codecs, bounded routing, channel
interfaces and bindings, and bounded reliable delivery. Its router and reliable
channel participate in caller-owned frame ordering without owning a world or
engine. It is transport-agnostic: callers provide a channel or a duck-typed
network facade at the edge rather than giving Messaging a Transport dependency.

There is no production translation unit: every primitive is a template or an
inline codec whose caller-selected capacities must stay visible at instantiation.
Any source file added later may depend only on Messaging's public headers and
Core, and must introduce no hidden transport, engine, clock, heap, or SDK
coupling.

## Concepts and boundaries

- All routing, handler, queue, retry, and frame-set capacities are caller
  selected and fixed at compile time; steady-state message work allocates
  nothing and reads no hidden clock.
- Message delivery remains queued and deterministic. Time arrives from the
  caller, and physical transport policies stay outside this system.
- Public symbols live in the flat `MicroWorld` namespace below the `Messaging/`
  include layout. The system never owns worlds, actors, engines, network hosts,
  drivers, or platform resources.

## Composition recipes

Four shapes cover every wiring this system supports. Frame order is the rule
that makes them work: **host play systems are added before the router**, so inbound
bytes are decoded in the same tick they are routed.

Standalone world, local messaging only — the router *is* the network frame:

```cpp
static TMessageRouter<16, 8, 96, 1> Router;              // handlers, queue, bytes, channels
static TEngineHost<8, 16, 512, 16, 2, 4, 8, 64> Engine{Budget, Router};
// actors take Router by IMessageRouter&, and subscribe in BeginPlay via AddMessageHandler
```

Client/server over one wire — server side shown:

```cpp
static FEsp32UartDriver Driver{{.UartPort = 1, .TxGpio = 17, .RxGpio = 18,
                                .BaudRate = 115200, .LocalNodeId = 1}};
static TTransportHost<2, 120> Net{Driver};               // Configure(DedicatedServer) + Start
static TMessageRouter<16, 8, 96, 1> Router;
static TMessageChannelBinding<decltype(Net)> Commands{Net, /*wire*/1, /*id*/1,
                                                      EChannelSendTarget::AllPeers, Router};
static THostPlaySystem<decltype(Net)> NetFrame{Net};
static TNetworkFrameSet<2> Frames;                       // Add(NetFrame); Add(Router);
static TEngineHost<...> Engine{Budget, Frames};
// after wiring: Router.AddChannel(Commands);
```

Two drivers, two channels, one world: a second driver, a second `TTransportHost`, and
a second binding with a different `FMessageChannelId` — both net frames added
before the router.

Guaranteed channel — the reliable wrapper sits between binding and router in
both directions. Wrapper and binding each hold the other by reference, a
construction cycle broken by one deliberate two-phase setup:

```cpp
static TReliableChannel<8, 96> Reliable{Router /*forward sink*/, {}};
static TMessageChannelBinding<decltype(Net)> Wire{Net, /*wire*/1, /*id*/1,
                                                  EChannelSendTarget::Server,
                                                  Reliable /*inbound sink*/};
static TNetworkFrameSet<3> Frames;
// at startup, in this order:
//   Reliable.SetInnerChannel(Wire);   // outbound: router -> reliable -> wire
//   Router.AddChannel(Reliable);      // AFTER SetInnerChannel: GetChannelId needs the inner id
//   Frames.Add(NetFrame); Frames.Add(Reliable); Frames.Add(Router);
```

## Verification

Build the engine from the repo root; Messaging is the `microworld_messaging`
INTERFACE target. Run the dependency-boundary checker with the Messaging system
root and the Messaging behavior tests after changes. This guide owns durable
boundaries; the system's headers and tests define its current behavior.
