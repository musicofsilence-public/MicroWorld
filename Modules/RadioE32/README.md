# MicroWorld RadioE32

MicroWorld RadioE32 is the optional portable E32 LoRa transport package. It
owns E32 packet validation, framing, bounded transmit progress, receive
pumping, and retained-frame delivery over Core's non-blocking
`IUartByteStream` contract.

Its dependency direction is `Core <- Net <- RadioE32`. RadioE32 depends only
on Core, Net, and the C++17 standard library; platform packages provide UART
configuration, lifecycle, buffering policy, and vendor SDK calls at the edge.
It includes no platform SDK, hardware configuration, device discovery, or
general-purpose UART abstraction.

Public consumers will include `MicroWorld/RadioE32/RadioE32Driver.h` and link
`MicroWorld::RadioE32`. Implementation details remain below
`MicroWorld/RadioE32/Detail/` and are not a supported consumer surface.

Direct callers advance queued frames with `AdvanceTransmit`; `TNetHost` already
does so after outbound FIFO progress. This narrow byte-transfer interface is not a universal
HAL and performs no UART configuration or hardware I/O.
