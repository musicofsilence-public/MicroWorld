#pragma once

#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/NetAddress.h>
#include <MicroWorld/Transport/NetDriver.h>
#include <MicroWorld/Transport/NetResult.h>
#include <MicroWorld/Platform/Esp32/SpiAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Largest single-transmission payload one wired SPI frame carries.
 *
 * Kept equal to `UartMaxPayloadBytes` so every wired transport carries the same message size and only the
 * driver construction differs.
 */
constexpr std::size_t SpiMaxPayloadBytes = 120;

/**
 * Bytes clocked in one full-duplex SPI transaction: one maximum frame padded up so the DMA rules
 * (rx buffer word-aligned, length a multiple of four) are met; the pad bytes are filler the decoder discards.
 */
constexpr std::size_t SpiTransactionWindowBytes = 128;

static_assert(
	SpiTransactionWindowBytes >= SpiMaxPayloadBytes + FrameOverheadBytes,
	"SpiTransactionWindowBytes must hold one whole frame (payload plus framing overhead).");
static_assert(SpiTransactionWindowBytes % 4 == 0, "SPI DMA requires the transaction length to be a multiple of four bytes.");

/**
 * Bytes of opaque storage the slave driver reserves for one persistent ESP-IDF transaction descriptor.
 *
 * A queued SPI-slave transaction must outlive the queue/harvest cycle, so its descriptor cannot be a stack
 * local; the source file places the real ESP-IDF type in this buffer and `static_assert`s that it fits.
 */
constexpr std::size_t SpiSlaveTransactionStorageBytes = 40;

/**
 * Construction parameters for the wired SPI master driver.
 *
 * Holds plain-integer bus parameters so the public header stays free of the ESP-IDF SPI enum types; the
 * platform-implementation header reinterprets them on the ESP32 side.
 */
struct FEsp32SpiMasterConfig
{
	/** SPI host number (ESP-IDF `spi_host_device_t`, e.g. SPI2_HOST == 1) passed as a plain integer. */
	std::int32_t SpiHost{1};

	/** MOSI GPIO number shared with the slave's MOSI pin, passed as a plain integer. */
	std::int32_t MosiGpio{0};

	/** MISO GPIO number shared with the slave's MISO pin, passed as a plain integer. */
	std::int32_t MisoGpio{0};

	/** SCLK GPIO number shared with the slave's SCLK pin, passed as a plain integer. */
	std::int32_t SclkGpio{0};

	/** CS GPIO number shared with the slave's CS pin, passed as a plain integer. */
	std::int32_t CsGpio{0};

	/** SPI clock (SCLK) frequency in hertz (1 MHz is reliable over short jumper wires). */
	std::uint32_t ClockHz{1000000};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Construction parameters for the wired SPI slave driver.
 *
 * Holds plain-integer bus parameters so the public header stays free of the ESP-IDF SPI enum types; the
 * slave is clocked by the master, so it carries no clock frequency.
 */
struct FEsp32SpiSlaveConfig
{
	/** SPI host number (ESP-IDF `spi_host_device_t`, e.g. SPI2_HOST == 1) passed as a plain integer. */
	std::int32_t SpiHost{1};

	/** MOSI GPIO number shared with the master's MOSI pin, passed as a plain integer. */
	std::int32_t MosiGpio{0};

	/** MISO GPIO number shared with the master's MISO pin, passed as a plain integer. */
	std::int32_t MisoGpio{0};

	/** SCLK GPIO number shared with the master's SCLK pin, passed as a plain integer. */
	std::int32_t SclkGpio{0};

	/** CS GPIO number shared with the master's CS pin, passed as a plain integer. */
	std::int32_t CsGpio{0};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Non-blocking wired `INetDriver` for the master side of a point-to-point SPI link.
 *
 * It clocks the bus with fixed-size full-duplex transactions: because every transaction moves both
 * directions, both `TrySend` and `TryReceive` feed the received window into a bounded `TFrameDecoder`, and
 * `TryReceive` delivers completed frames. It validates every argument before any syscall, leaves caller
 * outputs unchanged on any non-`Success` result, and exercises no bus traffic until example 21's hardware
 * checkpoint passes (§1.2).
 */
class FEsp32SpiMasterDriver final : public INetDriver
{
public:
	/**
	 * Initializes the SPI bus and adds the slave as its device.
	 *
	 * Initializes `SpiHost` on the given MOSI/MISO/SCLK/CS GPIOs at `ClockHz` in SPI mode 0 with DMA. On any
	 * failure the constructor rolls back and leaves `IsOpen() == false`; it never throws. The local node id is
	 * stamped on every outgoing frame.
	 *
	 * @param InConfig Host, GPIO, clock, and local node id parameters.
	 */
	explicit FEsp32SpiMasterDriver(const FEsp32SpiMasterConfig& InConfig) noexcept;

	/** Removes the device and frees the SPI bus opened by construction. */
	~FEsp32SpiMasterDriver() noexcept override;

	/** Prevents copying so one driver value owns exactly one bus identity. */
	FEsp32SpiMasterDriver(const FEsp32SpiMasterDriver&) = delete;

	/** Prevents copying so one driver value owns exactly one bus identity. */
	FEsp32SpiMasterDriver& operator=(const FEsp32SpiMasterDriver&) = delete;

	/** Prevents moving so the owned device handle and DMA buffers stay fixed. */
	FEsp32SpiMasterDriver(FEsp32SpiMasterDriver&&) = delete;

	/** Prevents moving so the owned device handle and DMA buffers stay fixed. */
	FEsp32SpiMasterDriver& operator=(FEsp32SpiMasterDriver&&) = delete;

	/**
	 * Sends one complete framed message to the slave in a single full-duplex transaction, transactionally.
	 *
	 * Returns `Invalid` for a destination that is not an SPI encoding, an oversize packet, or a null span with
	 * nonzero length; `Full` when the transaction times out; and `Success` only when the whole frame was
	 * clocked out. The bytes the slave clocks back during the same transaction are fed to the decoder (SPI is
	 * full-duplex), so a send never discards a pending reply.
	 *
	 * @param InTo Destination whose single byte must be an SPI node id (validated; the wire is point-to-point).
	 * @param InPacket Caller-owned payload bytes framed and sent as one message.
	 * @return Normalized outcome of the single send attempt.
	 */
	ENetResult TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Receives at most one framed message by clocking one idle full-duplex transaction, transactionally.
	 *
	 * Clocks a window with an idle transmit and pumps the received bytes through the decoder; `Unavailable`
	 * when the window holds no frame, `Full` when the held frame exceeds the destination (kept held for a
	 * larger retry), `Invalid` for a null destination with nonzero length, and `Success` after a complete
	 * frame copies its payload, byte count, and sender node id into `OutFrom`.
	 *
	 * @param OutFrom Filled with the sender's SPI address only on `Success`.
	 * @param InDestination Caller-owned buffer for the received payload bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept override;

	/** Reports the largest payload, in bytes, one send accepts (excludes framing overhead). */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable SPI master bus. */
	bool IsOpen() const noexcept;

private:
	/** Runs one full-duplex transaction with the given transmit window and pumps the received window into
	 * the decoder (only while no frame is already held); returns the transaction's send outcome. */
	ENetResult ExchangeAndPump(const std::uint8_t* InTransmitWindow) noexcept;

	/** Bounded RX deframer held by value; its capacity matches `SpiMaxPayloadBytes`. */
	TFrameDecoder<SpiMaxPayloadBytes> Decoder{};

	/** Transmit window (encoded frame plus idle padding); word-aligned for DMA. */
	alignas(4) std::uint8_t TransmitWindow[SpiTransactionWindowBytes]{};

	/** Receive window filled by each transaction; word-aligned for DMA. */
	alignas(4) std::uint8_t ReceiveWindow[SpiTransactionWindowBytes]{};

	/** All-idle window used as the transmit side of a receive-only transaction; word-aligned for DMA. */
	alignas(4) std::uint8_t IdleWindow[SpiTransactionWindowBytes]{};

	/** ESP-IDF `spi_device_handle_t` stored opaquely; reinterpreted only in the source file. */
	void* DeviceHandle{nullptr};

	/** SPI host number reinterpreted to its ESP-IDF type only in the source file. */
	std::int32_t SpiHostValue{0};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

/**
 * Non-blocking wired `INetDriver` for the slave side of a point-to-point SPI link.
 *
 * The master clocks every transfer, so `TrySend` stages one framed packet for the next queued transaction
 * and `TryReceive` harvests a completed transaction and drains its received window through a bounded
 * `TFrameDecoder`, keeping one transaction always queued. It validates every argument before any syscall,
 * leaves caller outputs unchanged on any non-`Success` result, and exercises no bus traffic until example
 * 21's hardware checkpoint passes (§1.2).
 */
class FEsp32SpiSlaveDriver final : public INetDriver
{
public:
	/**
	 * Initializes the SPI bus as a slave and queues the first transaction.
	 *
	 * Initializes `SpiHost` as a slave on the given MOSI/MISO/SCLK/CS GPIOs in SPI mode 0 with DMA and queues
	 * one idle transaction so the master's first clock finds a buffer. On any failure the constructor rolls
	 * back and leaves `IsOpen() == false`; it never throws. The local node id is stamped on every outgoing frame.
	 *
	 * @param InConfig Host, GPIO, and local node id parameters.
	 */
	explicit FEsp32SpiSlaveDriver(const FEsp32SpiSlaveConfig& InConfig) noexcept;

	/** Frees the SPI slave bus opened by construction. */
	~FEsp32SpiSlaveDriver() noexcept override;

	/** Prevents copying so one driver value owns exactly one bus identity. */
	FEsp32SpiSlaveDriver(const FEsp32SpiSlaveDriver&) = delete;

	/** Prevents copying so one driver value owns exactly one bus identity. */
	FEsp32SpiSlaveDriver& operator=(const FEsp32SpiSlaveDriver&) = delete;

	/** Prevents moving so the owned buffers and transaction descriptor stay fixed. */
	FEsp32SpiSlaveDriver(FEsp32SpiSlaveDriver&&) = delete;

	/** Prevents moving so the owned buffers and transaction descriptor stay fixed. */
	FEsp32SpiSlaveDriver& operator=(FEsp32SpiSlaveDriver&&) = delete;

	/**
	 * Stages one complete framed message for the master's next read, transactionally.
	 *
	 * Returns `Invalid` for a destination that is not an SPI encoding, an oversize packet, or a null span with
	 * nonzero length; `Full` when a previously staged frame has not yet been queued; and `Success` when the
	 * whole frame was staged. The staged frame is sent on the next transaction the master clocks.
	 *
	 * @param InTo Destination whose single byte must be an SPI node id (validated; the wire is point-to-point).
	 * @param InPacket Caller-owned payload bytes framed and staged as one message.
	 * @return Normalized outcome of the single send attempt.
	 */
	ENetResult TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Receives at most one framed message by harvesting a completed transaction, transactionally.
	 *
	 * Harvests one completed transaction, pumps its received window through the decoder, and re-queues so a
	 * transaction is always ready; `Unavailable` when none completed or the window held no frame, `Full` when
	 * the held frame exceeds the destination, `Invalid` for a null destination with nonzero length, and
	 * `Success` after a complete frame copies its payload, byte count, and sender node id into `OutFrom`.
	 *
	 * @param OutFrom Filled with the sender's SPI address only on `Success`.
	 * @param InDestination Caller-owned buffer for the received payload bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept override;

	/** Reports the largest payload, in bytes, one send accepts (excludes framing overhead). */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable SPI slave bus. */
	bool IsOpen() const noexcept;

private:
	/** Fills the transmit window (from the staged frame or idle) and queues one transaction so the master
	 * always finds a buffer to clock; records whether the queue call succeeded. */
	void QueueNextTransaction() noexcept;

	/** Bounded RX deframer held by value; its capacity matches `SpiMaxPayloadBytes`. */
	TFrameDecoder<SpiMaxPayloadBytes> Decoder{};

	/** Receive window filled by each completed transaction; word-aligned for DMA. */
	alignas(4) std::uint8_t ReceiveWindow[SpiTransactionWindowBytes]{};

	/** Transmit window owned by the driver while a transaction is queued; word-aligned for DMA. */
	alignas(4) std::uint8_t TransmitWindow[SpiTransactionWindowBytes]{};

	/** Staging buffer written by `TrySend`; copied into the transmit window at the next queue. */
	std::uint8_t StagedFrame[SpiTransactionWindowBytes]{};

	/** Opaque storage for the persistent ESP-IDF transaction descriptor; constructed in the source file. */
	alignas(8) unsigned char TransactionStorage[SpiSlaveTransactionStorageBytes]{};

	/** Points into `TransactionStorage` at the constructed descriptor; set in the source file. */
	void* TransactionPtr{nullptr};

	/** SPI host number reinterpreted to its ESP-IDF type only in the source file. */
	std::int32_t SpiHostValue{0};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** True when a frame is staged in `StagedFrame` awaiting its next queue. */
	bool bFrameStaged{false};

	/** True while one transaction is queued with the driver; drives recovery re-queues. */
	bool bTransactionQueued{false};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld
