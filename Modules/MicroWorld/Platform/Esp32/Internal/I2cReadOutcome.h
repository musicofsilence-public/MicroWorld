#pragma once

#include <MicroWorld/Platform/Esp32/Internal/I2cWriteOutcome.h>

#include <driver/i2c_master.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

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

} // namespace MicroWorld::Platform::Esp32
