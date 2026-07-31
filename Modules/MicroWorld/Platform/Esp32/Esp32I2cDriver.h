#pragma once

#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/I2cAddress.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Largest single-transmission payload one wired I2C frame carries.
 *
 * Kept equal to `UartMaxPayloadBytes` so every wired transport carries the same message size and only the
 * driver construction differs; the slave's ESP-IDF send/receive buffers are sized well above one whole frame
 * so a frame is never split across I2C transactions at the example's pacing.
 */
constexpr std::size_t I2cMaxPayloadBytes = 120;

/**
 * Bytes of one whole I2C transaction window: the largest single frame (payload plus framing) the master reads
 * or the slave stages in one transfer.
 */
constexpr std::size_t I2cTransactionWindowBytes = I2cMaxPayloadBytes + FrameOverheadBytes;

/**
 * Fixed-capacity byte inbox the I2C slave driver owns, filled by the platform receive ISR and drained by `TryReceive`.
 *
 * A single-producer/single-consumer ring: the ESP-IDF `on_receive` callback pushes bytes from ISR context
 * while the receive pump pops them, so the indices are `volatile` and a byte is dropped (not blocked) when the
 * ring is full — the frame codec's resync tolerates the loss exactly as a noisy radio link would.
 */
class FI2cReceiveInbox
{
public:
	/** Pushes one received byte from ISR context, dropping it when the ring is full so the ISR never blocks. */
	void PushFromIsr(std::uint8_t InByte) noexcept;

	/** Pops one buffered byte into `OutByte`, returning false when the ring is empty. */
	bool Pop(std::uint8_t& OutByte) noexcept;

private:
	/** Ring capacity in bytes: two whole transaction windows, so a full window plus a second arriving mid-pump both fit. */
	static constexpr std::uint32_t Capacity = 2u * static_cast<std::uint32_t>(I2cTransactionWindowBytes);

	/** Backing storage; read and write positions wrap modulo `Capacity`. */
	std::uint8_t Bytes[Capacity]{};

	/** Next write position, advanced only by the ISR producer. */
	volatile std::uint32_t WriteIndex{0};

	/** Next read position, advanced only by the pump consumer. */
	volatile std::uint32_t ReadIndex{0};
};

/**
 * Construction parameters for the wired I2C master driver.
 *
 * Holds plain-integer bus parameters so the public header stays free of the ESP-IDF I2C enum types; the
 * platform-implementation header reinterprets them on the ESP32 side. `SlaveAddress` is the 7-bit bus address
 * of the peer slave this master clocks.
 */
struct FEsp32I2cMasterConfig
{
	/** I2C port number (ESP-IDF `i2c_port_num_t`, e.g. I2C_NUM_0) passed as a plain integer. */
	std::int32_t I2cPort{0};

	/** SDA GPIO number shared with the slave's SDA pin, passed as a plain integer. */
	std::int32_t SdaGpio{0};

	/** SCL GPIO number shared with the slave's SCL pin, passed as a plain integer. */
	std::int32_t SclGpio{0};

	/** SCL clock frequency in hertz (100 kHz standard mode is reliable over short jumper wires). */
	std::uint32_t SclSpeedHz{100000};

	/** 7-bit bus address of the peer slave this master addresses. */
	std::uint8_t SlaveAddress{0x28};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Construction parameters for the wired I2C slave driver.
 *
 * Holds plain-integer bus parameters so the public header stays free of the ESP-IDF I2C enum types.
 * `SlaveAddress` is this board's own 7-bit bus address, the address the peer master clocks.
 */
struct FEsp32I2cSlaveConfig
{
	/** I2C port number (ESP-IDF `i2c_port_num_t`, e.g. I2C_NUM_0) passed as a plain integer. */
	std::int32_t I2cPort{0};

	/** SDA GPIO number shared with the master's SDA pin, passed as a plain integer. */
	std::int32_t SdaGpio{0};

	/** SCL GPIO number shared with the master's SCL pin, passed as a plain integer. */
	std::int32_t SclGpio{0};

	/** This board's own 7-bit bus address, the address the master clocks. */
	std::uint8_t SlaveAddress{0x28};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Non-blocking wired `IDevice` for the master side of a point-to-point I2C link.
 *
 * It clocks the bus: `TrySend` writes one framed packet in a single bus transaction and `TryReceive` reads one
 * whole-frame window and pumps it through a bounded `TFrameDecoder`, so a silent slave simply yields filler the
 * codec discards. It validates every argument before any syscall, leaves caller outputs unchanged on any
 * non-`Success` result, and exercises no bus traffic until example 20's hardware checkpoint passes (§1.2).
 */
class FEsp32I2cMasterDriver final : public IDevice
{
public:
	/**
	 * Opens the I2C master bus and adds the peer slave as its device.
	 *
	 * Allocates the bus at `I2cPort` on the given SDA/SCL GPIOs at `SclSpeedHz` and registers `SlaveAddress`
	 * as its device. On any failure the constructor rolls back what it allocated and leaves `IsOpen() == false`;
	 * it never throws. The local node id is stamped on every outgoing frame.
	 *
	 * @param InConfig Bus, GPIO, speed, slave-address, and local node id parameters.
	 */
	explicit FEsp32I2cMasterDriver(const FEsp32I2cMasterConfig& InConfig) noexcept;

	/** Removes the device and deletes the master bus opened by construction. */
	~FEsp32I2cMasterDriver() noexcept override;

	/** Prevents copying so one driver value owns exactly one bus identity. */
	FEsp32I2cMasterDriver(const FEsp32I2cMasterDriver&) = delete;

	/** Prevents copying so one driver value owns exactly one bus identity. */
	FEsp32I2cMasterDriver& operator=(const FEsp32I2cMasterDriver&) = delete;

	/** Prevents moving so the owned bus handles and interface identity stay fixed. */
	FEsp32I2cMasterDriver(FEsp32I2cMasterDriver&&) = delete;

	/** Prevents moving so the owned bus handles and interface identity stay fixed. */
	FEsp32I2cMasterDriver& operator=(FEsp32I2cMasterDriver&&) = delete;

	/**
	 * Sends one complete framed message to the slave in a single bus write, transactionally.
	 *
	 * Returns `Invalid` for a destination that is not an I2C encoding, an oversize packet, or a null span with
	 * nonzero length; `Full` when the slave does not acknowledge or the bus is busy; and `Success` only when the
	 * whole frame was clocked out. A non-success result leaves the bus state unchanged.
	 *
	 * @param InTo Destination whose single byte must be an I2C node id (validated; the wire is point-to-point).
	 * @param InPacket Caller-owned payload bytes framed and sent as one message.
	 * @return Normalized outcome of the single send attempt.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Receives at most one framed message by clocking one whole-frame window from the slave, transactionally.
	 *
	 * Reads one bounded window and pumps its bytes through the decoder; `Unavailable` when the window holds no
	 * frame (the filler a silent slave returns is discarded), `Full` when the held frame exceeds the destination
	 * (kept held for a larger retry), `Invalid` for a null destination with nonzero length, and `Success` after a
	 * complete frame copies its payload, byte count, and sender node id into `OutFrom`.
	 *
	 * @param OutFrom Filled with the sender's I2C address only on `Success`.
	 * @param InDestination Caller-owned buffer for the received payload bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override;

	/** Reports the largest payload, in bytes, one send accepts (excludes framing overhead). */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable I2C master bus. */
	bool IsOpen() const noexcept;

private:
	/** Bounded RX deframer held by value; its capacity matches `I2cMaxPayloadBytes`. */
	TFrameDecoder<I2cMaxPayloadBytes> Decoder{};

	/** ESP-IDF `i2c_master_bus_handle_t` stored opaquely; reinterpreted only in the source file. */
	void* BusHandle{nullptr};

	/** ESP-IDF `i2c_master_dev_handle_t` stored opaquely; reinterpreted only in the source file. */
	void* DeviceHandle{nullptr};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

/**
 * Non-blocking wired `IDevice` for the slave side of a point-to-point I2C link.
 *
 * The master clocks the bus, so `TrySend` stages one framed packet for the master's next read and `TryReceive`
 * drains an ISR-filled inbox through a bounded `TFrameDecoder`; it is the master driver's mirror above the `IDevice` interface.
 * It validates every argument before any syscall, leaves caller outputs unchanged on any non-`Success` result,
 * and exercises no bus traffic until example 20's hardware checkpoint passes (§1.2).
 */
class FEsp32I2cSlaveDriver final : public IDevice
{
public:
	/**
	 * Opens the I2C slave device and registers its receive callback.
	 *
	 * Creates the slave at `I2cPort` on the given SDA/SCL GPIOs listening on `SlaveAddress`, and registers the
	 * platform `on_receive` callback that fills this driver's inbox. On any failure the constructor rolls back and
	 * leaves `IsOpen() == false`; it never throws. The local node id is stamped on every outgoing frame.
	 *
	 * @param InConfig Bus, GPIO, own-address, and local node id parameters.
	 */
	explicit FEsp32I2cSlaveDriver(const FEsp32I2cSlaveConfig& InConfig) noexcept;

	/** Deletes the slave device opened by construction. */
	~FEsp32I2cSlaveDriver() noexcept override;

	/** Prevents copying so one driver value owns exactly one slave identity. */
	FEsp32I2cSlaveDriver(const FEsp32I2cSlaveDriver&) = delete;

	/** Prevents copying so one driver value owns exactly one slave identity. */
	FEsp32I2cSlaveDriver& operator=(const FEsp32I2cSlaveDriver&) = delete;

	/** Prevents moving so the owned slave handle, inbox address, and interface identity stay fixed. */
	FEsp32I2cSlaveDriver(FEsp32I2cSlaveDriver&&) = delete;

	/** Prevents moving so the owned slave handle, inbox address, and interface identity stay fixed. */
	FEsp32I2cSlaveDriver& operator=(FEsp32I2cSlaveDriver&&) = delete;

	/**
	 * Stages one complete framed message for the master's next read, transactionally.
	 *
	 * Returns `Invalid` for a destination that is not an I2C encoding, an oversize packet, or a null span with
	 * nonzero length; `Full` when the transmit ring cannot take the whole frame (any partial bytes are discarded
	 * so a half-frame never reaches the master); and `Success` only when the whole frame was queued.
	 *
	 * @param InTo Destination whose single byte must be an I2C node id (validated; the wire is point-to-point).
	 * @param InPacket Caller-owned payload bytes framed and staged as one message.
	 * @return Normalized outcome of the single send attempt.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Receives at most one framed message by draining the ISR-filled inbox, transactionally.
	 *
	 * Pumps buffered bytes one at a time through the decoder until a frame completes or the bounded pump drains;
	 * `Unavailable` when no frame is ready, `Full` when the held frame exceeds the destination (kept held for a
	 * larger retry), `Invalid` for a null destination with nonzero length, and `Success` after a complete frame
	 * copies its payload, byte count, and sender node id into `OutFrom`.
	 *
	 * @param OutFrom Filled with the sender's I2C address only on `Success`.
	 * @param InDestination Caller-owned buffer for the received payload bytes.
	 * @param OutResult Filled with the received byte count only on `Success`.
	 * @return Normalized outcome of the single receive attempt.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override;

	/** Reports the largest payload, in bytes, one send accepts (excludes framing overhead). */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports whether the constructor opened a usable I2C slave device. */
	bool IsOpen() const noexcept;

private:
	/** Bounded RX deframer held by value; its capacity matches `I2cMaxPayloadBytes`. */
	TFrameDecoder<I2cMaxPayloadBytes> Decoder{};

	/** Inbox the platform receive ISR fills and `TryReceive` drains; its address is passed to the callback. */
	FI2cReceiveInbox Inbox{};

	/** ESP-IDF `i2c_slave_dev_handle_t` stored opaquely; reinterpreted only in the source file. */
	void* SlaveHandle{nullptr};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeIdValue{0};

	/** Remains false when construction failed, so every op short-circuits safely. */
	bool bOpen{false};
};

} // namespace MicroWorld
