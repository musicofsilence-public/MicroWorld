#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/Lora/E32Lora.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Owns the deterministic, SDK-free E32 frame state shared by the portable FE32LoraDevice and its host tests.
 * Responsibilities: Accept one complete transmit frame, expose it one byte at a time for non-blocking UART progress, hold one
 *   decoded receive frame until transactional delivery succeeds, and perform no hardware access.
 * Example:
 *   FE32LoraTransportState State;
 *   State.TryQueueFrame(NodeId, To, Packet);
 *   State.PushReceivedByte(Byte);
 */
class FE32LoraTransportState final
{
public:
	/**
	 * Motivation: Creates empty transmit and receive state ready for first use.
	 * Responsibilities: Default-construct fixed storage without touching hardware.
	 */
	FE32LoraTransportState() noexcept = default;

	/**
	 * Motivation: Keeps a fixed-capacity value type side-effect free on destruction.
	 * Responsibilities: Release no resource since every buffer is fixed value storage.
	 */
	~FE32LoraTransportState() noexcept = default;

	/**
	 * Motivation: Prevents copying so one state value owns exactly one transmit slot and decoder.
	 * Responsibilities: Reject copy construction so two states never alias one transmit frame.
	 */
	FE32LoraTransportState(const FE32LoraTransportState&) = delete;

	/**
	 * Motivation: Prevents copying so one state value owns exactly one transmit slot and decoder.
	 * Responsibilities: Reject copy assignment so two states never alias one transmit frame.
	 */
	FE32LoraTransportState& operator=(const FE32LoraTransportState&) = delete;

	/**
	 * Motivation: Prevents moving so spans returned by the embedded decoder never outlive their storage address.
	 * Responsibilities: Reject move construction so a previously returned span never dangles.
	 */
	FE32LoraTransportState(FE32LoraTransportState&&) = delete;

	/**
	 * Motivation: Prevents moving so spans returned by the embedded decoder never outlive their storage address.
	 * Responsibilities: Reject move assignment so a previously returned span never dangles.
	 */
	FE32LoraTransportState& operator=(FE32LoraTransportState&&) = delete;

	/**
	 * Motivation: Accepts one payload transactionally into the fixed transmit slot so a rejected queue never half-occupies it.
	 * Responsibilities: Require the one-byte E32 address shape (not written on air in transparent mode), return Invalid for an
	 *   invalid address or span, Full while another frame remains queued, and Success once the complete encoded frame is owned.
	 */
	Core::ETransportResult TryQueueFrame(
		std::uint8_t InLocalNodeId, const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept;

	/**
	 * Motivation: Lets the device read the next transmit byte before committing it to the UART.
	 * Responsibilities: Fill OutByte without advancing state and return true when a queued byte is available; leave it unchanged
	 *   and return false otherwise.
	 */
	bool TryPeekTransmitByte(std::uint8_t& OutByte) const noexcept;

	/**
	 * Motivation: Advances the transmit cursor after a byte is successfully written.
	 * Responsibilities: Move past one previously peeked byte and release the slot after the final byte.
	 */
	void CommitTransmitByte() noexcept;

	/**
	 * Motivation: Frees the transmit slot after a hard UART failure so later sends can reuse it.
	 * Responsibilities: Drop the queued frame without writing any further byte.
	 */
	void DiscardTransmitFrame() noexcept;

	/**
	 * Motivation: Lets the device decide whether more transmit progress is possible.
	 * Responsibilities: Report whether at least one encoded transmit byte remains queued.
	 */
	bool HasPendingTransmit() const noexcept;

	/**
	 * Motivation: Feeds received UART bytes into the bounded decoder without overwriting a held frame.
	 * Responsibilities: Return FrameReady without consuming InByte while a frame is held, otherwise forward the byte and return
	 *   the decoder event it produced.
	 */
	::MicroWorld::Transport::FrameCodec::EFrameEvent PushReceivedByte(std::uint8_t InByte) noexcept;

	/**
	 * Motivation: Lets the device decide whether a decoded frame is ready to deliver.
	 * Responsibilities: Report whether a CRC-valid receive frame is held for delivery.
	 */
	bool HasReceivedFrame() const noexcept;

	/**
	 * Motivation: Delivers the held frame transactionally so a failed delivery retains it for retry.
	 * Responsibilities: Return Unavailable when no frame is held, Invalid for a null destination with nonzero size, Full when the
	 *   held payload does not fit, and Success after copying the payload, sender address, and byte count; every non-success
	 *   preserves all caller outputs and retains the held frame.
	 */
	Core::ETransportResult TryDeliverReceivedFrame(
		Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept;

private:
	/** Motivation: Owns bounded receive assembly and retains one complete frame across a Full retry. */
	::MicroWorld::Transport::FrameCodec::TFrameDecoder<E32MaxPayloadBytes> Decoder{};

	/** Motivation: Holds one complete encoded frame until the UART accepts every byte. */
	std::uint8_t TransmitFrame[E32MaxPayloadBytes + ::MicroWorld::Transport::FrameCodec::FrameOverheadBytes]{};

	/** Motivation: Counts meaningful bytes in TransmitFrame; zero means the transmit slot is free. */
	std::size_t TransmitFrameLength{0};

	/** Motivation: Identifies the next queued byte that has not yet been committed as transmitted. */
	std::size_t NextTransmitByteIndex{0};
};

} // namespace MicroWorld::Transport
