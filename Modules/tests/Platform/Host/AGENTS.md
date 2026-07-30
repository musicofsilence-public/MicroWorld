# Platform/Host Host Behavior Tests

Inherits `../../../AGENTS.md`.

## Architecture

Real-socket host tests for `FHostUdpDriver` and `FHostTimeSource`: round-trip
send/receive, bind/close, WinSock reference-counting, and time-source
monotonicity.

## Concepts

Tests use real localhost UDP sockets and `steady_clock`; no fakes.

## Verification

Run the `microworld_platform_host_tests` target via CTest.
