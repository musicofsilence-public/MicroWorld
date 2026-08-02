#pragma once

#include <MicroWorld/Platform/Esp32/Esp32SpiMasterDevice.h>
#include <MicroWorld/Platform/Esp32/Internal/OpenedSpiMaster.h>

#include <driver/spi_common.h>
#include <driver/spi_slave.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

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
