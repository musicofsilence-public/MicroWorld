#pragma once

#include <driver/i2c_master.h>
#include <esp_err.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Reports whether opening one I2C master bus and adding its slave device succeeded.
 * Responsibilities: Carry the bus handle, the device handle, and the open flag.
 * Example:
 *   FOpenedI2cMaster Opened = OpenConfiguredI2cMaster(Port, Sda, Scl, Speed, Addr);
 */
struct FOpenedI2cMaster
{
	/** Motivation: ESP-IDF master bus handle, valid only when bOpen is true. */
	i2c_master_bus_handle_t Bus;
	/** Motivation: ESP-IDF master device handle for the peer slave, valid only when bOpen is true. */
	i2c_master_dev_handle_t Dev;
	/** Motivation: True when both the bus and device were allocated; false when construction rolled back. */
	bool bOpen;
};

/**
 * Motivation: Allocates the I2C master bus and adds the peer slave as its only device behind one helper.
 * Responsibilities: Use 100 kHz standard mode with internal pull-ups enabled as insurance; on any failure delete the
 *   partially allocated bus and return bOpen false so the caller can leave the device inert without throwing.
 */
inline FOpenedI2cMaster OpenConfiguredI2cMaster(
	const std::int32_t InPort,
	const std::int32_t InSdaGpio,
	const std::int32_t InSclGpio,
	const std::uint32_t InSclSpeedHz,
	const std::uint8_t InSlaveAddress) noexcept
{
	i2c_master_bus_config_t BusConfig{};
	BusConfig.i2c_port = static_cast<i2c_port_num_t>(InPort);
	BusConfig.sda_io_num = static_cast<gpio_num_t>(InSdaGpio);
	BusConfig.scl_io_num = static_cast<gpio_num_t>(InSclGpio);
	BusConfig.clk_source = I2C_CLK_SRC_DEFAULT;
	BusConfig.glitch_ignore_cnt = 7;
	BusConfig.intr_priority = 0;
	BusConfig.trans_queue_depth = 0; // synchronous transactions only
	BusConfig.flags.enable_internal_pullup = true;
	i2c_master_bus_handle_t Bus = nullptr;
	if (i2c_new_master_bus(&BusConfig, &Bus) != ESP_OK)
	{
		return FOpenedI2cMaster{nullptr, nullptr, false};
	}
	i2c_device_config_t DeviceConfig{};
	DeviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
	DeviceConfig.device_address = InSlaveAddress;
	DeviceConfig.scl_speed_hz = InSclSpeedHz;
	DeviceConfig.scl_wait_us = 0;
	DeviceConfig.flags.disable_ack_check = false;
	i2c_master_dev_handle_t Device = nullptr;
	if (i2c_master_bus_add_device(Bus, &DeviceConfig, &Device) != ESP_OK)
	{
		(void)i2c_del_master_bus(Bus);
		return FOpenedI2cMaster{nullptr, nullptr, false};
	}
	return FOpenedI2cMaster{Bus, Device, true};
}

/**
 * Motivation: Tears down the master bus and device behind a safe helper so the device destructor needs no validity branch.
 * Responsibilities: No-op each step on a null handle and ignore the return values because the device is already inert.
 */
inline void CloseI2cMaster(const i2c_master_bus_handle_t InBus, const i2c_master_dev_handle_t InDevice) noexcept
{
	if (InDevice != nullptr)
	{
		(void)i2c_master_bus_rm_device(InDevice);
	}
	if (InBus != nullptr)
	{
		(void)i2c_del_master_bus(InBus);
	}
}

} // namespace MicroWorld::Platform::Esp32
