# MicroWorld Memory Namespace

Inherits `../AGENTS.md`.

## Architecture

This directory extends the shared `MicroWorld` namespace only with APIs owned
by the adjacent Memory package. It must not duplicate Core headers or introduce
unrelated package or platform APIs.

## Concepts

Names use the established `F`/`T`/`E` conventions and expose explicit typed
results. Public types remain portable C++17 values or narrow virtual
boundaries, with no vendor or product-policy dependency.

## Verification

Check includes and symbols against the package boundary before release, and
compile consumers using only this package's include root plus its declared Core
dependency.
