#pragma once

#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Platform/Esp32/I2cAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/** Motivation: Sizes one wired I2C frame payload to match UartMaxPayloadBytes so every wired transport carries the same message size, while the slave
 * buffers hold a whole frame plus headroom so a frame never splits across transactions. */
constexpr std::size_t I2cMaxPayloadBytes = 120;

/** Motivation: Names the largest single frame (payload plus framing) the master reads or the slave stages in one I2C transfer. */
constexpr std::size_t I2cTransactionWindowBytes = I2cMaxPayloadBytes + Transport::FrameCodec::FrameOverheadBytes;

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

/**
 * Motivation: Carries the plain-integer bus parameters one wired I2C master needs at construction so the public
 *   header stays free of ESP-IDF enum types.
 * Responsibilities: Hold port, SDA/SCL GPIO, SCL speed, peer slave address, and local node id as plain integers.
 * Example:
 *   FEsp32I2cMasterConfig Config;
 *   Config.SlaveAddress = 0x28;
 */
struct FEsp32I2cMasterConfig
{
	/** Motivation: I2C port number (ESP-IDF i2c_port_num_t, e.g. I2C_NUM_0) passed as a plain integer. */
	std::int32_t I2cPort{0};

	/** Motivation: SDA GPIO number shared with the slave's SDA pin, passed as a plain integer. */
	std::int32_t SdaGpio{0};

	/** Motivation: SCL GPIO number shared with the slave's SCL pin, passed as a plain integer. */
	std::int32_t SclGpio{0};

	/** Motivation: SCL clock frequency in hertz (100 kHz standard mode is reliable over short jumper wires). */
	std::uint32_t SclSpeedHz{100000};

	/** Motivation: 7-bit bus address of the peer slave this master addresses. */
	std::uint8_t SlaveAddress{0x28};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Motivation: Carries the plain-integer bus parameters one wired I2C slave needs at construction so the public
 *   header stays free of ESP-IDF enum types.
 * Responsibilities: Hold port, SDA/SCL GPIO, this board's own 7-bit slave address, and local node id as plain integers.
 * Example:
 *   FEsp32I2cSlaveConfig Config;
 *   Config.SlaveAddress = 0x29;
 */
struct FEsp32I2cSlaveConfig
{
	/** Motivation: I2C port number (ESP-IDF i2c_port_num_t, e.g. I2C_NUM_0) passed as a plain integer. */
	std::int32_t I2cPort{0};

	/** Motivation: SDA GPIO number shared with the master's SDA pin, passed as a plain integer. */
	std::int32_t SdaGpio{0};

	/** Motivation: SCL GPIO number shared with the master's SCL pin, passed as a plain integer. */
	std::int32_t SclGpio{0};

	/** Motivation: This board's own 7-bit bus address, the address the master clocks. */
	std::uint8_t SlaveAddress{0x28};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

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
