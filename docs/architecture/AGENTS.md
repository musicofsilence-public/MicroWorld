# MicroWorld LikeC4 Architecture

Inherits `../AGENTS.md`.

**General LikeC4 rules are not repeated here.** Element kinds, the subtitle rule,
description rules, tag rules, and LikeC4 syntax gotchas live in the
`likec4-modelling` skill. This file records only what MicroWorld decided, which is
the part that cannot be re-derived from the tool or its source model.

## Architecture

- `specification.c4` — the vocabulary. Eight element kinds, one tag (`optional`),
  seven relationship kinds.
- `model.c4` — elements, evidence-backed relationships. One `model { }` block; no
  deployment block (hardware is the `device` kind).
- `views.c4` — presentation only. Five views: one per level per scope, titled
  `[Cn] <scope>` with nothing after the scope. A view that needs a box the model
  lacks means the model is wrong. The C1 view keeps the reserved id `index`.

Generated previews go to `build/`, never beside the source.

## Concepts

### Systems carry the team's words, not the build's

`Core`, `Engine`, `Transport`, `Messaging System`, `Application`, `Networking`. Four
are the module names, because there the module name **is** the domain vocabulary. An
earlier revision replaced all six with invented abstractions ("Runtime Substrate",
"Engine-Network Integration") and forced every reader to translate before they could
find anything. Do not reintroduce a private vocabulary.

Two are renamed, and the trigger was the **name**, not the boundary: `Net` beside
`Net System` differed only by a generic suffix, so the pair read as one wrapping the
other and kept producing "shouldn't we merge these?" about the one boundary the model
exists to protect. `Transport` and `Networking` share no word, so the two altitudes are
legible before any metadata is read. Neither title came from the code — `TNetSystem`
named the packaging, `Integration` named where the code sits.

The architectural content is the **boundary**, not the name. But if the name hides the
boundary, readers will not see it.

### The subtitle names the role, never a directory

Every system carried a path once — `system: Modules/MicroWorld/Core`, `system: Modules/MicroWorld/Application`.
Five of six merely restated the title, and since the subtitle renders
second-most-prominently, the effect was that **directory paths supplied the diagram's
vocabulary**: the code driving the view, in the most visible position it could occupy.

Deleting them was the wrong repair — it cost six labels to fix six sentences. The subtitle
is a good slot; it was carrying the wrong content.

The `[<kind>: <detail>]` format is a general rule and lives in the skill. What is
MicroWorld's decision is the **detail** for a system: its **role**, the thing the layout
implies and never states.

| System | Subtitle |
| --- | --- |
| Core | `[system: foundation]` |
| Engine | `[system: managed runtime]` |
| Messaging System | `[system: play service]` |
| Transport | `[system: byte I/O]` |
| Application | `[system: entry point]` |
| Networking | `[system: play service]` |

The two `play service` lines are identical on purpose: it is how the model already shows
kinship, exactly as the two boards both read `device: microcontroller`. What separates
those two systems is their edges, which the diagram draws.

Both say `play service` because both implement `IPlaySystem` — the Core contract with four
turns: `BeginPlay`, `PreAdvance`, `PostAdvance`, `EndPlay`. It was called `IEngineSystem`,
which said Engine while the whole point is that implementers must not need Engine. The new
name was chosen for closeness to the UE5 API, not because it is the nicest word; the owner
said as much when picking it. `ITickableSystem` was rejected because Core's `FTickable`
already means cadence scheduling, and `IFrameSystem` because a frame here is a network
packet. Do not reopen this looking for a better word.

Two facts that an earlier draft crammed in here do not fit the format and are already
stated elsewhere — Engine's two packages and Transport's containment of every medium live
in `metadata { boundary }` and in the `c2Systems` description. Paths do not live anywhere
in the model at all; see **The code is not the authority** below.

`Transport`'s detail is `byte I/O`, not `transport`: after the rename the old detail
merely restated the title, which is the defect this section exists to prevent. The role
it names is the altitude — raw bytes, no semantics — which is exactly what separates it
from `Networking` one layer up.

### The code is not the authority — none of it

Boundaries are decided by responsibility. Everything the repository currently
contains — packages, headers, include sets, class names, example programs — records
where a responsibility lives *today*. None of it decides where the line should be,
and where the two disagree **the code is the defect to fix**, not the model.

This is wider than the build files, and the wider form is the one that matters: an
argument from an `#include` list or from "there is already a class called that" is the
same error as an argument from `target_link_libraries`. It reasons from the artifact
to the design, and the arrow only runs the other way.

Three things here disagree with the packaging on purpose, which is the check that the
rule is real: **Engine** is one system over two modules, **Transport** holds every
medium including the separately packaged radio, and neither **Transport** nor
**Networking** is named after the module it contains.

**File references do not stay** (owner, 2026-07-30): *"We must not care about the code,
the goal is well defined architecture, everything else will be updated later."* A
`traceability` key existed for "where does this live today?" and carried 24 entries;
all were removed. The model describes the target, and the repository is what gets
changed to match it.

Three metadata keys remain, and they must not be mixed:

| Key | Answers |
| --- | --- |
| `contract` | what does this system expose? |
| `standalone` | what can you build with **just** this? |
| `boundary` | why is the line drawn here? |

All three are decisions. None of them may cite a file, a header, an include set, or a
class name as a *reason* — naming a type as vocabulary is fine, naming one as evidence
is the error this section exists to prevent.

`standalone` exists because the capability and the verdict get cited separately and read
badly crammed together — see `messagingSystem`, where "usable with no networking at all"
is the decision and "so it is not part of networking" is what follows from it. Both are
architecture, and neither needs a header to back it up.

### Boundary verdicts for this repository

Recorded because they are the decisions someone will otherwise re-litigate:

| Subject | Verdict | Reason |
| --- | --- | --- |
| `Object` + `Engine` | **one** system, titled Engine | Object owns identity, Engine owns the lifetime built on it; neither is a responsibility anything wants alone |
| `Net` + every medium | **one** system, titled Transport | it sits below the engine; a named medium — Wi-Fi, a wire, a radio — only *satisfies* the device contract and declares nothing outward, which makes it an implementation |
| `Transport` + `Networking` | **two** systems | merged, one element would own both the byte contract and the message types, and nothing would forbid a device from knowing a message type. Asked and re-decided once already — the reason to merge came from the old names, not from the structure |
| `Messaging System` | **own** system | it is **usable with no networking at all** — a message between two actors on one board must not travel through a transport, so its contract names neither actors nor transports. Folding it into Networking would assert the opposite |
| `Core` + `Engine` | **two** systems | Core is the shared vocabulary that lets independent systems interoperate. Merge it and Transport → Core becomes Transport → Engine, collapsing the graph |
| `Platform*` | **outside the product** | not ours to own, so position says so; they are elements at C3 Transport, drawn outside the `microworld` boundary. An earlier verdict kept them out of the model entirely and named them in subtitles instead — overruled, see below |
| Who pumps a device | **its owner** — an application or Networking, never both | a callback cannot fire itself, so `Device Interface` carries the four `IPlaySystem` turns alongside its three operations. The owner's usage holds a device directly with no Networking anywhere, which is precisely the case Networking cannot cover — hence Application's `uses` edge to Transport, matching the one it already has to Messaging |
| Delivery guarantees | **Networking**, not Transport or Messaging | an acknowledgement is a message from the peer, so a byte layer that read one would have stopped carrying payload opaquely — which rules Transport out on its own contract. Messaging keeps the *declaration* only: a binding says it wants reliable delivery, and locally the call satisfies that for free. Sequencing, acknowledgement and retransmit are Networking's because they mean nothing without a wire, and Messaging must stay usable with none |
| Splitting an oversized message | **Networking**; a device publishes its limit and refuses | E32 frames are small, so this will happen. A device that reassembles holds state about traffic it was told to carry opaquely — the same argument that moved reliability out, and this is the exact spot where that machinery would otherwise creep back in. Networking already sequences, so it is the cheapest place for it |

### Depth

C1 is the product boundary and its purpose. C2 is six contract-defined systems.
C3 exists in exactly three of them, because these are where a contributor
reliably guesses wrong:

- **Engine** — the application-facing runtime boundary, deferred spawn barrier and
  generation-checked handles, plus where the `Engine + Object` merge gets unpacked.
- **Messaging System** — local delivery, reliable delivery, and the Core
  `IPlaySystem` lifecycle contract.
- **Transport** — the one device shape, and the media that realise it.

Do not add C3 elsewhere for symmetry.

### C3 Transport is flat, because that is how it is used

The owner's usage decides this level, and it holds a medium device directly:

```cpp
FWifiDevice WifiDevice;
WifiDevice->Setup(...);
WifiDevice->SendDataTo(Address, Data);
WifiDevice->OnDataReceived.Subscribe(...);
```

There is no layer above the devices. **The application holds a device and calls it
directly**, so one element — `Device Interface` — is both what an application writes
against and what each medium realises. Choosing the medium is not something to
abstract away; it is the point of the call site.

An intermediate revision stacked a separate application-facing interface and a packet
queue above the media. Both were read off the existing headers rather than derived
from the usage, and the usage has no room for either. If a future revision proposes
that stack again, the question to ask is which line of the call site above needs it —
not which class currently exists.

Two responsibilities left C3 Transport with that stack, and neither should return here:

| Dropped | Why it is not Transport |
| --- | --- |
| the outbound queue | how a device holds what it cannot send yet is beneath this level; the *contract* promises a send that never blocks, and that promise is on the contract |
| peer sessions — roles, admission, heartbeat, eviction | `SendDataTo(Address, Data)` has no handshake and no peer table. Admission is semantics, and Transport is byte I/O — it belongs to `Networking` if it is wanted at all |

### One contract, uniform in its data path and deliberately not in Setup

`SendDataTo(Address, Data)` reads the same on every medium, which is what makes the
media interchangeable at a call site. `Setup` is the opposite and stays per-medium,
because an SSID and a baud rate have nothing in common.

That asymmetry is the reason an application names a concrete device type — it needs
`FWifiDevice` to configure it, and nothing concrete afterwards.

Uniform is the *shape*, not the meaning: each medium reads the address its own way, and
a point-to-point one ignores it. The wired buses below show how far that spreads.

The rest of what a device promises, none of which a diagram can show:

- **`Setup` answers with a Core result code**, and a device before `Setup` is unusable.
- **A device is also a play system** — whoever owns it installs it.
- **Send never blocks.** It answers that it had no room; how a device holds what it
  cannot send yet is beneath this level.
- **The span handed to a subscriber belongs to the device**, and is valid only for the
  duration of that call. Copy it or lose it.
- **A device publishes its largest payload** and refuses anything longer.
- **Broadcast is a value of the address shape**, not a second operation. A medium that
  cannot broadcast refuses it, exactly as it would any address it cannot reach.

### Which media must frame, and which must not

`SendDataTo` carries a bounded message, so what a medium delivers decides whether
framing is needed at all:

| Medium | Arrives as | Frame codec |
| --- | --- | --- |
| Wi-Fi, Bluetooth | bounded datagrams | no edge |
| LoRa, Wired | a continuous stream | recovers boundaries |

The two devices with **no** edge to the frame codec are the most useful thing the
view says, so the rule is written into the `c3Transport` description. An earlier
revision drew framing *above* the device, as `Transport Host -> Frame Codec`. That
was false in both directions: nothing above a device frames, and UDP never frames
at all.

### An interface is its own kind, and it replaced a tag

`Play System Interface`, `Transport Device Interface` and `Engine Runtime Interface`
could be mistaken for `component` elements. The first two once carried a `#contract`
tag; the third arrived after the kind replaced that tag. None is honestly a
`component`: a component does work, while an interface only declares the shape others
satisfy. `entity` is no better: that kind means a stateful concept with identity and a
lifetime, which an interface has neither of.

So `interface` is a kind. It cost nothing to add, because `#contract` marked exactly
those two elements — identical membership means the tag and the kind were one
distinction wearing two names, and the kind is the honest half. The tag is deleted.

Both carry the same ball-and-socket glyph, which is correct: they are the same sort
of thing seen in two different systems. The shape stays `rectangle` — none of the
eight available shapes depicts an interface, and borrowing a near-miss would assert
something false.

The Engine component is the counter-example that keeps the boundary sharp: it does
work — owns the world, the object store and the timers — so it is a `component`
(`[component: TEngine<TTraits>]`), not an interface. `IEngineRuntime` is a box because
it owns the narrow begin/tick/end contract Application is written against; world,
storage, messaging and typed creation remain on the concrete component.
The C2 `Application` to `Engine` edge names `EngineRuntime` as what crosses the
boundary, and the C3 `realisedBy` edge uses the model's deliberate reversed
realisation direction to place declaration above implementation.

### The kind is `component`, not `domainService`

"Domain service" is DDD vocabulary: behaviour expressed in the *business* language.
MicroWorld has no business domain, so the word claimed something that does not exist,
and it was most obviously false on a byte framer. Its four members were always
something plainer, and C4 already had the word for it; the spec carries the definition.

`Subscription` is also a component: it packages the delegate, optional message-name
property, and delivery logic a Channel invokes for subscription delivery. It owns neither an
independent lifecycle nor a contract other code is written against, so `entity` would
overstate its architectural role.

`service` and `mechanism` were rejected for saying only that a thing does something.
`protocol` fits `Frame Codec` and nothing else, and a kind with one member is a
description wearing a classification's clothes. `module` is the closest synonym and
the worst choice available, because this repository has `Modules/` directories — the
word already means build target here.

The collision with `Actor Component` is real and survivable, because it exists only in
conversation. The kind never renders in that element's subtitle, which reads
`[entity: UActorComponent]`, and `Actor Component` stays an `entity`: it has identity
and a lifetime, which is what that kind means and what `component` does not.

### "Device" is overloaded on purpose

`device` is an element **kind** meaning hardware — ESP32-S3, RP2040 — and the C3
Transport elements are `component`s titled `Wi-Fi Device`, `LoRa Device` and so on.
The collision is deliberate: the owner's own code says `FWifiDevice`, and this file's
first rule is that systems carry the team's words. The subtitle prefix disambiguates
(`[component: …]` against `[device: microcontroller]`), and the two never share a
view — hardware lives at C1, media at C3.

### One element per medium, not per class

`Wired Device` holds UART, I2C and SPI in one box. They differ in wiring, in who clocks
the line, and in how a peer is named — I2C takes an address per send, SPI selects its
peer at Setup with a chip-select line, UART has only the one it is wired to. None of
that changes the shape: all three realise the same interface, all three must frame, all
three land on one platform. Splitting them would add two boxes that say what the first
already said.

That spread is also the honest limit on "one address shape". The shape is uniform so a
call site reads the same on every medium, which is worth keeping; the *meaning* is
per-medium, and a point-to-point device ignores the argument. Claiming more than that
would be claiming something the buses do not do.

For the same reason `Wi-Fi Device` is **one** element across two platforms rather
than a board box and a host box. The platform difference is real, and it is on the
arrows below the element — which is where a difference that does not change the
medium belongs.

Medium subtitles name the medium, never the platform: `[component: UART, I2C, SPI]`,
not `[component: ESP-IDF UART, I2C, SPI]`. They used to name the SDK, and once the
platform elements existed that was the same fact written twice — with the weaker copy
in the more prominent slot.

### Platform implementations are elements, and they sit outside the product

**"We depend on platform specific implementation and do not invent our own"** (owner,
2026-07-30) is one of the load-bearing claims of this project, and it needs to be
*structural*. `ESP-IDF`, `Pico SDK` and `Host OS` are elements drawn **outside the
`microworld` boundary**, which is how the skill says ownership is expressed — inside
is ours, outside is not. A medium wraps somebody else's radio or bus driver; it does
not implement one.

Three earlier attempts were weaker and all were rejected:

| Attempt | Why it failed |
| --- | --- |
| name the SDK in the **subtitle** (`[component: ESP-IDF UART, I2C, SPI]`) | a fragment of a label carrying a claim this size; the reader has to notice and interpret it |
| a `#nonPortable` **tag** | invisible without a view style rule, and says nothing a subtitle does not |
| a **colour** on the media | a second lever on elements that already carry an icon |

The arrows are the payoff, and they are asymmetric on purpose — that asymmetry *is*
"each board has its own implementation":

- **ESP-IDF** realises every medium.
- **Host OS** realises only Wi-Fi, which is why a development host runs UDP tests and
  nothing else.
- **Pico SDK** reaches only LoRa.

Core reaches two of them as well — ESP-IDF for `Time` and `Log`, Host OS for `Time`
— because a frame loop needs a clock before any medium exists. Pico SDK is absent
there: that edge ships only the byte stream underneath LoRa.

And **LoRa's arrows read differently from every other medium's**: it is not *realised
by* a platform, it *takes a byte stream from* one. The E32 protocol is ours and
portable; only the wire underneath is not. That single label difference states the
portability claim that a subtitle previously had to assert.

`color slate` on the three matches the neutral treatment the `device` kind already
gets, for the same reason: something we do not own should not compete with what we do.

### Bluetooth is modelled before it is written

Nothing implements it, and the model does not mention that, because no element here
describes an implementation. This is the `code is not the authority` rule running
forwards: the architecture decides which media exist and the code follows. Do not
delete Bluetooth as speculative — it is a stated requirement, not a guessed one.

### The invariant the model must keep visible

Engine and Transport never reference each other, and **no system joins them** — only
the application entry point does. `PlaySystem.h` in Core is what lets a play service extend a
frame loop it cannot see, which is why neither `Messaging System` nor `Networking` needs
Engine in order to be installed in one.

`Networking` was once drawn as the sanctioned exception, with an edge to Engine labelled
"advances the world each frame". That inverted control — an engine service is *called by*
the loop, it does not drive it — and `Messaging System` had already shown the dependency
was unnecessary, delivering messages between actors on Core alone. Networking sits further
from actors than Messaging does, so it cannot need more. Removing the edge made the absence
total, which is the stronger statement.

This is stated in the `c2Systems` description — **not** in a view of its own. A filtered
view of C2 would be the same diagram minus two boxes, and an absent edge is exactly as
absent either way.

### Styling carries meaning or it is not applied

| Lever | Meaning here |
| --- | --- |
| `icon` | the primary lever — a bundled logo where one is literally correct (`tech:cplusplus`, `tech:raspberry-pi`), otherwise one of our own SVGs |
| `shape` | only where the element *is* the depicted thing — `mobile` hardware, `browser` the dev host, `storage` the object store, `queue` the router |
| `color` | **not used on systems.** Only `muted` on the product boundary and `slate` on the `device` kind, both to keep a neutral back from competing |

Two levers are deliberately absent, both settled by looking at exports:

- **`border`.** On a filled box a dashed border has almost no exposed edge and is
  invisible at reading size — three dashed elements were indistinguishable from their
  solid siblings.
- **`color` as a role tier.** A four-hue scheme (foundation / runtime / transport /
  composition) was built, rendered, and then removed: once every system carries a
  glyph of what it does, a hue on top is a second encoding the reader cannot decode
  without a legend. The tiers were never lost — `autoLayout TopBottom` already stacks
  them in dependency order, composition above runtime above foundation.

The general rule this leaves: **one visual lever per element, and it should be the one
that needs no key.**

### Our own icons

Twelve `icon-*.svg` files sit beside the `.c4` files, referenced as `icon ./icon-transport.svg`.
They render in `export png` with no network, the same as the bundled sets.

| Element | Glyph | Why it is honest |
| --- | --- | --- |
| Core | stacked plinth | the foundation everything else rests on |
| Engine | globe | `UWorld` is literally the central type |
| Messaging System | envelope | typed messages, delivered |
| Transport | frames on a wire | "moves framed bytes" drawn |
| Application | loop with an arrowhead | it owns the `Run` frame loop |
| Networking | two lines merging into one | the only place Messaging and Transport meet |
| Wi-Fi / Wired / LoRa Device | network, cable, antenna | the physical medium, the one thing they differ by |
| Bluetooth Device | the Bluetooth rune | the medium has a real mark; one path, no interior detail |
| Play System / Transport Device / Engine Runtime Interface | ball-and-socket connector | the standard notation for a provided interface; shared because all three are one kind |
| every `component` | the UML component symbol | the standard notation for one. Set on the **kind** in `specification.c4` rather than on each element, so a new component carries it for free — `interface` predates that and still sets its icon twice |

Two rules learned by looking at exports rather than at SVGs:

- **Three or four strokes, no interior detail.** The first `icon-wired.svg` had pins
  inside the connector outlines and turned to mush at render size.
- **Hardcode the stroke; `currentColor` does not inherit.** It resolves to black, not
  to the label colour. `#dbeafe` works because every icon-bearing element keeps the
  default saturated fill — never put one on a `muted` or `secondary` element, whose
  rendered fill flips between light and dark across exports of the same source.

Shape cannot do this job instead: five of the eight values are literal pictures of a
browser or a phone, so there are not enough honest shapes for six systems. That is the
tool's ceiling, and it is the reason drawn icons are worth the maintenance.

ESP32-S3 still has **no** icon: the set has no `espressif`, `esp32` or generic-MCU
glyph, and a Wi-Fi symbol would label a capability rather than the thing. A drawn
chip outline is now possible, but RP2040 carries a real Raspberry Pi logo, so mixing a
hand-drawn mark with a brand mark trades one asymmetry for another. Left open.

### Dependency edges name what crosses them

`takes Time, Timer, Lifecycle, PlaySystem`, not `depends on`. The default
relationship title restates the arrow and tells a reader nothing. Each label names
the concepts that cross the boundary — which is a design statement about what one
system is allowed to know of another, not a summary of anyone's include list.

`realisedBy` is the one exception, and it is deliberate: its kind title *is* the
information, so those edges carry no label of their own.

### `realisedBy` points against the dependency, and that was measured

Every other edge in this model points the way the dependency runs. This one does not,
and the reason is a layout failure that was observed rather than guessed.

With realisation drawn the true way — each medium pointing up at the interface it
satisfies — **every edge in C3 Transport left a medium.** The media were the only
sources, so the layout engine had two ranks to work with and put `Device Interface`,
`Frame Codec` and all three vendor SDKs in a single row of five. The interface an
application writes against rendered *beside* the platform implementations. No layout
direction fixes that; the graph shape causes it.

Reversing realisation gives the three ranks the architecture actually has —
declaration, media, platforms — and the kind's own title (`realised by`) keeps the
statement true in the direction it is drawn.

This is a narrow exception, not a licence. The general rule still stands: model
direction by what is true and let the layout follow. It was overridden here only
because the truthful direction produced a picture that asserted something false, and
because a dashed line plus an explicit title make the inversion legible rather than
silent. **Do not reverse a `dependsOn` edge for layout** — that lie has no label to
rescue it.

### Two counts, deliberately different

This model has **six systems**; root `CLAUDE.md` has **eleven packages**. A
system is a responsibility, a package is a build target. Do not reconcile them —
each file cross-references the other.

## Verification

```powershell
likec4 validate docs/architecture
```

```powershell
python tools/CheckFolderAgents.py --root docs
```

That is the whole gate. **Do not export PNGs** — review happens in the live dev server,
so an export plus an image read is pure overhead.

Reason about what a change does to the render from the model source instead: `title`, `technology`
and `description` are rendered, `metadata { }` never is. So a metadata-only edit cannot
change a diagram, and an edit to a subtitle or a description changes the most prominent
text on it.

If a render is ever wanted, `export png --flat` needs its output directory deleted first —
it leaves orphan PNGs for renamed views, and a stale image reads as confirmation.
