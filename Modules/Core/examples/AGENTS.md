# Examples

Inherits `../AGENTS.md`.

## Architecture

Examples are small platform-neutral composition roots that depend only on the
released public API. They demonstrate consumer ownership and lifecycle
orchestration without becoming reusable production modules; MicroWorld never
depends on them.

## Concepts

- Concrete objects live in dependency-safe declaration order.
- Registration happens before lifecycle start and remains non-owning.
- Caller-provided timestamps keep output deterministic and testable.
- Example hooks expose ordering and independent ticks without product,
  hardware, or tutorial policy.

## Documentation and verification

Document each example function and persistent trace/counter state with the
teaching reason it exists. Keep work bounded and free of sleeps or platform
services. Build and run the example target and compare its trace with the
documented lifecycle order.
