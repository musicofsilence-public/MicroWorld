# MicroWorld LikeC4 Architecture

Inherits `../AGENTS.md`.

**General LikeC4 rules are not repeated here.** Element kinds, the subtitle rule,
description rules, tag rules, and LikeC4 syntax gotchas live in the
`likec4-modelling` skill. This file records only what MicroWorld decided, which is
the part that cannot be re-derived from the tool or the DSL.

## Architecture

- `specification.c4` — the vocabulary. Seven element kinds, two tags
  (`contract`, `optional`), six relationship kinds.
- `model.c4` — elements, evidence-backed relationships. One `model { }` block; no
  deployment block (hardware is the `device` kind).
- `views.c4` — presentation only. Four views: one per level per scope, titled
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

Every system carried a path once — `system: Modules/Core`, `system: Modules/Application`.
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
| Application | `[system: composition root]` |
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
in `metadata { boundary }` and in the `c2Systems` description. Paths live in
`metadata { traceability }`, where not rendering is correct rather than a limitation to
work around.

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
rule is real: **Engine** is one system over two modules, **Transport** holds
`Modules/RadioE32`, which is portable and separately packaged, and neither **Transport**
nor **Networking** is named after the module it contains.

File references stay, demoted to what they are. Four metadata keys, and they must not
be mixed:

| Key | Answers |
| --- | --- |
| `contract` | what does this system expose? |
| `standalone` | what can you build with **just** this? |
| `boundary` | why is the line drawn here? |
| `traceability` | where does this live today? |

The first three are decisions. The fourth is replaceable the moment code moves, and
must never be used to justify a boundary.

`standalone` exists because the capability and the verdict get cited separately and read
badly crammed together — see `messagingSystem`, where "usable with no networking at all"
is the decision and "so it is not part of networking" is what follows from it. Both are
architecture. The `#include` set that happens to confirm the first one sits in
`traceability`, phrased as confirmation, because that is all it can ever be.

### Boundary verdicts for this repository

Recorded because they are the decisions someone will otherwise re-litigate:

| Pair | Verdict | Reason |
| --- | --- | --- |
| `Object` + `Engine` | **one** system, titled Engine | Object owns identity, Engine owns the lifetime built on it; neither is a responsibility anything wants alone |
| `Net` + every medium | **one** system, titled Transport | it sits below the engine; a named medium — Wi-Fi, a wire, a radio — only *satisfies* the driver contract and declares nothing outward, which makes it an implementation |
| `Transport` + `Networking` | **two** systems | merged, one element would own both the byte contract and the message types, and nothing would forbid a driver from knowing a message type. Asked and re-decided once already — the reason to merge came from the old names, not from the structure |
| `Messaging System` | **own** system | it is **usable with no networking at all** — a message between two actors on one board must not travel through a transport, so its contract names neither actors nor transports. Folding it into Networking would assert the opposite |
| `Core` + `Engine` | **two** systems | Core is the shared vocabulary that lets independent systems interoperate. Merge it and Transport → Core becomes Transport → Engine, collapsing the graph |
| `Platform*` | **not C2** | the non-portable edge, and implementations besides; they appear inside the C3 driver families that name them |

### Depth

C1 is the product boundary and its purpose. C2 is six contract-defined systems.
C3 exists in exactly two of them, because these are where a contributor
reliably guesses wrong:

- **Engine** — the deferred spawn barrier and generation-checked handles, plus
  where the `Engine + Object` merge gets unpacked.
- **Transport** — the `INetDriver` interface, and the driver families behind it.

Do not add C3 elsewhere for symmetry.

C3 drivers appear by **family**, not one element per class. An eighth box reading
"realises `INetDriver`" teaches nothing the third did not; the class names live in
the subtitle, where enumeration costs nothing. This is also what keeps the Transport
view readable now that IP, wired and radio drivers all live there.

### The invariant the model must keep visible

Engine and Transport never reference each other, and **no system joins them** — only a
composition root does. `PlaySystem.h` in Core is what lets a play service extend a
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

Nine `icon-*.svg` files sit beside the `.c4` files, referenced as `icon ./icon-transport.svg`.
They render in `export png` with no network, the same as the bundled sets.

| Element | Glyph | Why it is honest |
| --- | --- | --- |
| Core | stacked plinth | the foundation everything else rests on |
| Engine | globe | `UWorld` is literally the central type |
| Messaging System | envelope | typed messages, delivered |
| Transport | frames on a wire | "moves framed bytes" drawn |
| Application | loop with an arrowhead | it owns the `Run` frame loop |
| Networking | two lines merging into one | the only place Messaging and Transport meet |
| IP / Wired / Radio Drivers | network, cable, antenna | the physical medium, the one thing the three differ by |

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
what actually crosses, taken from the `#include` set, with the header cited in
`metadata { traceability }`.

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

Reason about what a change does to the render from the DSL instead: `title`, `technology`
and `description` are rendered, `metadata { }` never is. So a metadata-only edit cannot
change a diagram, and an edit to a subtitle or a description changes the most prominent
text on it.

If a render is ever wanted, `export png --flat` needs its output directory deleted first —
it leaves orphan PNGs for renamed views, and a stale image reads as confirmation.
