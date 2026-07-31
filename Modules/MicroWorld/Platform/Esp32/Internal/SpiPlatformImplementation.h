#pragma once

// =============================================================================
// src/SpiPlatformImplementation.h is the SOLE header that pulls ESP-IDF SPI headers.
// It is included by one device translation unit — Esp32SpiDevice.cpp (the wired
// point-to-point SPI master/slave link pair) — and a public header must never reach
// it. Every ESP-IDF SPI divergence is hidden behind the helpers below so both device
// classes read one platform-free path that mirrors the UART and I2C devices. Example
// 21's master-clocked ping-pong runtime-verifies this path on ESP32-S3 (2026-07-23):
// SPI is full-duplex, so every master transaction both sends and receives (the master
// device feeds the received window to its decoder rather than discarding it — confirmed
// working); the slave is queue-based, so the device keeps one persistent transaction
// descriptor queued and the FrameCodec CRC rejects any garbage a momentary empty-queue
// gap produces. Error/timeout branches stayed unexercised (every exchange succeeded).
// DMA is used (SPI_DMA_CH_AUTO), so the device's transmit/receive buffers must live in
// internal RAM — the example composition root makes each device static, as the ESP32-S3
// main-task stack lesson in examples/AGENTS.md already requires. See ../AGENTS.md for
// the rule this comment satisfies.
// =============================================================================

#include <MicroWorld/Platform/Esp32/Esp32SpiDevice.h>

#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <driver/spi_slave.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Gives the device one vocabulary for a full-duplex SPI master transaction that is free of ESP-IDF
 *   error codes.
 * Responsibilities: Distinguish a clocked-out window, a timeout, and a hard error.
 * Example:
 *   if (TransmitSpiMaster(Dev, Tx, Rx, Len) == ESpiTransmitOutcome::Sent) { Drain(Rx); }
 */
enum class ESpiTransmitOutcome : std::uint8_t
{
	Sent,		///< Motivation: The whole window was clocked out and in.
	WouldBlock, ///< Motivation: The transaction timed out; treat as a transient full condition.
	Error,		///< Motivation: Any other SPI error.
};

/**
 * Motivation: Reports whether initializing the SPI bus as a master and adding its device succeeded.
 * Responsibilities: Carry the master device handle, the host number, and the open flag.
 * Example:
 *   FOpenedSpiMaster Opened = OpenConfiguredSpiMaster(Host, Mosi, Miso, Sclk, Cs, Clock);
 */
struct FOpenedSpiMaster
{
	/** Motivation: ESP-IDF master device handle, valid only when bOpen is true. */
	spi_device_handle_t Device;
	/** Motivation: SPI host number the bus was initialized on. */
	int Host;
	/** Motivation: True when the bus and device were initialized; false when construction rolled back. */
	bool bOpen;
};

/**
 * Motivation: Reports whether initializing the SPI bus as a slave succeeded.
 * Responsibilities: Carry the host number and the open flag.
 * Example:
 *   FOpenedSpiSlave Opened = OpenConfiguredSpiSlave(Host, Mosi, Miso, Sclk, Cs);
 */
struct FOpenedSpiSlave
{
	/** Motivation: SPI host number the slave bus was initialized on. */
	int Host;
	/** Motivation: True when the slave bus initialized; false when construction rolled back. */
	bool bOpen;
};

/**
 * Motivation: Restores the ESP-IDF SPI host type from the opaque stored host number so the public header never
 *   carries the platform enum.
 * Responsibilities: Reinterpret one opaque host number to its ESP-IDF SPI host type.
 */
inline spi_host_device_t AsSpiHost(const int InHost) noexcept
{
	return static_cast<spi_host_device_t>(InHost);
}

/**
 * Motivation: Initializes the SPI bus as a master and adds the slave as its only device behind one helper.
 * Responsibilities: Use SPI mode 0 at ClockHz with an automatically selected DMA channel and a transfer size of one
 *   whole window; on any failure free the partially initialized bus and return bOpen false so the caller can leave
 *   the device inert without throwing.
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
 * Motivation: Runs one full-duplex master transaction behind a normalized outcome so the device never inspects
 *   platform codes.
 * Responsibilities: Clock the transmit window out and the receive window in, mapping a timeout to WouldBlock and
 *   any other error to Error.
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
 * Motivation: Tears down the master bus and device behind a safe helper so the device destructor needs no validity branch.
 * Responsibilities: No-op or ignore each step's return because the device is already inert and there is no recovery
 *   action at this layer.
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
 * Motivation: Initializes the SPI bus as a slave listening on the given pins behind one helper.
 * Responsibilities: Use SPI mode 0 with an automatically selected DMA channel and no speed (the master supplies the
 *   clock); on failure return bOpen false so the caller can leave the device inert without throwing.
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
 * Motivation: Queues one full-duplex slave transaction so the master's next clock finds a buffer.
 * Responsibilities: Point the caller-owned persistent descriptor at the transmit and receive windows and queue it
 *   without blocking; return whether the queue call succeeded.
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
 * Motivation: Harvests one completed slave transaction without blocking so the device can drain its receive window.
 * Responsibilities: Return true when a queued transaction has completed (its receive window now holds the master's
 *   bytes) and false when none is done yet.
 */
inline bool HarvestSpiSlave(const int InHost) noexcept
{
	spi_slave_transaction_t* Completed = nullptr;
	return spi_slave_get_trans_result(AsSpiHost(InHost), &Completed, 0) == ESP_OK;
}

/**
 * Motivation: Tears down the slave bus behind a safe helper so the device destructor needs no validity branch.
 * Responsibilities: Ignore the return value because the device is already inert and there is no recovery action at
 *   this layer.
 */
inline void CloseSpiSlave(const int InHost) noexcept
{
	(void)spi_slave_free(AsSpiHost(InHost));
}

} // namespace MicroWorld::Platform::Esp32
