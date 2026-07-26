# Application Sources

Inherits `../AGENTS.md`.

## Architecture

`src/Application.cpp` is the only out-of-line Application source. It holds the
`FApplication` lifecycle state machine: the public `BeginPlay`/`Advance`/
`EndPlay` guards and the private `final` forwarders that call `OnConfigure`
then `IEngine::BeginPlay`, `IEngine::Tick`, and `IEngine::EndPlay`.

## Concepts and boundaries

- The single translation unit performs no allocation, hides no clock or
  thread, and depends only on the `Application` public header (which pulls in
  the engine interface).
- Keeping the lifecycle methods out of line keeps the state machine in one
  place and gives the Application archive stable linker evidence.
