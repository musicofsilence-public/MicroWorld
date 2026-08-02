#pragma once

#include <MicroWorld/Platform/Esp32/Esp32SpiMasterDevice.h>

#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/TFrameDecoder.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

struct FEsp32SpiSlaveConfig;

/** Motivation: Reserves opaque storage for one persistent ESP-IDF transaction descriptor that must outlive the slave's queue/harvest cycle. */
constexpr std::size_t SpiSlaveTransactionStorageBytes = 40;

/**
 * Motivation: Gives the application entry point a non-blocking Core::ITransportDevice for the SPI slave side of a
 *   point-to-point link that the master clocks.
 * Responsibilities: Stage one framed packet per TrySend for the next queued transaction, harvest a completed
 *   transaction per TryReceive and drain its received window through a bounded TFrameDecoder, and keep one
 *   transaction always queued; validate every argument before any syscall and leave caller outputs unchanged on
 *   any non-Success result.
 * Example:
 *   FEsp32SpiSlaveDevice Slave(Config);
 *   if (Slave.IsOpen()) { Slave.TrySend(To, Packet); }
 */
class FEsp32SpiSlaveDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Initializes the SPI bus as a slave and queues the first transaction so the master's first clock
	 *   finds a buffer instead of garbage.
	 * Responsibilities: Initialize SpiHost as a slave on the given GPIOs in SPI mode 0 with DMA and queue one idle
	 *   transaction; on any failure roll back and leave IsOpen false; never throw.
	 */
	explicit FEsp32SpiSlaveDevice(const FEsp32SpiSlaveConfig& InConfig) noexcept;

	/**
	 * Motivation: Releases the slave bus so construction-allocated ESP-IDF resources never leak.
	 * Responsibilities: Free the SPI slave bus opened by construction.
	 */
	~FEsp32SpiSlaveDevice() noexcept override;

	/**
	 * Motivation: Keeps one device value owning exactly one bus identity so the device handle never aliases.
	 * Responsibilities: Reject copy construction so the slave stays the single owner of its bus.
	 */
	FEsp32SpiSlaveDevice(const FEsp32SpiSlaveDevice&) = delete;

	/**
	 * Motivation: Keeps one device value owning exactly one bus identity so the device handle never aliases.
	 * Responsibilities: Reject copy assignment so the slave stays the single owner of its bus.
	 */
	FEsp32SpiSlaveDevice& operator=(const FEsp32SpiSlaveDevice&) = delete;

	/**
	 * Motivation: Keeps the owned buffers and persistent transaction descriptor fixed at one address for the link's
	 *   lifetime.
	 * Responsibilities: Reject move construction so the queued transaction's pointers never dangle.
	 */
	FEsp32SpiSlaveDevice(FEsp32SpiSlaveDevice&&) = delete;

	/**
	 * Motivation: Keeps the owned buffers and persistent transaction descriptor fixed at one address for the link's
	 *   lifetime.
	 * Responsibilities: Reject move assignment so the queued transaction's pointers never dangle.
	 */
	FEsp32SpiSlaveDevice& operator=(FEsp32SpiSlaveDevice&&) = delete;

	/**
	 * Motivation: Stages one complete framed message for the master's next read, transactionally.
	 * Responsibilities: Return Invalid for a non-SPI destination, oversize packet, or null span with nonzero length,
	 *   Full when a previously staged frame has not yet been queued, and Success once the whole frame is staged; the
	 *   staged frame is sent on the next transaction the master clocks.
	 */
	Core::ETransportResult TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Receives at most one framed message by harvesting a completed transaction, transactionally.
	 * Responsibilities: Harvest one completed transaction, pump its received window through the decoder, and re-queue
	 *   so a transaction is always ready; report Unavailable when none completed or the window held no frame, Full
	 *   (frame held for a larger retry), Invalid (null destination with nonzero length), or Success after a complete
	 *   frame copies payload, byte count, and sender node id into OutFrom.
	 */
	Core::ETransportResult TryReceive(
		Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size a packet against the transport's capacity without a magic number.
	 * Responsibilities: Report the largest payload, in bytes, one send accepts, excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Drains a staged reply into the next transaction before the master clocks it.
	 * Responsibilities: Queue one transaction when the open slave has none queued, consuming any staged frame.
	 */
	void PreAdvance(Core::TimePointMilliseconds) noexcept override
	{
		if (bOpen && !bTransactionQueued)
		{
			QueueNextTransaction();
		}
	}

	/**
	 * Motivation: Lets a caller gate every op on whether construction opened a usable slave bus.
	 * Responsibilities: Report the open flag set at construction and never mutated afterward except by destruction.
	 */
	bool IsOpen() const noexcept;

private:
	/**
	 * Motivation: Keeps one transaction always queued so the master always finds a buffer to clock.
	 * Responsibilities: Fill the transmit window (from the staged frame or idle) and queue one transaction, recording
	 *   whether the queue call succeeded.
	 */
	void QueueNextTransaction() noexcept;

	/** Motivation: Bounded RX deframer held by value; its capacity matches SpiMaxPayloadBytes. */
	Transport::FrameCodec::TFrameDecoder<SpiMaxPayloadBytes> Decoder{};

	/** Motivation: Receive window filled by each completed transaction; word-aligned for DMA. */
	alignas(4) std::uint8_t ReceiveWindow[SpiTransactionWindowBytes]{};

	/** Motivation: Transmit window owned by the device while a transaction is queued; word-aligned for DMA. */
	alignas(4) std::uint8_t TransmitWindow[SpiTransactionWindowBytes]{};

	/** Motivation: Staging buffer written by TrySend; copied into the transmit window at the next queue. */
	std::uint8_t StagedFrame[SpiTransactionWindowBytes]{};

	/** Motivation: Opaque storage for the persistent ESP-IDF transaction descriptor; constructed in the source file. */
	alignas(8) unsigned char TransactionStorage[SpiSlaveTransactionStorageBytes]{};

	/** Motivation: Points into TransactionStorage at the constructed descriptor; set in the source file. */
	void* TransactionPtr{nullptr};

	/** Motivation: SPI host number reinterpreted to its ESP-IDF type only in the source file. */
	std::int32_t SpiHostValue{0};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Motivation: True when a frame is staged in StagedFrame awaiting its next queue. */
	bool bFrameStaged{false};

	/** Motivation: True while one transaction is queued with the device; drives recovery re-queues. */
	bool bTransactionQueued{false};

	/** Motivation: Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Esp32
