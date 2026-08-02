#pragma once

#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/TFrameDecoder.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Sizes one wired I2C frame payload to match UartMaxPayloadBytes so every wired transport carries the same message size, while the slave
 * buffers hold a whole frame plus headroom so a frame never splits across transactions. */
constexpr std::size_t I2cMaxPayloadBytes = 120;

/** Motivation: Names the largest single frame (payload plus framing) the master reads or the slave stages in one I2C transfer. */
constexpr std::size_t I2cTransactionWindowBytes = I2cMaxPayloadBytes + Transport::FrameCodec::FrameOverheadBytes;

struct FEsp32I2cMasterConfig;

/**
 * Motivation: Gives the application entry point a non-blocking Core::ITransportDevice for the I2C master side of a
 *   point-to-point link.
 * Responsibilities: Clock one bus transaction per send and one whole-frame read window per receive; validate every
 *   argument before any syscall, leave caller outputs unchanged on any non-Success result, and never split a frame.
 * Example:
 *   FEsp32I2cMasterDevice Master(Config);
 *   if (Master.IsOpen()) { Master.TrySend(To, Packet); }
 */
class FEsp32I2cMasterDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Opens the I2C master bus and registers the peer slave as its device before any traffic flows.
	 * Responsibilities: Allocate the bus and device on the configured port/GPIOs/speed; on any failure roll back
	 *   what was allocated and leave IsOpen false; never throw.
	 */
	explicit FEsp32I2cMasterDevice(const FEsp32I2cMasterConfig& InConfig) noexcept;

	/**
	 * Motivation: Releases the master bus and device so construction-allocated ESP-IDF resources never leak.
	 * Responsibilities: Remove the device and delete the master bus opened by construction.
	 */
	~FEsp32I2cMasterDevice() noexcept override;

	/**
	 * Motivation: Keeps one device value owning exactly one bus identity so the bus handle never aliases.
	 * Responsibilities: Reject copy construction so the master stays the single owner of its bus.
	 */
	FEsp32I2cMasterDevice(const FEsp32I2cMasterDevice&) = delete;

	/**
	 * Motivation: Keeps one device value owning exactly one bus identity so the bus handle never aliases.
	 * Responsibilities: Reject copy assignment so the master stays the single owner of its bus.
	 */
	FEsp32I2cMasterDevice& operator=(const FEsp32I2cMasterDevice&) = delete;

	/**
	 * Motivation: Keeps the owned bus handles and interface identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move construction so the opaque ESP-IDF handles never relocate.
	 */
	FEsp32I2cMasterDevice(FEsp32I2cMasterDevice&&) = delete;

	/**
	 * Motivation: Keeps the owned bus handles and interface identity fixed at one address for the link's lifetime.
	 * Responsibilities: Reject move assignment so the opaque ESP-IDF handles never relocate.
	 */
	FEsp32I2cMasterDevice& operator=(FEsp32I2cMasterDevice&&) = delete;

	/**
	 * Motivation: Sends one complete framed message to the slave in a single bus write, transactionally.
	 * Responsibilities: Return Invalid for a non-I2C destination, oversize packet, or null span with nonzero
	 *   length, Full when the slave does not acknowledge or the bus is busy, and Success only after the whole
	 *   frame is clocked out; leave bus state unchanged on any non-success result.
	 */
	Core::ETransportResult TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Receives at most one framed message by clocking one whole-frame window from the slave, transactionally.
	 * Responsibilities: Pump the read window through the decoder and report Unavailable (filler discarded), Full
	 *   (frame held for a larger retry), Invalid (null destination with nonzero length), or Success after a
	 *   complete frame copies payload, byte count, and sender node id into OutFrom; leave outputs unchanged on
	 *   any non-success result.
	 */
	Core::ETransportResult TryReceive(
		Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size a packet against the transport's capacity without a magic number.
	 * Responsibilities: Report the largest payload, in bytes, one send accepts, excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Records that synchronous I2C master sends leave no deferred transport work for this turn.
	 * Responsibilities: Do no work because TrySend clocks each frame through one bus write.
	 */
	void PreAdvance(Core::TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Lets a caller gate every op on whether construction opened a usable bus.
	 * Responsibilities: Report the open flag set at construction and never mutated afterward except by destruction.
	 */
	bool IsOpen() const noexcept;

private:
	/** Motivation: Bounded RX deframer held by value; its capacity matches I2cMaxPayloadBytes. */
	Transport::FrameCodec::TFrameDecoder<I2cMaxPayloadBytes> Decoder{};

	/** Motivation: ESP-IDF i2c_master_bus_handle_t stored opaquely; reinterpreted only in the source file. */
	void* BusHandle{nullptr};

	/** Motivation: ESP-IDF i2c_master_dev_handle_t stored opaquely; reinterpreted only in the source file. */
	void* DeviceHandle{nullptr};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Motivation: Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Esp32
