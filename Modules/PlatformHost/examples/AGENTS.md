# Examples

Inherits `../AGENTS.md`.

## Architecture

Examples are small composition roots that depend only on the released
PlatformHost, Engine, and Net public APIs over real host sockets. They
demonstrate consumer ownership and adapter wiring without becoming reusable
production modules; MicroWorld never depends on them.

## Concepts

- Concrete hosts and drivers live in dependency-safe declaration order.
- A shared logical clock, not wall time, drives every step so the printed
  trace is deterministic and byte-identical across runs.
- Example wiring exposes real-socket adapter behavior without product,
  hardware, or tutorial policy.

## Documentation and verification

Document each example function and persistent trace/counter state with the
teaching reason it exists. Keep work bounded and free of sleeps beyond a
bounded readiness poll. Build and run the example target and compare its
trace against the documented transcript.
