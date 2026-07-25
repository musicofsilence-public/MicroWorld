#pragma once

#include <MicroWorld/Net/FrameCodec.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformEsp32/UartAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Largest single-transmission payload one wired UART frame carries.
 *
 * Chosen so one whole frame (payload + `FrameOverheadBytes`) fits inside the RX ring buffer the
 * shared open helper installs (two hardware FIFOs, ~256 bytes) with room for a second frame
 * arriving while the first is pumped at the example's 10 ms pacing; at 115200 baud that is ~115
 * raw bytes per 10 ms, so 120 leaves honest headroom instead of a tight fit.
 */
constexpr std::size_t UartMaxPayloadBytes = 120;

/**
 * Construction parameters for one wired UART driver.
 *
 * Holds the UART port number and TX/RX GPIO numbers as plain integers so the public header stays free of
 * the ESP-IDF `uart_port_t`/`gpio_num_t` enum types; the platform-implementation header reinterprets them on the ESP32 side.
 */
struct FEsp32UartConfig
{
	/** UART port number (ESP-IDF `uart_port_t`, e.g. UART_NUM_1) passed as a plain integer. */
	std::int32_t UartPort{0};

	/** TX GPIO number wired to the peer board's RX pin, passed as a plain integer. */
	std::int32_t TxGpio{0};

	/** RX GPIO number wired to the peer board's TX pin, passed as a plain integer. */
	std::int32_t RxGpio{0};

	/** Baud rate shared with the peer board's UART configuration (a wire is fast, so 115200 by default). */
	std::uint32_t BaudRate{115200};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Non-blocking point-to-point wired `INetDriver` that frames traffic over one ESP-IDF UART.
 *
 * Functionally the E32 LoRa driver minus the radio: encodes each packet with the portable `FrameCodec`
 * (magic, source node id, big-endian length, payload, CRC-16/CCITT-FALSE) and writes the whole frame to
 * the UART; receives pump one byte at a time through a bounded `TFrameDecoder` that resyncs on bad magic,
 * oversize length, or CRC mismatch. It validates every argument before any syscall, leaves caller-owned
 * outputs unchanged on any non-`Success` result, and exercises no UART traffic until this driver's example
 * hardware checkpoint passes (§1.2).
 */
class FEsp32UartDriver final : public INetDriver
{
public:
	/**
	 * Opens and configures one UART for wired point-to-point traffic.
	 *
	 * Installs the ESP-IDF UART driver at `UartPort`, configures it for 8N1 at `BaudRate`, and routes it to the
	 * given TX/RX GPIOs. On any configuration failure the constructor uninstalls what it installed and leaves
	 * the driver with `IsOpen() == false`; it never throws. The local node id is stamped on every outgoing frame.
	 *
	 * @param InConfig UART, GPIO, baud, and local node id parameters.
	 */
	explicit FEsp32UartDriver(const FEsp32UartConfig& InConfig) noexcept;

	/** Uninstalls the UART driver opened by construction. */
	~FEsp32UartDriver() noexcept override;

	/** Prevents copying so one driver value owns exactly one UART identity. */
	FEsp32UartDriver(const FEsp32UartDriver&) = delete;

	/** Prevents copying so one driver value owns exactly one UART identity. */
	FEsp32UartDriver& operator=(const FEsp32UartDriver&) = delete;

	/** Prevents moving so the owned UART port and interface identity stay fixed. */
	FEsp32UartDriver(FEsp32UartDriver&&) = delete;

	/** Prevents moving so the owned UART port and interface identity stay fixed. */
	FEsp32UartDriver& operator=(FEsp32UartDriver&&) = delete;

	/**
	 * Sends one complete framed message over the UART, transactionally.
	 *
	 * Returns `Invalid` for a destination address that is not a UART encoding, an oversize packet, or a null span
	 * with nonzero length; `Full` when the UART write would block; and `Success` only when the whole frame was
	 * accepted. A non-success result leaves the UART state unchanged.
	 *
	 * @param InTo Destination whose single byte must be a UART node id (validated; the wire is point-to-point).
	 * @param InPacket Caller-owned payload bytes framed and sent as one message.
	 * @return Normalized outcome of the single send attempt.
	 */
	ENetResult TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Receives at most one framed message into the caller-owned destination, transactionally.
	 *
	 * Pumps available UART bytes through the decoder one byte at a time until a frame completes or the bounded pump
	 * drains; `Unavailable` when no frame is ready, `Full` when the held frame's payload exceeds the destination
	 * (the frame stays held for a larger retry), `Invalid` for a null destination with nonzero length, and `Success`
	 * after a complete frame copies its payload, the byte count, and the sender node id into `OutFrom`.
	 *
	 * @param OutFrom Filled with the sender's UART address only on `Success`.
	 * @param InDestination Caller-owned buffer for the received payload bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept override;

	/** Reports the largest payload, in bytes, one send accepts (excludes framing overhead). */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable UART. */
	bool IsOpen() const noexcept;

private:
	/** Copies the decoder's held frame into the destination and clears it, or returns
	 * `Full` (leaving the frame held) when the payload exceeds the destination. */
	ENetResult DeliverFrameToDestination(TSpan<std::uint8_t> InDestination, FNetAddress& OutFrom, FNetReceiveResult& OutResult) noexcept;

	/** Pumps the bounded UART byte budget through the decoder and delivers the first
	 * completed frame; returns `Unavailable` when the budget drains with no frame ready. */
	ENetResult PumpDecoderForFrame(TSpan<std::uint8_t> InDestination, FNetAddress& OutFrom, FNetReceiveResult& OutResult) noexcept;

	/** Reports the first reason an outgoing packet cannot be framed and sent, or `Success`. */
	ENetResult ValidateOutgoingPacket(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) const noexcept;

	/** Bounded RX deframer held by value; its capacity matches `UartMaxPayloadBytes`. */
	TFrameDecoder<UartMaxPayloadBytes> Decoder{};

	/** UART port number reinterpreted to its ESP-IDF type only in the source file. */
	std::int32_t UartPortNumber{0};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld
