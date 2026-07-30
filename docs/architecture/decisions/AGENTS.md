# Architecture Decision Records

Inherits `../../AGENTS.md`. The sibling `../AGENTS.md` governs the LikeC4 model
files only; its element-kind and view rules do not apply to decision records.

## Architecture

ADRs record durable design decisions that would be costly to reverse. They are
not required for every implementation task. Keep accepted history readable; a
later decision may supersede it without rewriting measured evidence.

## Concepts

Headers and tests own implemented behavior, and benchmark records own
measurements. Verify dates, links, and claims against those sources.
