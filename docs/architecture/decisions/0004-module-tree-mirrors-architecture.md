# ADR 0004: The Module Tree Mirrors the Architecture Model

- **Status:** Accepted
- **Date:** 2026-07-30
- **Decision owner:** Project owner

## Context

`Modules/` held eleven build packages while `model.c4` defined six systems. The
gap was deliberate and documented: a system is a responsibility, a package is a
build target, and `CLAUDE.md` instructed readers not to reconcile the two counts.

That instruction asked the wrong thing of a reader. The folder tree is the first
architecture document anyone opens, and it disagreed with the model in four
places: `Object` and `Engine` were one system, `Net` and `RadioE32` were one
system, `Integration` was named `Networking` in the model alone, and the three
`Platform*` packages appeared in the model only as driver subsystems of
Transport. Learning any of that required reading `traceability` metadata inside
`.c4` files.

Two smaller frictions compounded it. The `include/` + `src/` split put a header
six levels deep — `Modules/Net/include/MicroWorld/Net/NetHost.h` — and named its
package twice. And the repository carried two mechanisms for "private header":
unreachable `src/*.h` files (six of them, all under the platform edges) and a
`Detail/` convention inside `include/` (five). Either is fine; having both means
neither is the rule.

`AGENTS.md` froze the `include/MicroWorld/...` layout and the CMake target names
against exactly this kind of change, so the freeze had to be lifted deliberately
rather than eroded.

## Decision

- **One folder per system.** `Modules/MicroWorld/` contains `Core`, `Engine`,
  `Messaging`, `Transport`, `Networking`, `Application`, and `Platform`. `Object`
  folds into `Engine` because identity and lifetime are not responsibilities
  anything wants separately; `Net` and `RadioE32` fold into `Transport` because
  one byte-I/O system owns the driver contract and every medium behind it.
- **Sources sit beside their headers.** `include/` and `src/` are gone. Private
  headers live in a `Detail/` subfolder, which becomes the single convention.
- **The include prefix names the system, never the package.** `MicroWorld/Core/`,
  `MicroWorld/Engine/`, `MicroWorld/Transport/`, and so on — six prefixes for six
  systems. The `MicroWorld/` namespace level stays: this library is compiled into
  firmware alongside ESP-IDF, where a bare `Core/Time.h` would collide.
- **Optionality is a build option, not a folder.** The radio and IP transports
  were separable because `RadioE32` was its own package; they stay separable
  through `MICROWORLD_TRANSPORT_RADIO` and `MICROWORLD_TRANSPORT_IP`, so an
  RP2040 build still omits IP and protocol code.
- **The dependency gate moves with the tree and keeps its authority.**
  `tools/CheckDependencyBoundaries.py` now reasons over system directories, and
  still fails `ctest` on any violation.

The boundary in one sentence a student can quote: **the folder tree is the C2
view, the include prefix is the system, and a build option — never a directory —
is what makes a transport optional.**

## Consequences

- `Modules/MicroWorld/` reads as the architecture with no `.c4` file open. A
  header is three levels deep instead of six, and no path names its system twice.
- 667 of 866 include directives were rewritten; the remaining 199 were already
  correct. No C++ logic changed — the only content edits inside functions and
  classes were two `#pragma once` line shifts.
- **Standalone per-module CMake builds are gone.** `cmake -S Modules/Engine -B
  build-engine` has no equivalent; the superbuild is the only path. This was a
  real capability for exercising deep sibling chains, and it is the largest thing
  this decision costs.
- **Six private headers lose compiler-enforced privacy.** In `Detail/` they are
  reachable by a determined consumer where an unreachable `src/*.h` was not.
  Convention replaces enforcement for those six files.
- **Every build target is renamed to its system.** `microworld_net` becomes
  `microworld_transport` and `microworld_integration` becomes
  `microworld_networking`, with `MicroWorld::Transport` and
  `MicroWorld::Networking` as the only aliases — the `MicroWorld::Net` and
  `MicroWorld::Integration` names are gone rather than kept as second spellings.
  Target, folder, include prefix, and model element now share one name each.
- **The version becomes 0.4.0.** Every include path a consumer writes has moved,
  which is a breaking change; the minor bump is what tells a consumer that
  `MicroWorld/Net/NetHost.h` will not be found. `FVersion` in
  `MicroWorld/Core/Version.h` and every `library.json` carry it.
- One `library.json` covers the portable systems; the three platform edges keep
  their own so a PlatformIO example can still consume one board's package. Each
  example's `lib_deps` collapses from five to seven `symlink://` entries to one.
- `AGENTS.md`'s frozen-identity section is rewritten, and `CLAUDE.md` no longer
  claims the counts are irreconcilable, because they are now reconciled.

## Alternatives considered

- **Nest package folders inside system folders** (`Transport/Net/include/...`).
  Rejected: it keeps all eleven packages and buys the C2 correspondence at the
  cost of a seven-level path that names its system twice. It also leaves the two
  merges visible only in metadata, which was the original complaint.
- **Rename folders only, keep eleven packages.** Rejected: renaming
  `Integration` to `Networking` and `Net` to `Transport` removes two name lies
  cheaply, but `Object` and `RadioE32` remain separate folders for systems that
  the model says do not exist separately.
- **Drop the `MicroWorld/` include prefix to shorten paths.** Rejected: it is
  the namespace that keeps `Core/Time.h`, `Memory/`, and `IO/` from colliding
  with other libraries in a firmware build. The duplication worth removing was
  the package level, not the namespace.
- **Change the model to eleven systems instead.** Rejected: it inverts the
  dependency between document and tree. A system is a responsibility; splitting
  Engine from Object in the model to match a build target would assert a boundary
  that no code or consumer wants.

## Revisit triggers

- A consumer needs to link one system without compiling the others — that is the
  standalone-build capability this decision spent, and it would argue for
  restoring per-system packages.
- A platform edge needs to ship as an independently versioned package; the three
  platform `library.json` files are the seam where that would start.
- The model gains or loses a system. The folder tree is now downstream of
  `model.c4`, so that edit becomes a folder move by definition.
