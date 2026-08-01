#pragma once

#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/UartByteStream.h>
#include <MicroWorld/Transport/Lora/Internal/E32LoraTransportState.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Implements Core's ITransportDevice contract for an E32 LoRa module over a platform-provided UART byte stream so portable
 *   framing lives once and platform adapters own only UART configuration and lifetime.
 * Responsibilities: Perform no I/O at construction or initialization, and own portable framing, bounded physical progress, and
 *   transactional delivery over the borrowed byte stream.
 * Example:
 *   FE32LoraDevice Device(Uart);
 *   Device.Initialize(NodeId);
 *   Device.TrySend(To, Packet);
 *   Device.PreAdvance(Now);
 */
class FE32LoraDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Creates an inert device that borrows a byte stream the platform adapter keeps alive.
	 * Responsibilities: Store the byte stream reference without performing I/O.
	 */
	explicit FE32LoraDevice(Core::IUartByteStream& InByteStream) noexcept;

	/**
	 * Motivation: Arms the portable device with the source node id for future outgoing frames exactly once.
	 * Responsibilities: Return Success on the first call and Unavailable on later calls; perform no UART I/O on either path.
	 */
	ETransportResult Initialize(std::uint8_t InLocalNodeId) noexcept;

	/**
	 * Motivation: Accepts one complete packet transactionally into the fixed transmit slot so a rejected send never half-occupies it.
	 * Responsibilities: Return Unavailable before initialization, Invalid for a malformed address/span or oversize packet, Full
	 *   while another frame remains queued, and Success once this device owns the complete encoded frame.
	 */
	ETransportResult TrySend(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Pumps a bounded number of UART bytes and transactionally delivers at most one decoded frame so a caller never
	 *   observes a partial delivery.
	 * Responsibilities: Preserve the destination, sender address, and result on every non-success; return Full to retain the
	 *   decoded frame for a later retry, and map a UART Error to Invalid without changing caller outputs.
	 */
	ETransportResult TryReceive(
		::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
		Core::TSpan<std::uint8_t> InDestination,
		Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller bound a send to the E32 payload capacity.
	 * Responsibilities: Report the shared E32 payload capacity, excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Drains a queued frame by a fixed byte budget so non-blocking UART progress stays bounded.
	 * Responsibilities: Commit each byte only after UART Success, retain the current byte on Unavailable, and discard the queued
	 *   frame on Error so a permanent UART failure cannot keep later sends Full.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override;

	/**
	 * Motivation: Lets a caller guard byte-stream operations behind one successful initialization.
	 * Responsibilities: Report whether the portable device accepted a local node id.
	 */
	bool IsInitialized() const noexcept;

private:
	/** Motivation: Borrows the platform-owned non-blocking IUartByteStream for the device's full lifetime. */
	Core::IUartByteStream& ByteStream;

	/** Motivation: Owns fixed transmit framing, receive assembly, and retained decoded-frame state. */
	FE32LoraTransportState TransportState{};

	/** Motivation: Stamps every queued frame with the initialized local E32 node id. */
	std::uint8_t LocalNodeIdValue{0};

	/** Motivation: Prevents byte-stream operations before one successful, single-shot initialization. */
	bool bInitialized{false};
};

} // namespace MicroWorld::Transport
