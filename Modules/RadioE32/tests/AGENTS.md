# RadioE32 Tests

Inherits `../AGENTS.md`.

## Architecture

This directory will hold host tests for shared observable E32 protocol and
queue behavior. Platform packages retain tests for UART configuration,
lifecycle, and vendor-SDK adapter behavior.

## Concepts and boundaries

- Tests exercise `FRadioE32Driver` through fixed-capacity `IUartByteStream`
  fakes instead of inspecting private transport state.
- Tests cover bounded progress, backpressure retention, error recovery,
  transactional receive outputs, and framing resynchronization.
- Tests use no hardware SDK, wall clock, sleep, heap allocation, or static
  mutable state.

## Verification

Run the standalone RadioE32 CTest suite once its required production and test
sources are present. Keep platform lifecycle coverage in the owning platform
package.
