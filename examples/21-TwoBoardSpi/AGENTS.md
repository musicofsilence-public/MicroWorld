# 21-TwoBoardSpi

Inherits `../AGENTS.md`.

## Architecture

One composition root (`app_main`) runs one role over one static SPI driver and
drives a bare ping-pong directly over `TrySend`/`TryReceive` — no `TNetManager`,
no `TNetHost`, no world. The role is a compile-time constant from
`-DMICROWORLD_EXAMPLE_SPI_MASTER`: the master build owns an
`FEsp32SpiMasterDriver` and `RunMaster`, the slave build an
`FEsp32SpiSlaveDriver` and `RunSlave`, so each environment compiles only its role.

## Concepts

- Makes the master-clocked asymmetry observable: only the master drives the
  bus, so `RunMaster` paces the volley — it sends, then polls reads until the
  slave's reply arrives — while `RunSlave` is purely reactive. The volley loop is
  byte-for-byte example 20's, so this is the same ping-pong with only the driver
  construction and pin set changed.
- SPI is full-duplex and the slave's reply is pipelined by a transaction, so the
  master may poll a few reads before the reply lands; the codec's resync absorbs
  the idle bytes in between.
- The wire is point-to-point, so `MakeSpiAddress(PeerNodeId)` is validated but
  not routed (CS selects the device in hardware); the sender identity a receiver
  prints comes from the frame's own source-id byte via `SpiAddressNodeId`.
- Static drivers, never `app_main` stack locals (§2.2) — SPI DMA buffers must
  live in internal RAM. No WiFi and no netif init — an SPI bus opens no socket.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/21-TwoBoardSpi`
builds both role environments. Hardware checkpoint (§1.2, human-gated) flashes the
master to one board and the slave to the other and expects both monitors to show
the counter climbing with no stalls.
