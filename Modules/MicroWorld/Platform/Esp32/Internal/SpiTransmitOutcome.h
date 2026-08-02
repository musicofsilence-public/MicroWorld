#pragma once

#include <driver/spi_master.h>
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

} // namespace MicroWorld::Platform::Esp32
