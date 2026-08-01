#pragma once

#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/UartAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Sizes one wired UART frame payload so a whole frame (payload plus framing overhead) fits the RX ring buffer with headroom for a second
 * frame arriving between pumps. */
constexpr std::size_t UartMaxPayloadBytes = 120;

/**
 * Motivation: Carries the plain-integer UART parameters one wired device needs at construction so the public header
 *   stays free of the ESP-IDF uart_port_t/gpio_num_t enum types.
 * Responsibilities: Hold UART port, TX/RX GPIO, baud rate, and local node id as plain integers.
 * Example:
 *   FEsp32UartConfig Config;
 *   Config.BaudRate = 115200;
 */
struct FEsp32UartConfig
{
	/** Motivation: UART port number (ESP-IDF uart_port_t, e.g. UART_NUM_1) passed as a plain integer. */
	std::int32_t UartPort{0};

	/** Motivation: TX GPIO number wired to the peer board's RX pin, passed as a plain integer. */
	std::int32_t TxGpio{0};

	/** Motivation: RX GPIO number wired to the peer board's TX pin, passed as a plain integer. */
	std::int32_t RxGpio{0};

	/** Motivation: Baud rate shared with the peer board's UART configuration (a wire is fast, so 115200 by default). */
	std::uint32_t BaudRate{115200};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Motivation: Gives the application entry point a non-blocking point-to-point Core::ITransportDevice over one ESP-IDF UART,
 *   the E32 LoRa device minus the radio.
 * Responsibilities: Encode each packet with the portable FrameCodec, write the whole frame to the UART, and pump
 *   received bytes one at a time through a bounded TFrameDecoder that resyncs on bad magic, oversize length, or CRC
 *   mismatch; validate every argument before any syscall and leave caller-owned outputs unchanged on any non-Success
 *   result.
 * Example:
 *   FEsp32UartDevice Uart(Config);
 *   if (Uart.IsOpen()) { Uart.TrySend(To, Packet); }
 */
class FEsp32UartDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Opens and configures one UART for wired point-to-point traffic before any frame flows.
	 * Responsibilities: Install the ESP-IDF UART driver at UartPort, configure it for 8N1 at BaudRate, and route it
	 *   to the given TX/RX GPIOs; on any failure uninstall what was installed and leave IsOpen false; never throw.
	 */
	explicit FEsp32UartDevice(const FEsp32UartConfig& InConfig) noexcept;

	/**
	 * Motivation: Releases the UART driver so construction-installed ESP-IDF resources never leak.
	 * Responsibilities: Uninstall the UART driver opened by construction.
	 */
	~FEsp32UartDevice() noexcept override;

	/**
	 * Motivation: Keeps one device value owning exactly one UART identity so the port handle never aliases.
	 * Responsibilities: Reject copy construction so the device stays the single owner of its UART.
	 */
	FEsp32UartDevice(const FEsp32UartDevice&) = delete;

	/**
	 * Motivation: Keeps one device value owning exactly one UART identity so the port handle never aliases.
	 * Responsibilities: Reject copy assignment so the device stays the single owner of its UART.
	 */
	FEsp32UartDevice& operator=(const FEsp32UartDevice&) = delete;

	/**
	 * Motivation: Keeps the owned UART port and interface identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move construction so the opaque port number never relocates.
	 */
	FEsp32UartDevice(FEsp32UartDevice&&) = delete;

	/**
	 * Motivation: Keeps the owned UART port and interface identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move assignment so the opaque port number never relocates.
	 */
	FEsp32UartDevice& operator=(FEsp32UartDevice&&) = delete;

	/**
	 * Motivation: Sends one complete framed message over the UART, transactionally.
	 * Responsibilities: Return Invalid for a non-UART destination, oversize packet, or null span with nonzero
	 *   length, Full when the UART write would block, and Success only after the whole frame is accepted; leave
	 *   UART state unchanged on any non-success result.
	 */
	Transport::ETransportResult TrySend(const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Receives at most one framed message into the caller-owned destination, transactionally.
	 * Responsibilities: Pump UART bytes through the decoder one byte at a time until a frame completes or the
	 *   bounded pump drains; report Unavailable, Full (frame held for a larger retry), Invalid (null destination
	 *   with nonzero length), or Success after a complete frame copies payload, byte count, and sender node id
	 *   into OutFrom; leave outputs unchanged on any non-success result.
	 */
	Transport::ETransportResult TryReceive(
		Transport::Address::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size a packet against the transport's capacity without a magic number.
	 * Responsibilities: Report the largest payload, in bytes, one send accepts, excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Records that synchronous UART sends leave no deferred transport work for this turn.
	 * Responsibilities: Do no work because TrySend writes each framed message directly to the UART.
	 */
	void PreAdvance(Core::TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Lets a caller gate every op on whether construction opened a usable UART.
	 * Responsibilities: Report the open flag set at construction and never mutated afterward except by destruction.
	 */
	bool IsOpen() const noexcept;

private:
	/**
	 * Motivation: Moves the decoder's held frame into the destination so a completed frame is delivered in one
	 *   transactional step.
	 * Responsibilities: Copy the payload, byte count, and sender node id and clear the held frame, or return Full
	 *   (leaving the frame held) when the payload exceeds the destination.
	 */
	Transport::ETransportResult DeliverFrameToDestination(
		Core::TSpan<std::uint8_t> InDestination, Transport::Address::FDeviceAddress& OutFrom, Core::FReceiveResult& OutResult) noexcept;

	/**
	 * Motivation: Drains a bounded byte budget through the decoder so a flood cannot starve the caller while still
	 *   completing a frame as soon as one arrives.
	 * Responsibilities: Pump the bounded UART byte budget through the decoder and deliver the first completed frame,
	 *   or return Unavailable when the budget drains with no frame ready.
	 */
	Transport::ETransportResult PumpDecoderForFrame(
		Core::TSpan<std::uint8_t> InDestination, Transport::Address::FDeviceAddress& OutFrom, Core::FReceiveResult& OutResult) noexcept;

	/**
	 * Motivation: Guards a send against a malformed address, oversize packet, or null span before any syscall so a
	 *   rejection is truly transactional.
	 * Responsibilities: Return the first reason an outgoing packet cannot be framed and sent, or Success.
	 */
	Transport::ETransportResult ValidateOutgoingPacket(
		const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) const noexcept;

	/** Motivation: Bounded RX deframer held by value; its capacity matches UartMaxPayloadBytes. */
	Transport::FrameCodec::TFrameDecoder<UartMaxPayloadBytes> Decoder{};

	/** Motivation: UART port number reinterpreted to its ESP-IDF type only in the source file. */
	std::int32_t UartPortNumber{0};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Motivation: Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Esp32
