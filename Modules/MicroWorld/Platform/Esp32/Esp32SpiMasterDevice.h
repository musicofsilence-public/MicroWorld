#pragma once

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

struct FEsp32SpiMasterConfig;

/** Motivation: Sizes one wired SPI frame payload to match UartMaxPayloadBytes so every wired transport carries the same message size. */
constexpr std::size_t SpiMaxPayloadBytes = 120;

/** Motivation: Sizes one full-duplex SPI transaction so the DMA rules (rx buffer word-aligned, length a multiple of four) are met; pad bytes are
 * filler the decoder discards. */
constexpr std::size_t SpiTransactionWindowBytes = 128;

static_assert(
	SpiTransactionWindowBytes >= SpiMaxPayloadBytes + Transport::FrameCodec::FrameOverheadBytes,
	"SpiTransactionWindowBytes must hold one whole frame (payload plus framing overhead).");
static_assert(SpiTransactionWindowBytes % 4 == 0, "SPI DMA requires the transaction length to be a multiple of four bytes.");

/**
 * Motivation: Gives the application entry point a non-blocking Core::ITransportDevice for the SPI master side of a
 *   point-to-point link with fixed-size full-duplex transactions.
 * Responsibilities: Feed every received window into a bounded TFrameDecoder so full-duplex traffic is never
 *   discarded, validate every argument before any syscall, leave caller outputs unchanged on any non-Success
 *   result, and never split a frame across transactions.
 * Example:
 *   FEsp32SpiMasterDevice Master(Config);
 *   if (Master.IsOpen()) { Master.TryReceive(From, Dest, Result); }
 */
class FEsp32SpiMasterDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Initializes the SPI bus and adds the slave as its device before any traffic flows.
	 * Responsibilities: Initialize SpiHost on the given GPIOs at ClockHz in SPI mode 0 with DMA; on any failure
	 *   roll back and leave IsOpen false; never throw.
	 */
	explicit FEsp32SpiMasterDevice(const FEsp32SpiMasterConfig& InConfig) noexcept;

	/**
	 * Motivation: Releases the bus and device so construction-allocated ESP-IDF resources never leak.
	 * Responsibilities: Remove the device and free the SPI bus opened by construction.
	 */
	~FEsp32SpiMasterDevice() noexcept override;

	/**
	 * Motivation: Keeps one device value owning exactly one bus identity so the device handle never aliases.
	 * Responsibilities: Reject copy construction so the master stays the single owner of its bus.
	 */
	FEsp32SpiMasterDevice(const FEsp32SpiMasterDevice&) = delete;

	/**
	 * Motivation: Keeps one device value owning exactly one bus identity so the device handle never aliases.
	 * Responsibilities: Reject copy assignment so the master stays the single owner of its bus.
	 */
	FEsp32SpiMasterDevice& operator=(const FEsp32SpiMasterDevice&) = delete;

	/**
	 * Motivation: Keeps the owned device handle and DMA buffers fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move construction so the word-aligned DMA buffers never relocate.
	 */
	FEsp32SpiMasterDevice(FEsp32SpiMasterDevice&&) = delete;

	/**
	 * Motivation: Keeps the owned device handle and DMA buffers fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move assignment so the word-aligned DMA buffers never relocate.
	 */
	FEsp32SpiMasterDevice& operator=(FEsp32SpiMasterDevice&&) = delete;

	/**
	 * Motivation: Sends one complete framed message to the slave in a single full-duplex transaction, transactionally.
	 * Responsibilities: Return Invalid for a non-SPI destination, oversize packet, or null span with nonzero length,
	 *   Full when the transaction times out, and Success only after the whole frame is clocked out; feed the bytes the
	 *   slave clocks back into the decoder so a send never discards a pending reply.
	 */
	Core::ETransportResult TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Receives at most one framed message by clocking one idle full-duplex transaction, transactionally.
	 * Responsibilities: Pump the received bytes through the decoder and report Unavailable when the window holds no
	 *   frame, Full (frame held for a larger retry), Invalid (null destination with nonzero length), or Success after
	 *   a complete frame copies payload, byte count, and sender node id into OutFrom; leave outputs unchanged on any
	 *   non-success result.
	 */
	Core::ETransportResult TryReceive(
		Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size a packet against the transport's capacity without a magic number.
	 * Responsibilities: Report the largest payload, in bytes, one send accepts, excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Records that synchronous SPI master sends leave no deferred transport work for this turn.
	 * Responsibilities: Do no work because TrySend clocks each frame in its full-duplex transaction.
	 */
	void PreAdvance(Core::TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Lets a caller gate every op on whether construction opened a usable master bus.
	 * Responsibilities: Report the open flag set at construction and never mutated afterward except by destruction.
	 */
	bool IsOpen() const noexcept;

private:
	/**
	 * Motivation: Unifies the full-duplex exchange and the decoder feed so TrySend and TryReceive share one path.
	 * Responsibilities: Run one full-duplex transaction with the given transmit window, pump the received window into
	 *   the decoder only while no frame is already held, and return the transaction's send outcome.
	 */
	Core::ETransportResult ExchangeAndPump(const std::uint8_t* InTransmitWindow) noexcept;

	/** Motivation: Bounded RX deframer held by value; its capacity matches SpiMaxPayloadBytes. */
	Transport::FrameCodec::TFrameDecoder<SpiMaxPayloadBytes> Decoder{};

	/** Motivation: Transmit window (encoded frame plus idle padding); word-aligned for DMA. */
	alignas(4) std::uint8_t TransmitWindow[SpiTransactionWindowBytes]{};

	/** Motivation: Receive window filled by each transaction; word-aligned for DMA. */
	alignas(4) std::uint8_t ReceiveWindow[SpiTransactionWindowBytes]{};

	/** Motivation: All-idle window used as the transmit side of a receive-only transaction; word-aligned for DMA. */
	alignas(4) std::uint8_t IdleWindow[SpiTransactionWindowBytes]{};

	/** Motivation: ESP-IDF spi_device_handle_t stored opaquely; reinterpreted only in the source file. */
	void* DeviceHandle{nullptr};

	/** Motivation: SPI host number reinterpreted to its ESP-IDF type only in the source file. */
	std::int32_t SpiHostValue{0};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Motivation: Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Esp32
