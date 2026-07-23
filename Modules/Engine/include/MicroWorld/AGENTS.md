# MicroWorld Engine Namespace

Inherits `../AGENTS.md`.

## Architecture

This directory extends the shared `MicroWorld` namespace only with Engine
contracts. It may use Core, Memory, and Object contracts but must not
duplicate them or introduce networking, platform, or product APIs.

## Concepts

All Engine symbols share the project namespace while retaining their separate
physical package and one-way dependency boundary.

## Verification

Verify public symbols and includes against the declared inward dependency
boundary before release.
