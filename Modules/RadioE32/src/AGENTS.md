# RadioE32 Sources

Inherits `../AGENTS.md`.

## Architecture

This directory will implement the portable E32 transport state and public
driver. Source files may depend only on RadioE32 public/detail headers and the
approved Core and Net dependency chain.

## Concepts and boundaries

- Sources make bounded byte progress only when callers invoke the driver.
- One fixed transmit frame and one retained decoded receive frame keep
  backpressure and transactional delivery explicit.
- Platform SDK calls, UART setup, and device ownership remain outside this
  package in platform adapters.

## Verification

Format sources with the repository policy, compile under C++17 strict warnings
with exceptions and RTTI disabled, and verify behavior through host tests.
