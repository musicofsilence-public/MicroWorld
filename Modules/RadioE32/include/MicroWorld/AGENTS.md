# RadioE32 Namespace Root

Inherits `../../AGENTS.md`.

## Architecture

The `MicroWorld/` namespace root holds the `RadioE32/` include directory for
the optional portable E32 transport. It joins the package's symbols to the flat
`MicroWorld` namespace without introducing a nested package namespace.

## Concepts and boundaries

- RadioE32 headers may include only public Core and Net headers plus C++17
  standard-library facilities.
- Platform adapters implement Core's `IUartByteStream` interface outside this package.
- No vendor SDK or hardware configuration header may enter this namespace root.

## Verification

Compile package headers as a portable Core-and-Net consumer with C++17 strict
warnings, exceptions disabled, and RTTI disabled.
