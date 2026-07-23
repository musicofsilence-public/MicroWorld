# MicroWorld PlatformEsp32 Namespace

Inherits `../AGENTS.md`.

## Architecture

This directory extends the shared `MicroWorld` namespace only with the ESP32
platform adapter's public declarations. It may use Core, Memory, Object, and
Net contracts but must not duplicate them or introduce Engine-layer or
product APIs.

## Concepts

All PlatformEsp32 symbols share the project namespace while retaining their
separate physical package and non-portable, excluded-from-boundary-checker
status.

## Verification

Verify public symbols and includes against the declared inward dependency
direction (never reversed onto portable packages) before release.
