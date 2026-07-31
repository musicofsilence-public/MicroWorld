# Driver → Device Rename

Applies ADR 0005 to the eleven classes that realise `IDevice`. `INetDriver` became
`IDevice` in the Net sweep; the implementations kept the retired word.

## The rule

The model classifies a medium device by its **medium**, not by its encoding. So the
encoding leaves the class name and stays in the doc comment: `Udp` → `Wifi`, and the
E32 module name drops from the platform facades whose prefix already makes them
unique. The portable class keeps `E32` because it *is* the E32 protocol — the facades
are this board's LoRa device, which is a different thing.

## Rename table — the single source of truth

Every unit below reads its rows from here. Do not derive a name any other way.

| Old class | New class | Old file stem | New file stem | Files touching it |
| --- | --- | --- | --- | --- |
| `FRadioE32Driver` | `FE32LoraDevice` | `Transport/Lora/RadioE32Driver` | `Transport/Lora/E32LoraDevice` | 11 |
| `FPacketDropDriver` | `FPacketDropDevice` | `Transport/PacketDropDriver` | `Transport/PacketDropDevice` | 11 |
| `FHostUdpDriver` | `FHostWifiDevice` | `Platform/Host/HostUdpDriver` | `Platform/Host/HostWifiDevice` | 9 |
| `FEsp32UdpDriver` | `FEsp32WifiDevice` | `Platform/Esp32/Esp32UdpDriver` | `Platform/Esp32/Esp32WifiDevice` | 24 |
| `FEsp32E32LoraDriver` | `FEsp32LoraDevice` | `Platform/Esp32/Esp32E32LoraDriver` | `Platform/Esp32/Esp32LoraDevice` | 15 |
| `FEsp32UartDriver` | `FEsp32UartDevice` | `Platform/Esp32/Esp32UartDriver` | `Platform/Esp32/Esp32UartDevice` | 21 |
| `FEsp32I2cMasterDriver` | `FEsp32I2cMasterDevice` | `Platform/Esp32/Esp32I2cDriver` | `Platform/Esp32/Esp32I2cDevice` | 7 |
| `FEsp32I2cSlaveDriver` | `FEsp32I2cSlaveDevice` | (same file) | (same file) | 7 |
| `FEsp32SpiMasterDriver` | `FEsp32SpiMasterDevice` | `Platform/Esp32/Esp32SpiDriver` | `Platform/Esp32/Esp32SpiDevice` | 7 |
| `FEsp32SpiSlaveDriver` | `FEsp32SpiSlaveDevice` | (same file) | (same file) | 7 |
| `FPicoE32LoraDriver` | `FPicoLoraDevice` | `Platform/Pico/PicoE32LoraDriver` | `Platform/Pico/PicoLoraDevice` | 12 |

`Esp32E32LoraDriver` is header-only; every other stem is an `.h` + `.cpp` pair.

Test files follow their subject:

| Old | New |
| --- | --- |
| `tests/Transport/RadioE32DriverTests.cpp` | `E32LoraDeviceTests.cpp` |
| `tests/Transport/PacketDropDriverTests.cpp` | `PacketDropDeviceTests.cpp` |
| `tests/Platform/Host/HostUdpDriverTests.cpp` | `HostWifiDeviceTests.cpp` |
| `tests/Platform/Pico/PicoE32LoraDriverTests.cpp` | `PicoLoraDeviceTests.cpp` |

## Not renamed, deliberately

- **Address types** — `FUdpAddress`, `FUartAddress`, `FI2cAddress`, `FSpiAddress`,
  `FLoraAddress` and `Wifi/UdpAddressCodec.h`. An address is the encoding-specific
  value, not the device. A UDP address genuinely is IPv4 plus port.
- **`Esp32WifiLink`** — association and connection, not an `IDevice`. It sits beside
  `FEsp32WifiDevice`, and the two names are meant to read as different things.
- **`IUartByteStream`** and the `*PlatformImplementation` headers — neither realises
  `IDevice`.

## Units

Each unit renames its classes **and every consumer of them** — headers, sources,
tests, examples, build source lists, library manifests — so the tree compiles at the
end of each one. No unit leaves a dangling reference for a later unit to fix.

| # | Unit | Verify with |
| --- | --- | --- |
| 1 | Transport portable: `FE32LoraDevice`, `FPacketDropDevice`, plus the `MICROWORLD_TRANSPORT_RADIO` option → `MICROWORLD_TRANSPORT_LORA` | `ctest` |
| 2 | Host Wi-Fi: `FHostWifiDevice` | `ctest` |
| 3 | ESP32 wireless: `FEsp32WifiDevice`, `FEsp32LoraDevice` | PlatformIO cross-compile |
| 4 | ESP32 wired: the five UART/I2C/SPI classes | PlatformIO cross-compile |
| 5 | Pico LoRa: `FPicoLoraDevice` | Pico consumer harness |
| 6 | Prose sweep: case-insensitive `driver` across comments, guides and examples | `ctest` + folder gate |

Unit 6 is the expensive one, and ADR 0005 says why: the classes are the cheap half.
It is a separate unit precisely so nobody declares victory after unit 5.

## Open

- `docs/RADIO_TRANSPORTS_ROADMAP.md` keeps its filename; only its content is swept.
  Renaming a roadmap mid-flight costs more than the stale word does.
