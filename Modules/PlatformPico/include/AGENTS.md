# PlatformPico Public Includes

Inherits `../AGENTS.md`.

## Architecture

This directory owns the installed include boundary for `PlatformPico`. Public
headers may depend inward on MicroWorld Net/Core but never expose Pico SDK or
FreeRTOS types.

## Concepts

- Configuration uses fixed-width integers and documented RP2040 identities.
- Public declarations remain C++17, exception-free, RTTI-free, and
  fixed-capacity.
