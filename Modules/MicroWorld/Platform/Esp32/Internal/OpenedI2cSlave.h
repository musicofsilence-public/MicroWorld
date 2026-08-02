#pragma once

#include <MicroWorld/Platform/Esp32/I2cReceiveInbox.h>

#include <driver/i2c_slave.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Sizes the slave's ESP-IDF send and receive buffers well above one whole frame so a frame never splits across transactions. */
constexpr std::uint32_t I2cSlaveBufferDepth = 256;

/**
 * Motivation: Reports whether opening one I2C slave device and registering its receive callback succeeded.
 * Responsibilities: Carry the slave device handle and the open flag.
 * Example:
 *   FOpenedI2cSlave Opened = OpenConfiguredI2cSlave(Port, Sda, Scl, Addr, Inbox);
 */
struct FOpenedI2cSlave
{
	/** Motivation: ESP-IDF slave device handle, valid only when bOpen is true. */
	i2c_slave_dev_handle_t Dev;
	/** Motivation: True when the device was created and its callback registered; false when construction rolled back. */
	bool bOpen;
};

/**
 * Motivation: Feeds bytes a master wrote into the owning slave device's inbox from ISR context so the receive pump
 *   can drain them later.
 * Responsibilities: Copy each received byte into the ring and return false (it wakes no task); stay short and
 *   allocation-free because it runs in ISR context.
 */
inline bool OnI2cSlaveReceiveFromIsr(i2c_slave_dev_handle_t, const i2c_slave_rx_done_event_data_t* InEventData, void* InUserData)
{
	FI2cReceiveInbox* const Inbox = static_cast<FI2cReceiveInbox*>(InUserData);
	if (Inbox == nullptr || InEventData == nullptr || InEventData->buffer == nullptr)
	{
		return false;
	}
	for (std::uint32_t Index = 0; Index < InEventData->length; ++Index)
	{
		Inbox->PushFromIsr(InEventData->buffer[Index]);
	}
	return false;
}

/**
 * Motivation: Creates the I2C slave device and registers the receive callback that fills the given inbox behind one helper.
 * Responsibilities: Listen on SlaveAddress with buffers sized to I2cSlaveBufferDepth and internal pull-ups enabled; on
 *   any failure delete the partially created device and return bOpen false so the caller can leave the device inert
 *   without throwing.
 */
inline FOpenedI2cSlave OpenConfiguredI2cSlave(
	const std::int32_t InPort,
	const std::int32_t InSdaGpio,
	const std::int32_t InSclGpio,
	const std::uint8_t InSlaveAddress,
	FI2cReceiveInbox& InInbox) noexcept
{
	i2c_slave_config_t SlaveConfig{};
	SlaveConfig.i2c_port = static_cast<i2c_port_num_t>(InPort);
	SlaveConfig.sda_io_num = static_cast<gpio_num_t>(InSdaGpio);
	SlaveConfig.scl_io_num = static_cast<gpio_num_t>(InSclGpio);
	SlaveConfig.clk_source = I2C_CLK_SRC_DEFAULT;
	SlaveConfig.send_buf_depth = I2cSlaveBufferDepth;
	SlaveConfig.receive_buf_depth = I2cSlaveBufferDepth;
	SlaveConfig.slave_addr = InSlaveAddress;
	SlaveConfig.addr_bit_len = I2C_ADDR_BIT_LEN_7;
	SlaveConfig.intr_priority = 0;
	SlaveConfig.flags.enable_internal_pullup = true;
	i2c_slave_dev_handle_t Device = nullptr;
	if (i2c_new_slave_device(&SlaveConfig, &Device) != ESP_OK)
	{
		return FOpenedI2cSlave{nullptr, false};
	}
	i2c_slave_event_callbacks_t Callbacks{};
	Callbacks.on_receive = OnI2cSlaveReceiveFromIsr;
	if (i2c_slave_register_event_callbacks(Device, &Callbacks, &InInbox) != ESP_OK)
	{
		(void)i2c_del_slave_device(Device);
		return FOpenedI2cSlave{nullptr, false};
	}
	return FOpenedI2cSlave{Device, true};
}

/**
 * Motivation: Tears down the slave device behind a safe helper so the device destructor needs no validity branch.
 * Responsibilities: No-op on a null handle and ignore the return value because the device is already inert.
 */
inline void CloseI2cSlave(const i2c_slave_dev_handle_t InDevice) noexcept
{
	if (InDevice != nullptr)
	{
		(void)i2c_del_slave_device(InDevice);
	}
}

} // namespace MicroWorld::Platform::Esp32
