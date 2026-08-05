# ADR 0011: File-size Ceiling and Out-of-class Partitions

Status: Accepted

Date: 2026-08-02

Decision owner: Project owner

## Context

The codebase had accumulated several translation units and headers well above a
thousand lines. Each oversized file mixed several responsibilities (lifecycle,
registration, barrier mechanics, codec primitives, retry policy, control-message
routing, test fixtures), and a reader had to scroll past unrelated groups to reach
the one in question. The largest also doubled as the only home for shared fixtures
and private machinery, so any split had to preserve visibility for the cases that
need it.

The owner set a hard readability ceiling and asked for a split that keeps each
responsibility group in its own file, without growing the directory tree and without
forcing every body back into the class definition.

## Decision

- **Every tracked C++ source file stays under a measured 700 lines.** Files between
  501 and 700 lines are a yellow caution zone; files over 700 lines are a red zone
  that must be partitioned. The ceiling is measured in source lines and treated as a
  readability budget, the same way a fixed-capacity container is treated as a memory
  budget.
- **Partitioned files are flat and co-located, never nested in a new subfolder.** A
  responsibility group joins its owner in the directory the owner already lives in.
  The directory tree mirrors the architecture, not the file-size history.
- **Partitioned file names take the owner's name as a prefix with an underscore
  separator, followed by the group name.** One owner may own several partitions; the
  prefix makes the family readable in a directory listing and distinguishes it from
  unrelated neighbors.
- **Function bodies move out of the class by default.** The class body keeps the
  declarations and the API contract; each definition is written out-of-class as
  `Owner::Method`. The declaration list stays compact and scannable while the bodies
  are grouped elsewhere by responsibility. This applies whether the owner is a
  concrete class or a class template.
- **Class templates use `.inl` partitions for out-of-class definitions, not `.cpp`
  translation units.** A template's definitions must remain visible at every
  instantiation, and the traits and size arguments it instantiates against live in
  the consuming code (tests and examples), not in the library. A `.cpp` would force
  enumerating every instantiation explicitly and would require the library to see the
  consuming code's type arguments — the wrong dependency direction. The `.inl`
  partitions are included once, after the class body closes, and are never included
  directly.
- **The `.inl` suffix is a gated source class, not an exemption.** The formatting and
  documentation-style gates scan `.inl` alongside `.h` and `.cpp`, so every partition
  carries the same Motivation/Responsibilities contract and the same clang-format
  policy as the public header.
- **Test files split the same way, plus shared helpers.** An oversized test file
  becomes one `.cpp` per theme, and fixtures shared across themes move to a helper
  header whose types live in a dedicated test namespace. Test bodies are relocated
  verbatim — the split is structural, not a rewrite.
- **This is a mechanical reorganization.** No public symbol, signature, namespace,
  ABI, or behavior changes. The only tooling change is the gates learning the `.inl`
  suffix.

## Consequences

- Each responsibility group is reachable by its own filename; a reader opens the one
  group they need instead of scrolling a thousand-line file.
- The class body becomes an index of declarations; the bodies are a follow-up read
  for the reader who needs a specific implementation.
- File count rose, and the directory it grew in is denser — the accepted price for
  per-group reachability, paid in exchange for not deepening the tree.
- Template `.inl` partitions add a fourth looked-for location (after `.h`, `.cpp`,
  and tests); the owner-name prefix and the after-class-body include keep the family
  discoverable despite that.
- The gates now batch their external formatter invocations on hosts whose command
  line length is bounded, so a growing file count never turns the format gate into a
  false failure.
- The red zone is empty as of this decision; the yellow zone is tracked but not
  mandated for action.

## Alternatives considered

- **Keep the bundle convention and accept oversized files.** Rejected: a
  thousand-line file forces every reader to reconstruct the responsibility boundaries
  on each visit, and the boundaries drift as the file grows.
- **Move template bodies into `.cpp` translation units with explicit instantiation.**
  Rejected: explicit instantiation can only name the type arguments the library sees.
  The consuming code instantiates against its own traits and size arguments, so a
  library `.cpp` would either have to reach into consuming code (wrong dependency
  direction) or fail to link every instantiation the consumers need.
- **Keep template bodies inline in the class body.** Rejected: it preserves the
  thousand-line header the ceiling is meant to remove, and the inline-via-include
  trick that splits a body across the class interior hides the API behind
  interleaved implementation.
- **Nest partitioned files under per-owner subfolders.** Rejected: the directory tree
  mirrors the architecture, and one owner's partitions are not a subsystem. A flat,
  prefix-named family keeps the architecture readable in the tree.

## Revisit triggers

- A measured build-time or compile-throughput regression shows the `.inl` include
  fan-out materially hurts a target build budget.
- A consuming workflow emerges that genuinely needs every body for one owner in a
  single file, and the decision is revisited for that owner rather than abandoning
  the ceiling.
- A future template is instantiated against a closed, library-owned set of type
  arguments, at which point a `.cpp` with explicit instantiation becomes viable for
  that owner and this decision is revisited locally.
