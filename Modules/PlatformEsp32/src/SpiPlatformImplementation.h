#pragma once

// =============================================================================
// src/SpiPlatformImplementation.h is the SOLE header that pulls ESP-IDF SPI headers.
// It is included by one driver translation unit — Esp32SpiDriver.cpp (the wired
// point-to-point SPI master/slave link pair) — and a public header must never reach
// it. Every ESP-IDF SPI divergence is hidden behind the helpers below so both driver
// classes read one platform-free path that mirrors the UART and I2C drivers. Example
// 21's master-clocked ping-pong runtime-verifies this path on ESP32-S3 (2026-07-23):
// SPI is full-duplex, so every master transaction both sends and receives (the master
// driver feeds the received window to its decoder rather than discarding it — confirmed
// working); the slave is queue-based, so the driver keeps one persistent transaction
// descriptor queued and the FrameCodec CRC rejects any garbage a momentary empty-queue
// gap produces. Error/timeout branches stayed unexercised (every exchange succeeded).
// DMA is used (SPI_DMA_CH_AUTO), so the driver's transmit/receive buffers must live in
// internal RAM — the example composition root makes each driver static, as the ESP32-S3
// stack lesson (docs/WIRED_TRANSPORTS_ROADMAP.md §2.2) already requires. See §1.2.
// =============================================================================

#include <MicroWorld/PlatformEsp32/Esp32SpiDriver.h>

#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <driver/spi_slave.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Detail
{

/** Normalized result of one full-duplex SPI master transaction. */
enum class ESpiTransmitOutcome : std::uint8_t
{
	/** The whole window was clocked out and in. */
	Sent,
	/** The transaction timed out; treat as a transient full condition. */
	WouldBlock,
	/** Any other SPI error. */
	Error,
};

/** Result of initializing the SPI bus as a master and adding its single device. */
struct FOpenedSpiMaster
{
	/** ESP-IDF master device handle, valid only when `bOpen` is true. */
	spi_device_handle_t Device;
	/** SPI host number the bus was initialized on. */
	int Host;
	/** True when the bus and device were initialized; false when construction rolled back. */
	bool bOpen;
};

/** Result of initializing the SPI bus as a slave. */
struct FOpenedSpiSlave
{
	/** SPI host number the slave bus was initialized on. */
	int Host;
	/** True when the slave bus initialized; false when construction rolled back. */
	bool bOpen;
};

/** Reinterprets the opaque stored host number as its ESP-IDF SPI host type. */
inline spi_host_device_t AsSpiHost(const int InHost) noexcept
{
	return static_cast<spi_host_device_t>(InHost);
}

/**
 * Initializes the SPI bus as a master and adds the slave as its only device.
 *
 * Uses SPI mode 0 at `ClockHz` with an automatically selected DMA channel and a transfer size of one whole
 * window. On any failure the partially initialized bus is freed so the caller sees `bOpen == false` and can
 * leave the driver inert without throwing.
 *
 * @param InHost SPI host number to initialize.
 * @param InMosi MOSI GPIO number.
 * @param InMiso MISO GPIO number.
 * @param InSclk SCLK GPIO number.
 * @param InCs CS GPIO number.
 * @param InClockHz SCLK frequency in hertz.
 * @return Opened-master descriptor reporting whether initialization succeeded.
 */
inline FOpenedSpiMaster OpenConfiguredSpiMaster(
	const int InHost,
	const std::int32_t InMosi,
	const std::int32_t InMiso,
	const std::int32_t InSclk,
	const std::int32_t InCs,
	const std::uint32_t InClockHz) noexcept
{
	spi_bus_config_t BusConfig{};
	BusConfig.mosi_io_num = InMosi;
	BusConfig.miso_io_num = InMiso;
	BusConfig.sclk_io_num = InSclk;
	BusConfig.quadwp_io_num = -1;
	BusConfig.quadhd_io_num = -1;
	BusConfig.max_transfer_sz = static_cast<int>(SpiTransactionWindowBytes);
	if (spi_bus_initialize(AsSpiHost(InHost), &BusConfig, SPI_DMA_CH_AUTO) != ESP_OK)
	{
		return FOpenedSpiMaster{nullptr, InHost, false};
	}
	spi_device_interface_config_t DeviceConfig{};
	DeviceConfig.mode = 0;
	DeviceConfig.clock_speed_hz = static_cast<int>(InClockHz);
	DeviceConfig.spics_io_num = InCs;
	DeviceConfig.queue_size = 1;
	spi_device_handle_t Device = nullptr;
	if (spi_bus_add_device(AsSpiHost(InHost), &DeviceConfig, &Device) != ESP_OK)
	{
		(void)spi_bus_free(AsSpiHost(InHost));
		return FOpenedSpiMaster{nullptr, InHost, false};
	}
	return FOpenedSpiMaster{Device, InHost, true};
}

/**
 * Runs one full-duplex master transaction, clocking the transmit window out and the receive window in.
 *
 * A timeout maps to `WouldBlock`; any other error maps to `Error`. Runtime-verified by example 21
 * (2026-07-23): the full-duplex transaction round-tripped every volley (error/timeout branches unexercised).
 *
 * @param InDevice Open SPI master device handle.
 * @param InTransmitBytes First byte of the transmit window.
 * @param OutReceiveBytes First byte of the receive window (filled by the transaction).
 * @param InLengthBytes Window length in bytes (both directions transfer this many).
 * @return Normalized outcome of the single transaction.
 */
inline ESpiTransmitOutcome TransmitSpiMaster(
	const spi_device_handle_t InDevice,
	const std::uint8_t* const InTransmitBytes,
	std::uint8_t* const OutReceiveBytes,
	const std::size_t InLengthBytes) noexcept
{
	spi_transaction_t Transaction{};
	Transaction.length = InLengthBytes * 8;
	Transaction.tx_buffer = InTransmitBytes;
	Transaction.rx_buffer = OutReceiveBytes;
	const esp_err_t Result = spi_device_transmit(InDevice, &Transaction);
	if (Result == ESP_OK)
	{
		return ESpiTransmitOutcome::Sent;
	}
	if (Result == ESP_ERR_TIMEOUT)
	{
		return ESpiTransmitOutcome::WouldBlock;
	}
	return ESpiTransmitOutcome::Error;
}

/**
 * Removes the device and frees the master bus opened by `OpenConfiguredSpiMaster`.
 *
 * Each step is a safe no-op or ignored return because the driver is already going inert and there is no
 * recovery action at this layer.
 *
 * @param InHost SPI host number to free.
 * @param InDevice Master device handle to remove.
 */
inline void CloseSpiMaster(const int InHost, const spi_device_handle_t InDevice) noexcept
{
	if (InDevice != nullptr)
	{
		(void)spi_bus_remove_device(InDevice);
	}
	(void)spi_bus_free(AsSpiHost(InHost));
}

/**
 * Initializes the SPI bus as a slave listening on the given pins.
 *
 * Uses SPI mode 0 with an automatically selected DMA channel; the master supplies the clock, so no speed is
 * set here. On failure the caller sees `bOpen == false` and can leave the driver inert without throwing.
 *
 * @param InHost SPI host number to initialize.
 * @param InMosi MOSI GPIO number.
 * @param InMiso MISO GPIO number.
 * @param InSclk SCLK GPIO number.
 * @param InCs CS GPIO number.
 * @return Opened-slave descriptor reporting whether initialization succeeded.
 */
inline FOpenedSpiSlave OpenConfiguredSpiSlave(
	const int InHost, const std::int32_t InMosi, const std::int32_t InMiso, const std::int32_t InSclk, const std::int32_t InCs) noexcept
{
	spi_bus_config_t BusConfig{};
	BusConfig.mosi_io_num = InMosi;
	BusConfig.miso_io_num = InMiso;
	BusConfig.sclk_io_num = InSclk;
	BusConfig.quadwp_io_num = -1;
	BusConfig.quadhd_io_num = -1;
	BusConfig.max_transfer_sz = static_cast<int>(SpiTransactionWindowBytes);
	spi_slave_interface_config_t SlaveConfig{};
	SlaveConfig.spics_io_num = InCs;
	SlaveConfig.flags = 0;
	SlaveConfig.queue_size = 3;
	SlaveConfig.mode = 0;
	SlaveConfig.post_setup_cb = nullptr;
	SlaveConfig.post_trans_cb = nullptr;
	if (spi_slave_initialize(AsSpiHost(InHost), &BusConfig, &SlaveConfig, SPI_DMA_CH_AUTO) != ESP_OK)
	{
		return FOpenedSpiSlave{InHost, false};
	}
	return FOpenedSpiSlave{InHost, true};
}

/**
 * Queues one full-duplex slave transaction so the master's next clock finds a buffer.
 *
 * Points the caller-owned persistent descriptor at the transmit and receive windows and queues it without
 * blocking. Runtime-verified by example 21 (2026-07-23): the queued descriptor served the master's clocks.
 *
 * @param InHost SPI host number acting as a slave.
 * @param OutTransaction Caller-owned persistent transaction descriptor (must outlive the queue/harvest cycle).
 * @param InTransmitBytes First byte of the transmit window.
 * @param InReceiveBytes First byte of the receive window.
 * @param InLengthBytes Window length in bytes.
 * @return True when the transaction was queued.
 */
inline bool QueueSpiSlave(
	const int InHost,
	spi_slave_transaction_t* const OutTransaction,
	const std::uint8_t* const InTransmitBytes,
	std::uint8_t* const InReceiveBytes,
	const std::size_t InLengthBytes) noexcept
{
	*OutTransaction = spi_slave_transaction_t{};
	OutTransaction->length = InLengthBytes * 8;
	OutTransaction->tx_buffer = InTransmitBytes;
	OutTransaction->rx_buffer = InReceiveBytes;
	return spi_slave_queue_trans(AsSpiHost(InHost), OutTransaction, 0) == ESP_OK;
}

/**
 * Harvests one completed slave transaction without blocking.
 *
 * Returns true when a queued transaction has completed (its receive window now holds the master's bytes) and
 * false when none is done yet. Runtime-verified by example 21 (2026-07-23): harvested frames decoded correctly.
 *
 * @param InHost SPI host number acting as a slave.
 * @return True when a transaction completed.
 */
inline bool HarvestSpiSlave(const int InHost) noexcept
{
	spi_slave_transaction_t* Completed = nullptr;
	return spi_slave_get_trans_result(AsSpiHost(InHost), &Completed, 0) == ESP_OK;
}

/**
 * Frees the SPI slave bus opened by `OpenConfiguredSpiSlave`.
 *
 * The return value is ignored because the driver is already going inert and there is no recovery action at
 * this layer.
 *
 * @param InHost SPI host number to free.
 */
inline void CloseSpiSlave(const int InHost) noexcept
{
	(void)spi_slave_free(AsSpiHost(InHost));
}

} // namespace MicroWorld::Detail
