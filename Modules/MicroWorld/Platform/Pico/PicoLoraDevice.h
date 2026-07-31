#pragma once

#include <MicroWorld/Platform/Pico/Internal/PicoE32LoraPlatform.h>
#include <MicroWorld/Platform/Pico/Internal/PicoUartByteStream.h>
#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Describes one RP2040 UART connection to an E32 module without exposing Pico SDK types.
 *
 * The composition root must give the device exclusive ownership of the selected UART; sharing it with another device
 * or UART-backed stdio is
 * unsupported.
 */
struct FPicoE32LoraConfig
{
	/** UART hardware index: `0` for UART0 or `1` for UART1. */
	std::uint8_t UartIndex{0};

	/** RP2040 GPIO routed from UART TX to the E32 RXD pin. */
	unsigned int TxGpio{0};

	/** RP2040 GPIO routed from the E32 TXD pin to UART RX. */
	unsigned int RxGpio{1};

	/** Exact UART baud rate configured on the E32; zero is invalid. */
	std::uint32_t BaudRate{0};

	/** Source node id stamped into every outgoing MicroWorld frame. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Released RP2040 Pico compatibility facade over the portable RadioE32 device.
 *
 * Construction is inert so static storage is safe before `main`; `Initialize` opens one exclusive UART and initializes
 * portable framing. `TrySend(Success)` queues one complete frame for `AdvanceTransmit`, while transparent-mode
 * destination addresses remain shape-checked metadata rather than on-air routing.
 */
class FPicoLoraDevice final : public IDevice
{
public:
	/** Creates a closed device that borrows the production Pico SDK UART binding. */
	FPicoLoraDevice() noexcept;

	/** Creates a closed device that borrows the supplied binding for host policy tests or alternate Pico wiring. */
	explicit FPicoLoraDevice(IPicoE32LoraPlatform& InPlatform) noexcept;

	/** Releases the delegated byte stream, which deinitializes the exclusively owned UART when initialization succeeded. */
	~FPicoLoraDevice() noexcept override;

	/** Prevents copying so one facade owns exactly one UART byte stream and delegated transport state. */
	FPicoLoraDevice(const FPicoLoraDevice&) = delete;

	/** Prevents copying so one facade owns exactly one UART byte stream and delegated transport state. */
	FPicoLoraDevice& operator=(const FPicoLoraDevice&) = delete;

	/** Prevents moving so byte-stream ownership and the delegated transport reference remain stable. */
	FPicoLoraDevice(FPicoLoraDevice&&) = delete;

	/** Prevents moving so byte-stream ownership and the delegated transport reference remain stable. */
	FPicoLoraDevice& operator=(FPicoLoraDevice&&) = delete;

	/**
	 * Validates and configures one exclusive RP2040 UART, then initializes portable E32 framing.
	 *
	 * Returns `Unavailable` when already open, `Invalid` for an unsupported index/pin mapping, zero baud, or a baud
	 * the SDK cannot produce exactly, and the delegated RadioE32 initialization result after opening the UART.
	 *
	 * @param InConfig UART identity, GPIO routing, baud rate, and local node id.
	 * @return Outcome of the initialization attempt.
	 */
	ETransportResult Initialize(const FPicoE32LoraConfig& InConfig) noexcept;

	/**
	 * Transactionally accepts one complete packet into the delegated fixed transmit slot.
	 *
	 * Returns `Unavailable` while closed, `Invalid` for a malformed address/span or oversize packet, `Full` while a
	 * prior frame remains queued, and `Success` once the delegated device queued the complete encoded frame for later
	 * physical progress.
	 *
	 * @param InTo Device-relative one-byte destination metadata; transparent mode does not route it on air.
	 * @param InPacket Payload to frame
	 * and queue.
	 * @return Outcome of the acceptance attempt.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Pumps a bounded number of UART bytes and transactionally delivers at most one frame.
	 *
	 * Every non-success result preserves the destination, sender address, and result. A `Full` result retains the
	 * decoded frame for a later retry with a larger destination.
	 *
	 * @param OutFrom Filled with the sender's E32 address only on `Success`.
	 * @param InDestination Destination for one decoded payload.
	 * @param OutResult Filled with the delivered byte count only on `Success`.
	 * @return `Success`, `Unavailable`, `Full`, or `Invalid` under the shared `IDevice` contract.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override;

	/** Reports the delegated shared E32 payload capacity, excluding framing overhead. */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Advances a bounded burst of up to one complete encoded frame's capacity when UART bytes are writable. */
	void AdvanceTransmit() noexcept override;

	/** Reports whether both the byte stream is open and the delegated RadioE32 device is initialized. */
	bool IsOpen() const noexcept;

private:
	/** Owns the configured RP2040 UART lifetime and provides bounded SDK-free byte operations to RadioE32. */
	FPicoUartByteStream ByteStream{};

	/** Owns portable E32 framing while borrowing the preceding byte stream for its full facade lifetime. */
	FE32LoraDevice RadioDevice{ByteStream};
};

} // namespace MicroWorld
