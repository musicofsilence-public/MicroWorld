#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformPico/Detail/E32LoraTransportState.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Describes one RP2040 UART connection to an E32 module without exposing Pico SDK types.
 *
 * The composition root must give the driver exclusive ownership of the selected UART; sharing it with another driver
 * or UART-backed stdio is unsupported.
 */
struct FPicoE32LoraConfig
{
	/** UART hardware index: `0` for UART0 or `1` for UART1. */
	std::uint8_t UartIndex{0};

	/** RP2040 GPIO routed from UART TX to the E32 RXD pin. */
	unsigned int TxGpio{0};

	/** RP2040 GPIO routed from the E32 TXD pin to UART RX. */
	unsigned int RxGpio{1};

	/** Exact UART baud rate configured on the E32; zero is invalid. */
	std::uint32_t BaudRate{0};

	/** Source node id stamped into every outgoing MicroWorld frame. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Fixed-capacity, non-blocking E32 LoRa `INetDriver` for the native RP2040 Pico SDK.
 *
 * Construction is inert so static storage is safe before `main`; `Initialize` validates and opens one exclusive UART.
 * `TrySend(Success)` accepts one complete frame into the driver-owned slot for regular `AdvanceTransmit` calls, while
 * transparent-mode destination addresses remain shape-checked metadata rather than on-air routing.
 */
class FPicoE32LoraDriver final : public INetDriver
{
public:
	/** Creates a closed driver without touching Pico SDK state. */
	FPicoE32LoraDriver() noexcept = default;

	/** Deinitializes the exclusively owned UART when initialization succeeded. */
	~FPicoE32LoraDriver() noexcept override;

	/** Prevents copying so one value owns exactly one UART identity and transport state. */
	FPicoE32LoraDriver(const FPicoE32LoraDriver&) = delete;

	/** Prevents copying so one value owns exactly one UART identity and transport state. */
	FPicoE32LoraDriver& operator=(const FPicoE32LoraDriver&) = delete;

	/** Prevents moving so the UART identity and fixed transport storage remain stable. */
	FPicoE32LoraDriver(FPicoE32LoraDriver&&) = delete;

	/** Prevents moving so the UART identity and fixed transport storage remain stable. */
	FPicoE32LoraDriver& operator=(FPicoE32LoraDriver&&) = delete;

	/**
	 * Validates and configures one exclusive RP2040 UART for E32 traffic.
	 *
	 * Returns `Unavailable` when already open, `Invalid` for an unsupported index/pin mapping, zero baud, or a baud
	 * the SDK cannot produce exactly, and `Success` after configuring 8N1 with FIFO enabled and no flow control.
	 *
	 * @param InConfig UART identity, GPIO routing, baud rate, and local node id.
	 * @return Outcome of the initialization attempt.
	 */
	ENetResult Initialize(const FPicoE32LoraConfig& InConfig) noexcept;

	/**
	 * Transactionally accepts one complete packet into the fixed transmit slot.
	 *
	 * Returns `Unavailable` while closed, `Invalid` for a malformed address/span or oversize packet, `Full` while a
	 * prior frame remains queued, and `Success` once this driver owns the complete encoded frame.
	 *
	 * @param InTo Driver-relative one-byte destination metadata; transparent mode does not route it on air.
	 * @param InPacket Payload to frame and queue.
	 * @return Outcome of the acceptance attempt.
	 */
	ENetResult TrySend(const FNetAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Pumps a bounded number of UART bytes and transactionally delivers at most one frame.
	 *
	 * Every non-success result preserves the destination, sender address, and result. A `Full` result retains the
	 * decoded frame for a later retry with a larger destination.
	 *
	 * @param OutFrom Filled with the sender's E32 address only on `Success`.
	 * @param InDestination Destination for one decoded payload.
	 * @param OutResult Filled with the delivered byte count only on `Success`.
	 * @return `Success`, `Unavailable`, `Full`, or `Invalid` under the shared `INetDriver` contract.
	 */
	ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept override;

	/** Reports the shared E32 payload capacity, excluding framing overhead. */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Advances at most one queued byte when the UART is writable; otherwise performs no work. */
	void AdvanceTransmit() noexcept;

	/** Reports whether `Initialize` opened a usable UART. */
	bool IsOpen() const noexcept;

private:
	/** Pumps available UART bytes within one fixed budget and delivers the first completed frame. */
	ENetResult PumpReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> InDestination, FNetReceiveResult& OutResult) noexcept;

	/** Owns the SDK-free transmit slot and receive decoder exercised by host tests. */
	Detail::FE32LoraTransportState TransportState{};

	/** Stores the initialized UART index so private source code can resolve the SDK instance. */
	std::uint8_t UartIndexValue{0};

	/** Stamps each queued frame with this Pico's source node id. */
	std::uint8_t LocalNodeIdValue{0};

	/** Prevents SDK access before successful initialization and double initialization. */
	bool bOpen{false};
};

} // namespace MicroWorld
