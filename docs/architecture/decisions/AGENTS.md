# Architecture Decision Records

Inherits `../../AGENTS.md`. The sibling `../AGENTS.md` governs the LikeC4 model
files only; its element-kind and view rules do not apply to decision records.

## Architecture

ADRs record durable design decisions that would be costly to reverse. They are
not required for every implementation task. Keep accepted history readable; a
later decision may supersede it without rewriting measured evidence.

## Concepts

**An ADR records a decision, never an implementation** (owner, 2026-07-30, the same
ruling that removed every file reference from the model). No file path, header name,
vendor-SDK call, commit hash, or build-target name appears here as content or as
evidence. Naming a type as vocabulary is fine; citing one to prove a point is the
error, because it makes the repository the authority over a document whose whole job
is to govern it.

The tell is a section that would need editing when code moves but not when the
decision changes. ADR 0003 carried two appendices of bus APIs, buffer depths and pin
assignments; they said how to implement a decision that the record above states in a
sentence, and they are gone. Version history keeps them.

**Measurements survive this rule.** A size, a timing, or a failure mode observed on a
target is evidence *for* a design choice, not a description of code — see ADR 0002a,
where the contract mismatch it measured is the reason the decision went the way it
did. Record what was measured and under what conditions; do not link to where the
numbers are filed.
