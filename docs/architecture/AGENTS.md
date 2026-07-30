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

### Systems carry this repository's own names

`Core`, `Engine`, `Net`, `Messaging`, `Application`, `Net System` —
because here the module names **are** the domain vocabulary. An earlier revision
renamed them to invented abstractions ("Runtime Substrate", "Managed Runtime",
"Engine-Network Integration") and the result forced every reader to translate
before they could find anything. Do not reintroduce a private vocabulary.

The architectural content is the **boundary**, not the name, and the subtitle is
where it shows: `system: Modules/Engine + Modules/Object` on one box is the fact no
directory listing carries. Keep the module path in `technology`, not `metadata`,
because metadata does not render.

`Net System` is titled from `TNetSystem`, its own class — an `IEngineSystem` owning
net drivers and channels. "Integration" named where the code sits, not what it is.

### The build graph is not the authority

Boundaries are decided by responsibility. Package structure records where a
responsibility lives today; it never justifies where the line is drawn, and where
the two disagree the packaging is the defect to fix. No boundary in this model may
be defended with a `target_link_libraries` line.

File references stay, demoted to what they are. Three metadata keys, and they must
not be mixed: `contract` is what a system exposes, `boundary` is why the line is
drawn where it is, `traceability` is where an element or dependency lives today.
The first two are decisions; the third is replaceable the moment code moves.

### Boundary verdicts for this repository

Recorded because they are the decisions someone will otherwise re-litigate:

| Pair | Verdict | Reason |
| --- | --- | --- |
| `Object` + `Engine` | **one** system, titled Engine | Object owns identity, Engine owns the lifetime built on it; neither is a responsibility anything wants alone |
| `Net` + every transport | **one** system, titled Net | a named medium — Wi-Fi, a wire, a radio — realises `INetDriver` and declares nothing outward, so it is an implementation. `Modules/RadioE32` is portable and separately packaged and still not a system |
| `Net` + `Net System` | **two** systems | the invariant is *about a boundary* — Net must not learn that actors exist — and a separation cannot be stated with one element |
| `Messaging` | **own** system | it **runs standalone on Core** — `TMessageRouter` is an `IEngineSystem` naming neither actors nor transports, so Core plus Messaging is already a usable stack. Folding it into networking would claim a local message needs a transport |
| `Core` + `Engine` | **two** systems | Core is the shared vocabulary that lets independent systems interoperate. Merge it and Net → Core becomes Net → Engine, collapsing the graph |
| `Platform*` | **not C2** | the non-portable edge, and implementations besides; they appear inside the C3 driver families that name them |

### Depth

C1 is the product boundary and its purpose. C2 is six contract-defined systems.
C3 exists in exactly two of them, because these are where a contributor
reliably guesses wrong:

- **Engine** — the deferred spawn barrier and generation-checked handles, plus
  where the `Engine + Object` merge gets unpacked.
- **Net** — the `INetDriver` interface, and the transport families behind it.

Do not add C3 elsewhere for symmetry.

C3 drivers appear by **family**, not one element per class. An eighth box reading
"realises `INetDriver`" teaches nothing the third did not; the class names live in
the subtitle, where enumeration costs nothing. This is also what keeps the Net view
readable now that IP, wired and radio transports all live there.

### The invariant the model must keep visible

Engine and Net never reference each other; `Net System` is the only element that
touches both, and `EngineSystem.h` in Core is what lets Net extend a frame loop it
cannot see.

This is stated in the `c2Systems` description and in the `netSystem` element
description — **not** in a view of its own. A filtered view of C2 would be the same
diagram minus two boxes, and an absent edge is exactly as absent either way.

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

Nine `icon-*.svg` files sit beside the `.c4` files, referenced as `icon ./icon-net.svg`.
They render in `export png` with no network, the same as the bundled sets.

| Element | Glyph | Why it is honest |
| --- | --- | --- |
| Core | stacked plinth | the foundation everything else rests on |
| Engine | globe | `UWorld` is literally the central type |
| Messaging | envelope | typed messages, delivered |
| Net | frames on a wire | "moves framed bytes" drawn |
| Application | loop with an arrowhead | it owns the `Run` frame loop |
| Net System | two lines merging into one | the only place Engine and Net meet |
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

`takes Time, Timer, Lifecycle, EngineSystem`, not `depends on`. The default
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
Remove-Item build/likec4-architecture-preview -Recurse -Force; likec4 export png --flat -o build/likec4-architecture-preview docs/architecture
```

Delete first — `export` leaves orphan PNGs for views that were renamed or removed,
and a stale image reads as confirmation.

```powershell
python tools/CheckFolderAgents.py --root docs
```

Then open all four exported images — `index`, `c2Systems`, `c3Engine`, `c3Net`.
Four declared, four exported: a fifth means a view id stopped claiming `index` and
LikeC4 generated one. Validation proves the model resolves; only inspection proves
it still says what you meant.
