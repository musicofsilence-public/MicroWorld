#pragma once

// =============================================================================
// src/I2cPlatformImplementation.h is the SOLE header that pulls ESP-IDF I2C headers.
// It is included by one driver translation unit — Esp32I2cDriver.cpp (the wired
// point-to-point I2C master/slave link pair) — and a public header must never reach
// it. Every ESP-IDF I2C divergence is hidden behind the helpers below so both driver
// classes read one platform-free send/receive path that mirrors the UART driver. This
// platform implementation is COMPILE-VERIFIED on ESP32-S3 but the runtime behavior of
// i2c_master_transmit/receive, of i2c_slave_write's partial-write path, and of the
// on_receive ISR callback are UNVERIFIED at runtime: the mapping below treats a NACK or
// timeout as a transient full condition, discards any partial slave write, and copies
// callback bytes into the driver's inbox. The on_receive callback and the byte push it
// calls run in ISR context and assume CONFIG_I2C_ISR_IRAM_SAFE stays disabled (the
// default); enabling it would require placing both in IRAM. No I2C traffic is exercised
// until example 20's hardware checkpoint passes (docs/WIRED_TRANSPORTS_ROADMAP.md §1.2).
// =============================================================================

#include <MicroWorld/PlatformEsp32/Esp32I2cDriver.h>

#include <driver/i2c_master.h>
#include <driver/i2c_slave.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Detail
{

/** Milliseconds one blocking bus transaction may take before it is reported transiently full (100 kHz clocks one whole frame in ~13 ms). */
constexpr int I2cTransactionTimeoutMs = 50;

/** Depth in bytes of the slave's ESP-IDF send and receive buffers, sized well above one whole frame so a frame never splits across transactions. */
constexpr std::uint32_t I2cSlaveBufferDepth = 256;

/** Normalized result of one non-blocking I2C send attempt (master transmit or slave stage). */
enum class EI2cWriteOutcome : std::uint8_t
{
	/** The whole frame was accepted. */
	Sent,
	/** The frame was not accepted right now; treat as a transient full condition. */
	WouldBlock,
	/** Any other I2C error. */
	Error,
};

/** Normalized result of one bounded I2C master read transaction. */
enum class EI2cReadOutcome : std::uint8_t
{
	/** The window buffer was filled by the read. */
	Read,
	/** The bus was busy or the slave did not respond; no bytes are available. */
	WouldBlock,
	/** Any other I2C error. */
	Error,
};

/** Result of opening one I2C master bus and adding its single slave device. */
struct FOpenedI2cMaster
{
	/** ESP-IDF master bus handle, valid only when `bOpen` is true. */
	i2c_master_bus_handle_t Bus;
	/** ESP-IDF master device handle for the peer slave, valid only when `bOpen` is true. */
	i2c_master_dev_handle_t Dev;
	/** True when both the bus and device were allocated; false when construction rolled back. */
	bool bOpen;
};

/** Result of opening one I2C slave device with its receive callback registered. */
struct FOpenedI2cSlave
{
	/** ESP-IDF slave device handle, valid only when `bOpen` is true. */
	i2c_slave_dev_handle_t Dev;
	/** True when the device was created and its callback registered; false when construction rolled back. */
	bool bOpen;
};

/**
 * Copies bytes a master wrote into the owning slave driver's inbox, from ISR context.
 *
 * Registered as the ESP-IDF `on_receive` callback with the inbox address as its user data; it copies each
 * received byte into the ring and returns false because it wakes no higher-priority task. It must stay short
 * and allocation-free because it runs in ISR context.
 *
 * @param EventData Driver-fed event carrying the received bytes and their count.
 * @param UserData The `FI2cReceiveInbox` address passed at registration.
 * @return Always false: this callback wakes no task.
 */
inline bool OnI2cSlaveReceiveFromIsr(i2c_slave_dev_handle_t, const i2c_slave_rx_done_event_data_t* EventData, void* UserData)
{
	FI2cReceiveInbox* const Inbox = static_cast<FI2cReceiveInbox*>(UserData);
	if (Inbox == nullptr || EventData == nullptr || EventData->buffer == nullptr)
	{
		return false;
	}
	for (std::uint32_t Index = 0; Index < EventData->length; ++Index)
	{
		Inbox->PushFromIsr(EventData->buffer[Index]);
	}
	return false;
}

/**
 * Allocates the I2C master bus and adds the peer slave as its only device.
 *
 * Uses 100 kHz standard mode with the default clock source and internal pull-ups enabled as insurance over the
 * mandatory external resistors. On any failure the partially allocated bus is deleted so the caller sees
 * `bOpen == false` and can leave the driver inert without throwing.
 *
 * @param Port I2C port number to open.
 * @param SdaGpio SDA GPIO number wired to the slave's SDA pin.
 * @param SclGpio SCL GPIO number wired to the slave's SCL pin.
 * @param SclSpeedHz SCL clock frequency in hertz.
 * @param SlaveAddress 7-bit bus address of the peer slave.
 * @return Opened-master descriptor reporting whether allocation succeeded.
 */
inline FOpenedI2cMaster OpenConfiguredI2cMaster(
	const std::int32_t Port,
	const std::int32_t SdaGpio,
	const std::int32_t SclGpio,
	const std::uint32_t SclSpeedHz,
	const std::uint8_t SlaveAddress) noexcept
{
	i2c_master_bus_config_t BusConfig{};
	BusConfig.i2c_port = static_cast<i2c_port_num_t>(Port);
	BusConfig.sda_io_num = static_cast<gpio_num_t>(SdaGpio);
	BusConfig.scl_io_num = static_cast<gpio_num_t>(SclGpio);
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
	DeviceConfig.device_address = SlaveAddress;
	DeviceConfig.scl_speed_hz = SclSpeedHz;
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
 * Writes one complete framed message to the slave in a single bus transaction.
 *
 * A NACK or timeout maps to `WouldBlock` so the driver can treat an unready peer as transiently full; any
 * other error maps to `Error`. The runtime behavior is UNVERIFIED (compile-only phase).
 *
 * @param Device Open I2C master device handle.
 * @param FrameBytes First byte of the framed message to send.
 * @param Length Number of bytes to send.
 * @return Normalized outcome of the single write attempt.
 */
inline EI2cWriteOutcome WriteI2cMaster(const i2c_master_dev_handle_t Device, const std::uint8_t* const FrameBytes, const std::size_t Length) noexcept
{
	const esp_err_t Result = i2c_master_transmit(Device, FrameBytes, Length, I2cTransactionTimeoutMs);
	if (Result == ESP_OK)
	{
		return EI2cWriteOutcome::Sent;
	}
	if (Result == ESP_ERR_INVALID_RESPONSE || Result == ESP_ERR_TIMEOUT)
	{
		return EI2cWriteOutcome::WouldBlock;
	}
	return EI2cWriteOutcome::Error;
}

/**
 * Clocks one whole-frame window of bytes out of the slave in a single read transaction.
 *
 * The master generates the clock, so a well-formed bus fills the whole window (with filler where the slave had
 * nothing queued); a busy bus or unresponsive slave maps to `WouldBlock`. The runtime behavior is UNVERIFIED
 * (compile-only phase).
 *
 * @param Device Open I2C master device handle.
 * @param WindowBytes Caller-owned buffer filled with the read window.
 * @param Length Number of bytes to read.
 * @return Normalized outcome of the single read attempt.
 */
inline EI2cReadOutcome ReadI2cMaster(const i2c_master_dev_handle_t Device, std::uint8_t* const WindowBytes, const std::size_t Length) noexcept
{
	const esp_err_t Result = i2c_master_receive(Device, WindowBytes, Length, I2cTransactionTimeoutMs);
	if (Result == ESP_OK)
	{
		return EI2cReadOutcome::Read;
	}
	if (Result == ESP_ERR_INVALID_RESPONSE || Result == ESP_ERR_TIMEOUT)
	{
		return EI2cReadOutcome::WouldBlock;
	}
	return EI2cReadOutcome::Error;
}

/**
 * Removes the device and deletes the master bus opened by `OpenConfiguredI2cMaster`.
 *
 * Each step is a safe no-op when its handle is null; the return values are ignored because the driver is
 * already going inert and there is no recovery action at this layer.
 *
 * @param Bus Master bus handle to delete.
 * @param Device Master device handle to remove.
 */
inline void CloseI2cMaster(const i2c_master_bus_handle_t Bus, const i2c_master_dev_handle_t Device) noexcept
{
	if (Device != nullptr)
	{
		(void)i2c_master_bus_rm_device(Device);
	}
	if (Bus != nullptr)
	{
		(void)i2c_del_master_bus(Bus);
	}
}

/**
 * Creates the I2C slave device and registers the receive callback that fills the given inbox.
 *
 * Listens on `SlaveAddress` with send and receive buffers sized to `I2cSlaveBufferDepth` and internal pull-ups
 * enabled as insurance. On any failure the partially created device is deleted so the caller sees
 * `bOpen == false` and can leave the driver inert without throwing.
 *
 * @param Port I2C port number to open.
 * @param SdaGpio SDA GPIO number wired to the master's SDA pin.
 * @param SclGpio SCL GPIO number wired to the master's SCL pin.
 * @param SlaveAddress This board's own 7-bit bus address.
 * @param Inbox Inbox the receive callback fills; its address is passed as the callback user data.
 * @return Opened-slave descriptor reporting whether creation succeeded.
 */
inline FOpenedI2cSlave OpenConfiguredI2cSlave(
	const std::int32_t Port,
	const std::int32_t SdaGpio,
	const std::int32_t SclGpio,
	const std::uint8_t SlaveAddress,
	FI2cReceiveInbox& Inbox) noexcept
{
	i2c_slave_config_t SlaveConfig{};
	SlaveConfig.i2c_port = static_cast<i2c_port_num_t>(Port);
	SlaveConfig.sda_io_num = static_cast<gpio_num_t>(SdaGpio);
	SlaveConfig.scl_io_num = static_cast<gpio_num_t>(SclGpio);
	SlaveConfig.clk_source = I2C_CLK_SRC_DEFAULT;
	SlaveConfig.send_buf_depth = I2cSlaveBufferDepth;
	SlaveConfig.receive_buf_depth = I2cSlaveBufferDepth;
	SlaveConfig.slave_addr = SlaveAddress;
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
	if (i2c_slave_register_event_callbacks(Device, &Callbacks, &Inbox) != ESP_OK)
	{
		(void)i2c_del_slave_device(Device);
		return FOpenedI2cSlave{nullptr, false};
	}
	return FOpenedI2cSlave{Device, true};
}

/**
 * Stages one complete framed message for the master's next read.
 *
 * Writes with a zero timeout so the call never blocks; a full write is `Sent`, and anything else discards any
 * partial bytes with `i2c_slave_reset_tx_fifo` (so a half-frame never reaches the master) and reports
 * `WouldBlock` for a transient full ring or `Error` otherwise. The runtime behavior is UNVERIFIED (compile-only
 * phase).
 *
 * @param Device Open I2C slave device handle.
 * @param FrameBytes First byte of the framed message to stage.
 * @param Length Number of bytes to stage.
 * @return Normalized outcome of the single stage attempt.
 */
inline EI2cWriteOutcome WriteI2cSlave(const i2c_slave_dev_handle_t Device, const std::uint8_t* const FrameBytes, const std::size_t Length) noexcept
{
	std::uint32_t Written = 0;
	const esp_err_t Result = i2c_slave_write(Device, FrameBytes, static_cast<std::uint32_t>(Length), &Written, 0);
	if (Result == ESP_OK && static_cast<std::size_t>(Written) == Length)
	{
		return EI2cWriteOutcome::Sent;
	}
	// Discard any partial bytes so a half-frame never corrupts the master's read.
	(void)i2c_slave_reset_tx_fifo(Device);
	if (Result == ESP_OK || Result == ESP_ERR_TIMEOUT)
	{
		return EI2cWriteOutcome::WouldBlock;
	}
	return EI2cWriteOutcome::Error;
}

/**
 * Deletes the slave device opened by `OpenConfiguredI2cSlave`.
 *
 * A safe no-op when the device is null; the return value is ignored because the driver is already going inert
 * and there is no recovery action at this layer.
 *
 * @param Device Slave device handle to delete.
 */
inline void CloseI2cSlave(const i2c_slave_dev_handle_t Device) noexcept
{
	if (Device != nullptr)
	{
		(void)i2c_del_slave_device(Device);
	}
}

} // namespace MicroWorld::Detail
