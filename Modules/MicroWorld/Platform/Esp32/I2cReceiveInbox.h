#pragma once

#include <MicroWorld/Platform/Esp32/Esp32I2cMasterDevice.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Gives the I2C slave device one bounded byte inbox that decouples the ISR context pushing received
 *   bytes from the receive pump draining them, so the on_receive callback never blocks the bus.
 * Responsibilities: Act as a single-producer/single-consumer ring that drops a byte (never blocks) when full and
 *   keeps its read and write indices volatile so the ISR producer and pump consumer race safely.
 * Example:
 *   FI2cReceiveInbox Inbox;
 *   Inbox.PushFromIsr(Byte);
 *   if (Inbox.Pop(Out)) { Pump(Out); }
 */
class FI2cReceiveInbox
{
public:
	/**
	 * Motivation: Lets the receive ISR enqueue one byte without ever blocking the bus.
	 * Responsibilities: Drop the byte when the ring is full and advance only the write index.
	 */
	void PushFromIsr(std::uint8_t InByte) noexcept;

	/**
	 * Motivation: Lets the receive pump harvest one buffered byte without racing the ISR.
	 * Responsibilities: Return false and leave OutByte untouched when the ring is empty; otherwise copy and advance.
	 */
	bool Pop(std::uint8_t& OutByte) noexcept;

private:
	/** Motivation: Bounds the ring at two whole transaction windows so a full window plus a second arriving mid-pump both fit. */
	static constexpr std::uint32_t Capacity = 2u * static_cast<std::uint32_t>(I2cTransactionWindowBytes);

	/** Motivation: Backing byte storage whose read and write positions wrap modulo Capacity. */
	std::uint8_t Bytes[Capacity]{};

	/** Motivation: Next write position, advanced only by the ISR producer. */
	volatile std::uint32_t WriteIndex{0};

	/** Motivation: Next read position, advanced only by the pump consumer. */
	volatile std::uint32_t ReadIndex{0};
};

} // namespace MicroWorld::Platform::Esp32
