#pragma once

#include <MicroWorld/Net/FrameCodec.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformEsp32/LoraAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/** Largest single-transmission payload the configured E32 module is assumed to accept; tunable per module. */
constexpr std::size_t E32MaxPayloadBytes = 58;

/**
 * Construction parameters for one E32 LoRa UART driver.
 *
 * Holds the UART port number and TX/RX GPIO numbers as plain integers so the public header stays free of
 * the ESP-IDF `uart_port_t`/`gpio_num_t` enum types; the glue header reinterprets them on the ESP32 side.
 */
struct FEsp32E32LoraConfig
{
	/** UART port number (ESP-IDF `uart_port_t`, e.g. UART_NUM_1) passed as a plain integer. */
	std::int32_t UartPort{0};

	/** TX GPIO number wired to the E32 module's RX pin, passed as a plain integer. */
	std::int32_t TxGpio{0};

	/** RX GPIO number wired to the E32 module's TX pin, passed as a plain integer. */
	std::int32_t RxGpio{0};

	/** Baud rate shared with the E32 module's UART configuration (commonly 9600). */
	std::uint32_t BaudRate{9600};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Non-blocking E32 LoRa `INetDriver` that frames traffic over one ESP-IDF UART.
 *
 * Encodes each packet with the portable `FrameCodec` (magic, source node id, big-endian length, payload,
 * CRC-16/CCITT-FALSE) and writes the whole frame to the UART; receives pump one byte at a time through a
 * bounded `TFrameDecoder` that resyncs on bad magic, oversize length, or CRC mismatch. It validates every
 * argument before any syscall and leaves caller-owned outputs unchanged on any non-`Success` result, and no
 * radio traffic is exercised in the compile-only phase.
 */
class FEsp32E32LoraDriver final : public INetDriver
{
public:
	/**
	 * Opens and configures one UART for E32 LoRa traffic.
	 *
	 * Installs the ESP-IDF UART driver at `UartPort`, configures it for 8N1 at `BaudRate`, and routes it to the
	 * given TX/RX GPIOs. On any configuration failure the constructor uninstalls what it installed and leaves
	 * the driver with `IsOpen() == false`; it never throws. The local node id is stamped on every outgoing frame.
	 *
	 * @param Config UART, GPIO, baud, and local node id parameters.
	 */
	explicit FEsp32E32LoraDriver(const FEsp32E32LoraConfig& Config) noexcept;

	/** Uninstalls the UART driver opened by construction. */
	~FEsp32E32LoraDriver() noexcept override;

	/** Prevents copying so one driver value owns exactly one UART identity. */
	FEsp32E32LoraDriver(const FEsp32E32LoraDriver&) = delete;

	/** Prevents copying so one driver value owns exactly one UART identity. */
	FEsp32E32LoraDriver& operator=(const FEsp32E32LoraDriver&) = delete;

	/** Prevents moving so the owned UART port and interface identity stay fixed. */
	FEsp32E32LoraDriver(FEsp32E32LoraDriver&&) = delete;

	/** Prevents moving so the owned UART port and interface identity stay fixed. */
	FEsp32E32LoraDriver& operator=(FEsp32E32LoraDriver&&) = delete;

	/**
	 * Sends one complete framed message over the UART, transactionally.
	 *
	 * Returns `Invalid` for a destination address that is not a LoRa encoding, an oversize packet, or a null span
	 * with nonzero length; `Full` when the UART write would block; and `Success` only when the whole frame was
	 * accepted. A non-success result leaves the UART state unchanged.
	 *
	 * @param To Destination whose single byte must be a LoRa node id (validated but broadcast on the wire).
	 * @param Packet Caller-owned payload bytes framed and sent as one message.
	 * @return Normalized outcome of the single send attempt.
	 */
	ENetResult TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept override;

	/**
	 * Receives at most one framed message into the caller-owned destination, transactionally.
	 *
	 * Pumps available UART bytes through the decoder one byte at a time until a frame completes or the bounded pump
	 * drains; `Unavailable` when no frame is ready, `Full` when the held frame's payload exceeds the destination
	 * (the frame stays held for a larger retry), `Invalid` for a null destination with nonzero length, and `Success`
	 * after a complete frame copies its payload, the byte count, and the sender node id into `OutFrom`.
	 *
	 * @param OutFrom Filled with the sender's LoRa address only on `Success`.
	 * @param Destination Caller-owned buffer for the received payload bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept override;

	/** Reports the largest payload, in bytes, one send accepts (excludes framing overhead). */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable UART. */
	bool IsOpen() const noexcept;

private:
	/** Bounded RX deframer held by value; its capacity matches `E32MaxPayloadBytes`. */
	TFrameDecoder<E32MaxPayloadBytes> Decoder{};

	/** UART port number reinterpreted to its ESP-IDF type only in the source file. */
	std::int32_t UartPortNumber{0};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld
