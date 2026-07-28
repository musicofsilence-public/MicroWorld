# MicroWorld RadioE32 Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-radio-e32` is the optional portable E32 LoRa transport package.
Its dependency direction is `Core <- Net <- RadioE32`: it may depend only on
Core, Net, and the C++17 standard library. Platform packages may depend inward
on RadioE32, but RadioE32 must not include platform headers, vendor SDKs, or
hardware policy.

The package owns E32 validation, framing, bounded transmit progress, receive
pumping, and retained-frame delivery over `IUartByteStream`. Platform adapters
own UART configuration, lifetime, buffering policy, pin/baud setup, and vendor
SDK calls. This package is an optional transport, not a universal HAL.

## Concepts and boundaries

- Public headers expose the supported `FRadioE32Driver` contract in the flat
  `MicroWorld` namespace.
- `Detail/` holds fixed transport state and other implementation mechanics;
  consumers must not depend on those headers.
- Every operation is non-blocking, bounded, fixed-capacity, and explicit about
  success, backpressure, capacity, and invalid data.
- Portable production and test code use no heap allocation, exceptions, RTTI,
  hidden clocks, threads, platform SDKs, or global mutable state.

## Verification

Configure and build this package independently under C++17 strict warnings with
exceptions and RTTI disabled, then run its host behavior tests. Keep platform
lifecycle and SDK verification in the platform packages.
