#pragma once

#include <MicroWorld/Platform/Esp32/Internal/Esp32UartByteStream.h>
#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

using namespace ::MicroWorld::Transport;
using namespace ::MicroWorld::Transport::Address;
using namespace ::MicroWorld::Transport::Device;

/**
 * Construction parameters for one ESP32 E32 LoRa compatibility facade.
 *
 * UART port and pin values remain plain integers so this released public config stays free of ESP-IDF enum types;
 * the internal byte-stream adapter converts them only within PlatformEsp32 private implementation code.
 */
struct FEsp32E32LoraConfig
{
	/** UART port number (ESP-IDF `uart_port_t`, e.g. UART_NUM_1) passed as a plain integer. */
	std::int32_t UartPort{0};

	/** TX GPIO number wired to the E32 module's RX pin, passed as a plain integer. */
	std::int32_t TxGpio{0};

	/** RX GPIO number wired to the E32 module's TX pin, passed as a plain integer. */
	std::int32_t RxGpio{0};

	/** Baud rate shared with the E32 module's UART configuration (commonly 9600). */
	std::uint32_t BaudRate{9600};

	/** Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Header-defined ESP32 compatibility facade for the portable E32 LoRa `IDevice`.
 *
 * The facade owns ESP-IDF UART lifetime through its internal byte stream while `FE32LoraDevice` owns portable
 * framing and bounded progress.
 * Keeping all methods inline means PlatformEsp32 consumers resolve RadioE32 only when they include this E32 header; non-LoRa PlatformEsp32 consumers
 * remain independent of the optional package.
 */
class FEsp32LoraDevice final : public IDevice
{
public:
	/**
	 * Opens one exclusive ESP32 UART stream and initializes the portable E32 device.
	 *
	 * Configuration failure leaves the facade closed. If portable initialization fails after opening, the stream closes
	 * before construction completes so this facade never retains a usable UART without an initialized radio device.
	 *
	 * @param InConfig UART, GPIO, baud, and local node id parameters.
	 */
	explicit FEsp32LoraDevice(const FEsp32E32LoraConfig& InConfig) noexcept
	{
		FEsp32UartByteStreamConfig StreamConfig{};
		StreamConfig.UartPort = InConfig.UartPort;
		StreamConfig.TxGpio = InConfig.TxGpio;
		StreamConfig.RxGpio = InConfig.RxGpio;
		StreamConfig.BaudRate = InConfig.BaudRate;
		if (!ByteStream.Open(StreamConfig))
		{
			return;
		}

		const ETransportResult InitializeResult = RadioDevice.Initialize(InConfig.LocalNodeId);
		if (InitializeResult != ETransportResult::Success)
		{
			ByteStream.Close();
		}
	}

	/** Releases the internally owned UART stream through the byte-stream member destructor. */
	~FEsp32LoraDevice() noexcept override = default;

	/** Prevents copying so one facade value owns exactly one UART stream installation. */
	FEsp32LoraDevice(const FEsp32LoraDevice&) = delete;

	/** Prevents copying so UART lifecycle and queued-frame ownership remain unique. */
	FEsp32LoraDevice& operator=(const FEsp32LoraDevice&) = delete;

	/** Prevents moving so the internally referenced byte stream remains stable. */
	FEsp32LoraDevice(FEsp32LoraDevice&&) = delete;

	/** Prevents moving so the owned UART close responsibility cannot transfer between facade values. */
	FEsp32LoraDevice& operator=(FEsp32LoraDevice&&) = delete;

	/**
	 * Queues one complete framed packet for later bounded UART progress.
	 *
	 * `Success` means the facade accepted the complete encoded frame into its fixed slot, not that the frame was
	 * physically emitted. Direct callers must invoke `AdvanceTransmit` regularly; `TTransportHost` already does so after
	 * each outbound FIFO drain. Invalid address/span and capacity outcomes follow `FE32LoraDevice` unchanged.
	 *
	 * @param InTo Device-relative one-byte destination metadata; transparent mode does not route it on air.
	 * @param InPacket Caller-owned
	 * payload to frame and queue.
	 * @return Outcome of the portable frame-acceptance attempt.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override
	{
		return RadioDevice.TrySend(InTo, InPacket);
	}

	/**
	 * Pumps bounded UART input and transactionally delivers at most one decoded frame.
	 *
	 * Every non-success result preserves caller outputs; a `Full` destination retains the decoded frame for a larger
	 * retry, and a UART failure maps to `Invalid` under the portable device contract.
	 *
	 * @param OutFrom Filled with the sender's E32 address only on `Success`.
	 * @param InDestination Caller-owned destination for one decoded payload.
	 * @param OutResult Filled with the delivered byte count only on `Success`.
	 * @return `Success`, `Unavailable`, `Full`, or `Invalid` under `IDevice`.
	 */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
	{
		return RadioDevice.TryReceive(OutFrom, InDestination, OutResult);
	}

	/** Reports the portable E32 payload capacity, excluding framing overhead. */
	std::size_t MaxPacketBytes() const noexcept override { return RadioDevice.MaxPacketBytes(); }

	/** Advances the queued frame through bounded physical UART progress. */
	void AdvanceTransmit() noexcept override { RadioDevice.AdvanceTransmit(); }

	/** Reports whether construction opened the UART stream and completed portable radio initialization. */
	bool IsOpen() const noexcept { return ByteStream.IsOpen() && RadioDevice.IsInitialized(); }

private:
	/** Owns ESP-IDF UART configuration and lifetime while exposing only Core's non-blocking `Core::IUartByteStream` interface. */
	FEsp32UartByteStream ByteStream{};

	/** Owns portable E32 framing and retains a reference to ByteStream for its full facade lifetime. */
	FE32LoraDevice RadioDevice{ByteStream};
};

} // namespace MicroWorld::Platform::Esp32
