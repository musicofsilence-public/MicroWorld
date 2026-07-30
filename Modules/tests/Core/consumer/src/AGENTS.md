# Consumer Entry Points

Inherits `../AGENTS.md`.

## Architecture

`NativeMain.cpp` and `Esp32Main.cpp` are Core probes.
`MemoryNativeMain.cpp` and `MemoryEsp32Main.cpp` exercise Core's memory APIs
through one shared public-API probe. `ObjectNativeMain.cpp` and
`ObjectEsp32Main.cpp` exercise the Core+Object profile through fixed storage,
root, weak-reference, and collection APIs. `EngineNativeMain.cpp` and
`EngineEsp32Main.cpp` exercise the accepted Core+Object+Engine compile profile.
`NetNativeMain.cpp` and `NetEsp32Main.cpp` exercise the Core+Net compile
profile through byte writer/reader, loopback, and manager APIs.
`PlatformEsp32Main.cpp` proves the PlatformEsp32 network/engine composition
without optional radio dependencies; `PlatformEsp32RadioE32Main.cpp` separately
compile-links the released ESP32 RadioE32 facade without runtime UART I/O.
`Esp32BenchmarkMain.cpp` remains the only target runtime harness. The parent
project guarantees that only one entry point is linked per environment.

## Concepts and boundaries

- Every entry point asserts the exact MicroWorld 0.4.0 public version.
- Basic probes instantiate a public World type without reaching into private
  implementation. The native probe also calls one out-of-line tick primitive so
  linker maps contain positive Core archive evidence.
- Memory probes construct fixed-resource unique/shared ownership, bounded
  containers, and delegates; virtual resource destruction forces positive
  Memory archive evidence.
- Object probes retain one root through collection, then prove weak expiry
  after unrooted reclamation through public APIs.
- Engine probes cover representative registration, lifecycle, tick, and
  collection APIs without claiming target runtime behavior.
- Net probes cover representative byte writer/reader, loopback send/receive,
  manager queue/advance/receive, and all four `ENetResult` paths without
  claiming target runtime behavior.
- The benchmark owns ESP-IDF counters, serial output, heap/stack sampling, and
  fixed workloads so target dependencies remain outside MicroWorld.
- Benchmark validation compares cumulative tick counts after every trial to
  expose semantic drift.

## Documentation and verification

Document functions, workload constants, result fields, and accumulated state by
the evidence they produce. Keep probes minimal and benchmark work fixed and
bounded.

- Verify with `pio run -d Modules/tests/Core/consumer -e native`,
  `-e esp32-s3`, `-e esp32-s3-memory`, `-e esp32-s3-object`,
  `-e esp32-s3-engine`, `-e esp32-s3-net`, or `-e esp32-s3-benchmark`.
