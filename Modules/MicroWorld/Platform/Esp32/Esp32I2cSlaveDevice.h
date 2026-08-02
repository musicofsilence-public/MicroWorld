#pragma once

#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/TFrameDecoder.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Platform/Esp32/Esp32I2cMasterDevice.h>
#include <MicroWorld/Platform/Esp32/I2cReceiveInbox.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

struct FEsp32I2cSlaveConfig;

/**
 * Motivation: Gives the application entry point a non-blocking Core::ITransportDevice for the I2C slave side of a
 *   point-to-point link, mirroring the master interface above.
 * Responsibilities: Stage one framed packet per TrySend for the master's next read and drain an ISR-filled inbox
 *   per TryReceive through a bounded TFrameDecoder; validate every argument before any syscall and leave caller
 *   outputs unchanged on any non-Success result.
 * Example:
 *   FEsp32I2cSlaveDevice Slave(Config);
 *   if (Slave.IsOpen()) { Slave.TryReceive(From, Dest, Result); }
 */
class FEsp32I2cSlaveDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Opens the I2C slave device and registers its receive callback before any traffic flows.
	 * Responsibilities: Create the slave on the configured port/GPIOs/address and register the platform
	 *   on_receive callback that fills the inbox; on any failure roll back and leave IsOpen false; never throw.
	 */
	explicit FEsp32I2cSlaveDevice(const FEsp32I2cSlaveConfig& InConfig) noexcept;

	/**
	 * Motivation: Releases the slave device so construction-allocated ESP-IDF resources never leak.
	 * Responsibilities: Delete the slave device opened by construction.
	 */
	~FEsp32I2cSlaveDevice() noexcept override;

	/**
	 * Motivation: Keeps one device value owning exactly one slave identity so the device handle never aliases.
	 * Responsibilities: Reject copy construction so the slave stays the single owner of its device.
	 */
	FEsp32I2cSlaveDevice(const FEsp32I2cSlaveDevice&) = delete;

	/**
	 * Motivation: Keeps one device value owning exactly one slave identity so the device handle never aliases.
	 * Responsibilities: Reject copy assignment so the slave stays the single owner of its device.
	 */
	FEsp32I2cSlaveDevice& operator=(const FEsp32I2cSlaveDevice&) = delete;

	/**
	 * Motivation: Keeps the owned slave handle, inbox address, and interface identity fixed at one address for the
	 *   link's lifetime.
	 * Responsibilities: Reject move construction so the inbox address handed to the ISR callback never relocates.
	 */
	FEsp32I2cSlaveDevice(FEsp32I2cSlaveDevice&&) = delete;

	/**
	 * Motivation: Keeps the owned slave handle, inbox address, and interface identity fixed at one address for the
	 *   link's lifetime.
	 * Responsibilities: Reject move assignment so the inbox address handed to the ISR callback never relocates.
	 */
	FEsp32I2cSlaveDevice& operator=(FEsp32I2cSlaveDevice&&) = delete;

	/**
	 * Motivation: Stages one complete framed message for the master's next read, transactionally.
	 * Responsibilities: Return Invalid for a non-I2C destination, oversize packet, or null span with nonzero
	 *   length, Full (discarding any partial bytes so a half-frame never reaches the master) when the transmit
	 *   ring cannot take the whole frame, and Success only after the whole frame is queued.
	 */
	Core::ETransportResult TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Receives at most one framed message by draining the ISR-filled inbox, transactionally.
	 * Responsibilities: Pump buffered bytes through the decoder until a frame completes or the bounded pump drains,
	 *   and report Unavailable, Full (frame held for a larger retry), Invalid (null destination with nonzero
	 *   length), or Success after a complete frame copies payload, byte count, and sender node id into OutFrom;
	 *   leave outputs unchanged on any non-success result.
	 */
	Core::ETransportResult TryReceive(
		Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size a packet against the transport's capacity without a magic number.
	 * Responsibilities: Report the largest payload, in bytes, one send accepts, excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Records that I2C slave writes leave no device-local work for the next transport turn.
	 * Responsibilities: Do no work because TrySend writes each frame directly to the slave transmit ring.
	 */
	void PreAdvance(Core::TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Lets a caller gate every op on whether construction opened a usable slave device.
	 * Responsibilities: Report the open flag set at construction and never mutated afterward except by destruction.
	 */
	bool IsOpen() const noexcept;

private:
	/** Motivation: Bounded RX deframer held by value; its capacity matches I2cMaxPayloadBytes. */
	Transport::FrameCodec::TFrameDecoder<I2cMaxPayloadBytes> Decoder{};

	/** Motivation: Inbox the platform receive ISR fills and TryReceive drains; its address is passed to the callback. */
	FI2cReceiveInbox Inbox{};

	/** Motivation: ESP-IDF i2c_slave_dev_handle_t stored opaquely; reinterpreted only in the source file. */
	void* SlaveHandle{nullptr};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Motivation: Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Esp32
