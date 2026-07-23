# 20-TwoBoardI2c

Inherits `../AGENTS.md`.

## Architecture

One composition root (`app_main`) runs one role over one static I2C driver and
drives a bare ping-pong directly over `TrySend`/`TryReceive` — no `TNetManager`,
no `TNetHost`, no world. The role is a compile-time constant from
`-DMICROWORLD_EXAMPLE_I2C_MASTER`: the master build owns an
`FEsp32I2cMasterDriver` and `RunMaster`, the slave build an
`FEsp32I2cSlaveDriver` and `RunSlave`, so each environment compiles only its role.

## Concepts

- Makes the master-clocked asymmetry observable: only the master drives the
  bus, so `RunMaster` paces the volley — it sends, then polls reads until the
  slave's staged reply arrives — while `RunSlave` is purely reactive.
- The volley loop above the driver is otherwise example 18's, so this is the
  same ping-pong with only the driver construction and the who-clocks-the-bus
  pacing changed.
- The wire is point-to-point, so `MakeI2cAddress(PeerNodeId)` is validated but
  not routed; the sender identity a receiver prints comes from the frame's own
  source-id byte via `I2cAddressNodeId`. The 7-bit bus address is a config field.
- Static driver and RX buffer, never `app_main` stack locals (§2.2). No WiFi and
  no netif init — an I2C bus opens no socket.

## Verification

Build Verify (`docs/EXAMPLES_ROADMAP.md` §1.1): `pio run -d examples/20-TwoBoardI2c`
builds both role environments. Hardware checkpoint (§1.2, human-gated) needs two
external ~4.7 kΩ pull-ups; it flashes the master to one board and the slave to
the other and expects both monitors to show the counter climbing with no stalls.
