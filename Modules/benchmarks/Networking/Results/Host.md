# Networking Host Evidence

**Status:** Host build and behavioral-test evidence only.

## Source qualification

- **Recorded:** 2026-08-05T18:29:12+03:00.
- **Base SHA:** `58e065a8f76180a6c5d05f92b09271c2003dbc33`.
- **Worktree:** dirty at capture time (`git status --short` reported 109
  paths). This is an integration snapshot, not clean-revision or release
  evidence.

## Environment and commands

- Windows host; CMake 4.0.2; MSBuild 17.14.40+3e7442088.

```powershell
cmake --version
cmake --build build --config Release --target microworld_networking_tests
ctest --test-dir build -C Release -R '^microworld_networking_tests$' --output-on-failure
```

## Observed host result

The `microworld_networking_tests` target built successfully. Its one selected
CTest test passed: 1/1 in 0.11 s (0.12 s total real time).

## Resource boundary evidenced by source and tests

- A server stores at most four peers (`FNetworkSystem::MaxPeers`); a client
  stores one server session.

- A routed application envelope owns at most 74 payload bytes
  (`FRoutedMessage::MaxPayloadBytes`).
- The host tests cover bounded admission and bounded application routing over
  Messaging loopback.

These are source-level capacity contracts, not measured RAM, stack, allocation,
latency, throughput, target compile, or target runtime margins. No such claim
is established by this record.
