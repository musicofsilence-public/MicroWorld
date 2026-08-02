#pragma once

// =============================================================================
// These internal headers confine every ESP-IDF SPI include to the Esp32 platform
// internals, never reaching a public header. They are included by one device
// translation unit — Esp32SpiDevice.cpp (the wired point-to-point SPI master/slave
// link pair). Every ESP-IDF SPI divergence is hidden behind the helpers below so both
// device classes read one platform-free path that mirrors the UART and I2C devices.
// 21's master-clocked ping-pong runtime-verifies this path on ESP32-S3 (2026-07-23):
// SPI is full-duplex, so every master transaction both sends and receives (the master
// device feeds the received window to its decoder rather than discarding it — confirmed
// working); the slave is queue-based, so the device keeps one persistent transaction
// descriptor queued and the FrameCodec CRC rejects any garbage a momentary empty-queue
// gap produces. Error/timeout branches stayed unexercised (every exchange succeeded).
// DMA is used (SPI_DMA_CH_AUTO), so the device's transmit/receive buffers must live in
// internal RAM — the example entry point makes each device static, as the ESP32-S3
// main-task stack lesson in examples/AGENTS.md already requires. See ../AGENTS.md for
// the rule this comment satisfies.
// =============================================================================

#include <MicroWorld/Platform/Esp32/Esp32SpiMasterDevice.h>

#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_err.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

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

} // namespace MicroWorld::Platform::Esp32
