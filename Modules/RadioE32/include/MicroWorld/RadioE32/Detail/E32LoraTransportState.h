#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/E32Lora.h>
#include <MicroWorld/Net/FrameCodec.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Detail
{

/**
 * Owns the deterministic, SDK-free E32 frame state used by the portable RadioE32 driver and its host tests.
 *
 * The state accepts one complete transmit frame, exposes it one byte at a time for non-blocking UART progress, and
 * holds one decoded receive frame until transactional delivery succeeds. It performs no hardware access.
 */
class FE32LoraTransportState final
{
public:
	/** Creates empty transmit and receive state without touching hardware. */
	FE32LoraTransportState() noexcept = default;

	/** Releases no resources because every buffer is fixed-capacity value storage. */
	~FE32LoraTransportState() noexcept = default;

	/** Prevents copying so one state value owns exactly one transmit slot and decoder. */
	FE32LoraTransportState(const FE32LoraTransportState&) = delete;

	/** Prevents copying so one state value owns exactly one transmit slot and decoder. */
	FE32LoraTransportState& operator=(const FE32LoraTransportState&) = delete;

	/** Prevents moving so spans returned by the embedded decoder never outlive their storage address. */
	FE32LoraTransportState(FE32LoraTransportState&&) = delete;

	/** Prevents moving so spans returned by the embedded decoder never outlive their storage address. */
	FE32LoraTransportState& operator=(FE32LoraTransportState&&) = delete;

	/**
	 * Transactionally accepts one payload into the fixed transmit slot.
	 *
	 * The destination must have the one-byte E32 shape but is not written on air in transparent mode. Returns
	 * `Invalid` for an invalid address or span, `Full` while another frame remains queued, and `Success` once the
	 * complete encoded frame is owned by this state.
	 *
	 * @param InLocalNodeId Source node id stamped into the encoded frame.
	 * @param InTo Driver-relative destination metadata whose one-byte shape is validated.
	 * @param InPacket Payload to encode into the single transmit slot.
	 * @return Outcome of the acceptance attempt.
	 */
	ENetResult TryQueueFrame(std::uint8_t InLocalNodeId, const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept;

	/**
	 * Reads the next queued transmit byte without advancing state.
	 *
	 * @param OutByte Filled only when a queued byte is available.
	 * @return True when `OutByte` was filled; false leaves it unchanged.
	 */
	bool TryPeekTransmitByte(std::uint8_t& OutByte) const noexcept;

	/** Advances past one previously peeked/transmitted byte and releases the slot after the final byte. */
	void CommitTransmitByte() noexcept;

	/** Releases a queued frame after a hard UART write failure so later sends can reuse the fixed slot. */
	void DiscardTransmitFrame() noexcept;

	/** Reports whether at least one encoded transmit byte remains queued. */
	bool HasPendingTransmit() const noexcept;

	/**
	 * Feeds one received UART byte into the bounded decoder.
	 *
	 * A held frame is never overwritten: while one is held this returns `FrameReady` without consuming `InByte`.
	 *
	 * @param InByte Next byte received from the E32 UART stream.
	 * @return Decoder event produced by this byte, or `FrameReady` when a prior frame is still held.
	 */
	EFrameEvent PushReceivedByte(std::uint8_t InByte) noexcept;

	/** Reports whether a CRC-valid receive frame is held for delivery. */
	bool HasReceivedFrame() const noexcept;

	/**
	 * Transactionally delivers the held frame into caller-owned outputs.
	 *
	 * Returns `Unavailable` when no frame is held, `Invalid` for a null destination with nonzero size, `Full` when
	 * the held payload does not fit, and `Success` after copying the payload, sender address, and byte count. Every
	 * non-success result preserves all caller outputs and retains any held frame.
	 *
	 * @param OutFrom Filled with the sender's E32 address only on `Success`.
	 * @param InDestination Destination for the held payload.
	 * @param OutResult Filled with the delivered byte count only on `Success`.
	 * @return Outcome of the delivery attempt.
	 */
	ENetResult TryDeliverReceivedFrame(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept;

private:
	/** Owns bounded receive assembly and retains one complete frame across a `Full` retry. */
	TFrameDecoder<E32MaxPayloadBytes> Decoder{};

	/** Holds one complete encoded frame until the UART accepts every byte. */
	std::uint8_t TransmitFrame[E32MaxPayloadBytes + FrameOverheadBytes]{};

	/** Counts meaningful bytes in `TransmitFrame`; zero means the transmit slot is free. */
	std::size_t TransmitFrameLength{0};

	/** Identifies the next queued byte that has not yet been committed as transmitted. */
	std::size_t NextTransmitByteIndex{0};
};

} // namespace MicroWorld::Detail
