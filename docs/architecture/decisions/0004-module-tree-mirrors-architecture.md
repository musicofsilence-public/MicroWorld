# ADR 0004: The Folder Tree Is Downstream Of The Model

- **Status:** Accepted
- **Date:** 2026-07-30
- **Decision owner:** Project owner

## Context

The repository held eleven build packages while the model defined six systems. The
gap was deliberate and documented: a system is a responsibility, a package is a
build target, and readers were told not to reconcile the two counts.

That instruction asked the wrong thing of a reader. **The folder tree is the first
architecture document anyone opens**, and it disagreed with the model in four
places — two systems were split across two folders each, one folder carried a name
the model did not use, and the platform edges appeared in the model only as media
inside Transport. None of that was visible without opening the model and reading
metadata that no longer exists.

Two smaller frictions compounded it. A public header sat six levels deep and named
its own package twice. And there were two competing mechanisms for "private
header", which means neither was the rule.

## Decision

- **One folder per system.** The tree states the systems directly, so identity and
  lifetime live together, and one byte-I/O system owns the device shape with every
  medium behind it.
- **Sources sit beside their headers**, with one convention for private headers
  rather than two.
- **The include prefix names the system, never the package.** One product-level
  namespace stays, because this library compiles into firmware alongside a vendor
  SDK where a bare foundation header name would collide.
- **Optionality is a build option, never a directory.** A medium is omitted from a
  build by asking for it to be, not by living somewhere separate.
- **The dependency rule is machine-checked and keeps its authority.** It reasons
  over system directories and fails the test suite on any violation.

The boundary in one sentence a student can quote: **the folder tree is the C2 view,
the include prefix is the system, and a build option — never a directory — is what
makes a medium optional.**

## Consequences

- The tree reads as the architecture with no model file open, which is the whole
  point.
- **Standalone per-system builds are gone.** Building one system in isolation has
  no equivalent; the superbuild is the only path. This was a real capability for
  exercising deep dependency chains, and it is the largest thing this decision
  costs.
- **A handful of private headers lose compiler-enforced privacy.** They are now
  reachable by a determined consumer where before they were not. Convention
  replaces enforcement for those files.
- **Every build target is renamed to its system**, with the old names removed
  rather than kept as second spellings. Target, folder, include prefix, and model
  element now share one name each.
- Every include path a consumer writes has moved, which is a breaking change and
  is versioned as one.

## Alternatives considered

- **Nest package folders inside system folders.** Rejected: it keeps all eleven
  packages and buys the correspondence at the cost of a deeper path that names its
  system twice. It also leaves the merges visible only in metadata, which was the
  original complaint.
- **Rename folders only, keep eleven packages.** Rejected: renaming removes two
  name lies cheaply, but two systems the model says are single would still be two
  folders each.
- **Drop the product-level include prefix to shorten paths.** Rejected: it is what
  prevents collisions with other libraries in a firmware build. The duplication
  worth removing was the package level, not the namespace.
- **Change the model to eleven systems instead.** Rejected, and this is the
  important one: it inverts the dependency between document and tree. A system is a
  responsibility, and splitting one in the model to match a build target would
  assert a boundary nothing wants.

## Revisit triggers

- A consumer needs to link one system without compiling the others — that is the
  standalone-build capability this decision spent.
- A platform edge needs to ship as an independently versioned package.
- The model gains or loses a system. The tree is downstream of the model, so that
  edit becomes a folder move by definition.
