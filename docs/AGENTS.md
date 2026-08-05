# MicroWorld Documentation

Inherits `../AGENTS.md`.

## Architecture

Documentation is short and tied to current code.

## Concepts

- `RADIO_TRANSPORTS_ROADMAP.md` owns next work; git history owns what changed.
- Headers and behavior tests own implemented behavior.
- Current system boundaries are Core, Transport, Messaging, Networking, Engine,
  and Application. Transport owns byte I/O and byte frame codecs; Messaging owns
  message framing and reliable delivery; Networking owns peer sessions and routes.
- Benchmark result files own exact measurements and qualifications.
- ADRs preserve durable decisions; a later decision supersedes history rather
  than hiding it.
- `Style.md`, `Porting.md`, and `Performance.md` give practical guidance.
- `Style.md` owns the simplicity rules and the reference-file list every review
  cites.

Do not turn future ideas into API claims, roadmaps, or extra process documents.
Verify links, symbols, behavior statements, and measured values against their
authoritative source.
