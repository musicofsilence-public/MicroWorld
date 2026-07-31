#pragma once

#include <MicroWorld/Core/IO/UartByteStream.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/Lora/Internal/E32LoraTransportState.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Fixed-capacity, non-blocking E32 LoRa `IDevice` over a platform-provided UART byte stream.
 *
 * Construction and initialization perform no I/O. Platform adapters own UART configuration and lifetime, while this
 * driver owns portable framing, bounded physical progress, and transactional delivery over the borrowed byte stream.
 */
class FRadioE32Driver final : public IDevice
{
public:
	/** Creates an inert driver that borrows a byte stream the platform adapter keeps alive. */
	explicit FRadioE32Driver(IUartByteStream& InByteStream) noexcept;

	/**
	 * Initializes the portable driver with the source node id for future outgoing frames.
	 *
	 * The first call returns `Success`; later calls return `Unavailable`. Neither path performs UART I/O.
	 *
	 * @param InLocalNodeId Source node id stamped into every queued frame.
	 * @return `Success` on first initialization or `Unavailable` when already initialized.
	 */
	ETransportResult Initialize(std::uint8_t InLocalNodeId) noexcept;

	/**
	 * Transactionally accepts one complete packet into the fixed transmit slot.
	 *
	 * Returns `Unavailable` before initialization, `Invalid` for a malformed address/span or oversize packet, `Full`
	 * while another frame remains queued, and `Success` once this driver owns the complete encoded frame.
	 *
	 * @param InTo Driver-relative one-byte destination metadata; transparent mode does not route it on air.
	 * @param InPacket Payload to frame and queue.
	 * @return Outcome of the acceptance attempt.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Pumps a bounded number of UART bytes and transactionally delivers at most one decoded frame.
	 *
	 * Every non-success result preserves the destination, sender address, and result. A `Full` result retains the
	 * decoded frame for a later retry with a larger destination; a UART `Error` becomes `Invalid` without changing
	 * caller outputs.
	 *
	 * @param OutFrom Filled with the sender's E32 address only on `Success`.
	 * @param InDestination Destination for one decoded payload.
	 * @param OutResult Filled with the delivered byte count only on `Success`.
	 * @return `Success`, `Unavailable`, `Full`, or `Invalid` under the shared `IDevice` contract.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override;

	/** Reports the shared E32 payload capacity, excluding framing overhead. */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Advances one queued frame by a fixed encoded-frame byte budget.
	 *
	 * Each byte commits only after UART `Success`; `Unavailable` retains the current byte, while `Error` discards the
	 * queued frame so a permanent UART failure cannot keep later sends `Full`.
	 */
	void AdvanceTransmit() noexcept override;

	/** Reports whether the portable driver accepted a local node id and may use its byte stream. */
	bool IsInitialized() const noexcept;

private:
	/** Borrows the platform-owned non-blocking `IUartByteStream` for the driver's full lifetime. */
	IUartByteStream& ByteStream;

	/** Owns fixed transmit framing, receive assembly, and retained decoded-frame state. */
	FE32LoraTransportState TransportState{};

	/** Stamps every queued frame with the initialized local E32 node id. */
	std::uint8_t LocalNodeIdValue{0};

	/** Prevents byte-stream operations before one successful, single-shot initialization. */
	bool bInitialized{false};
};

} // namespace MicroWorld
