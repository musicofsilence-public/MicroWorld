# Wi-Fi UDP Address Encoding

Inherits `../AGENTS.md`.

## Architecture

This directory holds one header and no driver. `UdpAddressCodec.h` defines the
6-byte UDP address encoding — four IPv4 octets plus a port — that every UDP
adapter reads and writes.

The drivers themselves are not portable and never will be: `Platform/Host`
reaches for OS sockets and `Platform/Esp32` for lwIP. Neither may depend on the
other, so the one thing they must agree on — the layout of the bytes inside an
`FDeviceAddress` — has to live somewhere both already depend on, and portable
Transport is that place. Defining it once here replaced a hand-copied encoding
per package; `Platform/Host/UdpAddress.h` and `Platform/Esp32/UdpAddress.h` keep
their historical include paths and forward to this header.

Nothing here is conditional. The codec is header-only, so it contributes no
source file to the Transport target and no CMake option guards it.

## Concepts and boundaries

- Every function is `constexpr` arithmetic over `FDeviceAddress` with no OS,
  socket, or lwIP include. That absence is the boundary this directory exists to
  hold, and the dependency-boundary checker rejects any include that is neither
  a MicroWorld header nor a whitelisted C++17 standard header.
- Bytes 0-3 carry the IPv4 octets in dotted order and bytes 4-5 the port with
  its high byte first, over an active `Size` of 6. `MakeUdpAddress` and
  `UdpAddressPort` are exact inverses across those two port bytes and share
  Core's byte-codec shift and mask constants, so edit them as one pair.
- `IsUdpAddress` tests the active length only, so a one-byte LoRa or loopback
  address is never mistaken for a UDP one; the byte contents are validated later,
  when a driver actually routes them.
- Byte-order conversion stays with the caller. A driver applies `ntohl`/`ntohs`
  to a received datagram and passes host-order values to
  `MakeUdpAddressFromPackedHostOrder`; nothing here calls a network-order helper,
  because that would need a platform header.
- Nothing here opens a socket, sends, receives, blocks, retries, or maps an
  error code. Those belong to the platform edges that own the sockets.

## Verification

A header-only directory has nothing of its own to build, so the proof is that
its consumers still compile: build both UDP platform adapters after any change.
Behaviour is covered indirectly by the host UDP driver and end-to-end tests
under `Modules/tests/Platform/Host`, which encode an address, route a real
datagram, and decode the sender back. Treat any layout change as a wire-format
change for every UDP node — both adapters must ship together.
