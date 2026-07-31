#pragma once

// =============================================================================
// src/I2cPlatformImplementation.h is the SOLE header that pulls ESP-IDF I2C headers.
// It is included by one device translation unit — Esp32I2cDevice.cpp (the wired
// point-to-point I2C master/slave link pair) — and a public header must never reach
// it. Every ESP-IDF I2C divergence is hidden behind the helpers below so both device
// classes read one platform-free send/receive path that mirrors the UART device. Example
// 20's master-clocked ping-pong runtime-verifies this path on ESP32-S3 (2026-07-23):
// i2c_master_transmit, i2c_master_receive, i2c_slave_write's staged reply, and the
// on_receive ISR callback all round-trip, and the NACK/timeout -> transient-full mapping
// was observed when a peer was absent. The i2c_slave_write partial-write discard stays
// unexercised (frames fit the ring). Verified behavior needs external pull-ups and a
// clean co-start: I2C is open-drain, so resetting one board mid-transaction can latch the
// bus low until both restart. The on_receive callback and the byte push it calls run in
// ISR context and assume CONFIG_I2C_ISR_IRAM_SAFE stays disabled (the default); enabling
// it would require placing both in IRAM. See ../AGENTS.md for the rule this
// comment satisfies.
// =============================================================================

#include <MicroWorld/Platform/Esp32/Esp32I2cDevice.h>

#include <driver/i2c_master.h>
#include <driver/i2c_slave.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Bounds one blocking bus transaction before it is reported transiently full (100 kHz clocks one whole frame in ~13 ms). */
constexpr int I2cTransactionTimeoutMs = 50;

/** Motivation: Sizes the slave's ESP-IDF send and receive buffers well above one whole frame so a frame never splits across transactions. */
constexpr std::uint32_t I2cSlaveBufferDepth = 256;

/**
 * Motivation: Gives the device one vocabulary for an I2C send attempt that is free of ESP-IDF error codes.
 * Responsibilities: Distinguish accepted, transiently-blocked, and failed send outcomes.
 * Example:
 *   if (WriteI2cMaster(Dev, Frame, Len) == EI2cWriteOutcome::WouldBlock) { Retry(); }
 */
enum class EI2cWriteOutcome : std::uint8_t
{
	Sent,		///< Motivation: The whole frame was accepted.
	WouldBlock, ///< Motivation: The frame was not accepted right now; treat as a transient full condition.
	Error,		///< Motivation: Any other I2C error.
};

/**
 * Motivation: Gives the device one vocabulary for a bounded I2C master read that is free of ESP-IDF error codes.
 * Responsibilities: Distinguish a filled window, a transiently-blocked bus, and a hard error.
 * Example:
 *   if (ReadI2cMaster(Dev, Win, Len) == EI2cReadOutcome::Read) { Drain(Win); }
 */
enum class EI2cReadOutcome : std::uint8_t
{
	Read,		///< Motivation: The window buffer was filled by the read.
	WouldBlock, ///< Motivation: The bus was busy or the slave did not respond; no bytes are available.
	Error,		///< Motivation: Any other I2C error.
};

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
 * Motivation: Writes one complete framed message to the slave in a single bus transaction behind a normalized outcome.
 * Responsibilities: Map a NACK or timeout to WouldBlock so an unready peer reads as transiently full, and any other
 *   error to Error.
 */
inline EI2cWriteOutcome WriteI2cMaster(
	const i2c_master_dev_handle_t InDevice, const std::uint8_t* const InFrameBytes, const std::size_t InLength) noexcept
{
	const esp_err_t Result = i2c_master_transmit(InDevice, InFrameBytes, InLength, I2cTransactionTimeoutMs);
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
 * Motivation: Clocks one whole-frame window of bytes out of the slave in a single read transaction behind a
 *   normalized outcome.
 * Responsibilities: Fill the whole window (with filler where the slave had nothing queued) on a well-formed bus, and
 *   map a busy bus or unresponsive slave to WouldBlock.
 */
inline EI2cReadOutcome ReadI2cMaster(const i2c_master_dev_handle_t InDevice, std::uint8_t* const OutWindowBytes, const std::size_t InLength) noexcept
{
	const esp_err_t Result = i2c_master_receive(InDevice, OutWindowBytes, InLength, I2cTransactionTimeoutMs);
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
 * Motivation: Stages one complete framed message for the master's next read behind a normalized outcome.
 * Responsibilities: Write with a zero timeout so the call never blocks; on a full write report Sent, and otherwise
 *   discard any partial bytes with i2c_slave_reset_tx_fifo (so a half-frame never reaches the master) and report
 *   WouldBlock for a transient full ring or Error otherwise.
 */
inline EI2cWriteOutcome WriteI2cSlave(
	const i2c_slave_dev_handle_t InDevice, const std::uint8_t* const InFrameBytes, const std::size_t InLength) noexcept
{
	std::uint32_t Written = 0;
	const esp_err_t Result = i2c_slave_write(InDevice, InFrameBytes, static_cast<std::uint32_t>(InLength), &Written, 0);
	if (Result == ESP_OK && static_cast<std::size_t>(Written) == InLength)
	{
		return EI2cWriteOutcome::Sent;
	}
	// Discard any partial bytes so a half-frame never corrupts the master's read.
	(void)i2c_slave_reset_tx_fifo(InDevice);
	if (Result == ESP_OK || Result == ESP_ERR_TIMEOUT)
	{
		return EI2cWriteOutcome::WouldBlock;
	}
	return EI2cWriteOutcome::Error;
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
