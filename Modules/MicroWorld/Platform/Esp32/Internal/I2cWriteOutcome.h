#pragma once

// =============================================================================
// These internal headers confine ESP-IDF I2C includes to the Esp32 platform
// internals, never reaching a public header. They are included by one device
// translation unit — Esp32I2cDevice.cpp (the wired point-to-point I2C master/slave
// link pair) — and a public header must never reach them. Every ESP-IDF I2C
// divergence is hidden behind the helpers below so both device classes read one
// platform-free send/receive path that mirrors the UART device. Example 20's
// master-clocked ping-pong runtime-verifies this path on ESP32-S3 (2026-07-23):
// i2c_master_transmit, i2c_master_receive, i2c_slave_write's staged reply, and the
// on_receive ISR callback all round-trip, and the NACK/timeout -> transient-full
// mapping was observed when a peer was absent. The i2c_slave_write partial-write
// discard stays unexercised (frames fit the ring). Verified behavior needs external
// pull-ups and a clean co-start: I2C is open-drain, so resetting one board
// mid-transaction can latch the bus low until both restart. The on_receive callback
// and the byte push it calls run in ISR context and assume CONFIG_I2C_ISR_IRAM_SAFE
// stays disabled (the default); enabling it would require placing both in IRAM. See
// ../AGENTS.md for the rule this comment satisfies.
// =============================================================================

#include <driver/i2c_master.h>
#include <driver/i2c_slave.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Bounds one blocking bus transaction before it is reported transiently full (100 kHz clocks one whole frame in ~13 ms). */
constexpr int I2cTransactionTimeoutMs = 50;

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

} // namespace MicroWorld::Platform::Esp32
