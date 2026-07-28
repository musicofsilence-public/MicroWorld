# RadioE32 Public Headers

Inherits `../../../AGENTS.md`.

## Architecture

This directory will expose `FRadioE32Driver`, the supported portable E32
transport over `IUartByteStream` and `INetDriver`. It owns public driver
semantics while platform facades retain UART lifecycle and hardware ownership.

## Concepts and boundaries

- Public driver operations validate E32 address shape and packet bounds before
  they accept work into fixed storage.
- Send and receive outcomes remain non-blocking and transactional for caller
  outputs.
- `Detail/` implementation headers are excluded from the supported consumer
  contract and must not be included by applications or platform facades.

## Verification

Test observable behavior through the public driver contract using fixed-capacity
UART fakes. Compile public headers without platform SDKs, exceptions, or RTTI.
