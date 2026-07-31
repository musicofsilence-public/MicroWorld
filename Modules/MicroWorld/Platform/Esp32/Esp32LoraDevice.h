#pragma once

#include <MicroWorld/Platform/Esp32/Internal/Esp32UartByteStream.h>
#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer UART parameters one ESP32 E32 LoRa facade needs at construction so the
 *   released public config stays free of ESP-IDF enum types.
 * Responsibilities: Hold UART port, TX/RX GPIO, baud rate, and local node id as plain integers.
 * Example:
 *   FEsp32E32LoraConfig Config;
 *   Config.BaudRate = 9600;
 */
struct FEsp32E32LoraConfig
{
	/** Motivation: UART port number (ESP-IDF uart_port_t, e.g. UART_NUM_1) passed as a plain integer. */
	std::int32_t UartPort{0};

	/** Motivation: TX GPIO number wired to the E32 module's RX pin, passed as a plain integer. */
	std::int32_t TxGpio{0};

	/** Motivation: RX GPIO number wired to the E32 module's TX pin, passed as a plain integer. */
	std::int32_t RxGpio{0};

	/** Motivation: Baud rate shared with the E32 module's UART configuration (commonly 9600). */
	std::uint32_t BaudRate{9600};

	/** Motivation: Local node id stamped on every outgoing frame's source node id byte. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Motivation: Gives PlatformEsp32 one header-defined compatibility facade over the portable E32 LoRa IDevice so an
 *   application composes the radio without depending on the optional RadioE32 package unless it includes this header.
 * Responsibilities: Own ESP-IDF UART lifetime through the internal byte stream while delegating framing and bounded
 *   progress to FE32LoraDevice, and keep all methods inline so non-LoRa PlatformEsp32 consumers stay independent.
 * Example:
 *   FEsp32LoraDevice Radio(Config);
 *   if (Radio.IsOpen()) { Radio.AdvanceTransmit(); }
 */
class FEsp32LoraDevice final : public Transport::Device::IDevice
{
public:
	/**
	 * Motivation: Opens one exclusive ESP32 UART stream and initializes the portable E32 device so the facade is
	 *   ready for traffic on a successful return.
	 * Responsibilities: On any failure leave the facade closed; if portable initialization fails after the stream
	 *   opens, close the stream so the facade never retains a usable UART without an initialized radio.
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

		const Transport::ETransportResult InitializeResult = RadioDevice.Initialize(InConfig.LocalNodeId);
		if (InitializeResult != Transport::ETransportResult::Success)
		{
			ByteStream.Close();
		}
	}

	/**
	 * Motivation: Releases the internally owned UART stream through the byte-stream member destructor.
	 * Responsibilities: Default the destructor so the byte-stream member closes the UART exactly once.
	 */
	~FEsp32LoraDevice() noexcept override = default;

	/**
	 * Motivation: Keeps one facade value owning exactly one UART stream installation.
	 * Responsibilities: Reject copy construction so UART lifecycle and queued-frame ownership remain unique.
	 */
	FEsp32LoraDevice(const FEsp32LoraDevice&) = delete;

	/**
	 * Motivation: Keeps one facade value owning exactly one UART stream installation.
	 * Responsibilities: Reject copy assignment so UART lifecycle and queued-frame ownership remain unique.
	 */
	FEsp32LoraDevice& operator=(const FEsp32LoraDevice&) = delete;

	/**
	 * Motivation: Keeps the internally referenced byte stream stable for the facade's whole lifetime.
	 * Responsibilities: Reject move construction so the radio device's reference to ByteStream never dangles.
	 */
	FEsp32LoraDevice(FEsp32LoraDevice&&) = delete;

	/**
	 * Motivation: Keeps the owned UART close responsibility bound to one facade value.
	 * Responsibilities: Reject move assignment so the close responsibility cannot transfer between facade values.
	 */
	FEsp32LoraDevice& operator=(FEsp32LoraDevice&&) = delete;

	/**
	 * Motivation: Queues one complete framed packet for later bounded UART progress without blocking the caller.
	 * Responsibilities: Delegate to FE32LoraDevice.TrySend and forward its outcome unchanged; Success means the
	 *   complete encoded frame was accepted into the fixed slot, not physically emitted, so direct callers must
	 *   invoke AdvanceTransmit regularly (TTransportHost already does so).
	 */
	Transport::ETransportResult TrySend(const Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override
	{
		return RadioDevice.TrySend(InTo, InPacket);
	}

	/**
	 * Motivation: Pumps bounded UART input and transactionally delivers at most one decoded frame.
	 * Responsibilities: Delegate to FE32LoraDevice.TryReceive; every non-success result preserves caller outputs,
	 *   Full retains the decoded frame for a larger retry, and a UART failure maps to Invalid.
	 */
	Transport::ETransportResult TryReceive(
		Transport::Address::FDeviceAddress& OutFrom,
		Core::TSpan<std::uint8_t> InDestination,
		Transport::Device::FReceiveResult& OutResult) noexcept override
	{
		return RadioDevice.TryReceive(OutFrom, InDestination, OutResult);
	}

	/**
	 * Motivation: Lets a caller size a packet against the radio's capacity without a magic number.
	 * Responsibilities: Report the portable E32 payload capacity, excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override { return RadioDevice.MaxPacketBytes(); }

	/**
	 * Motivation: Drives the queued frame toward the wire in bounded steps so a caller never blocks on a full UART.
	 * Responsibilities: Advance the queued frame through bounded physical UART progress.
	 */
	void AdvanceTransmit() noexcept override { RadioDevice.AdvanceTransmit(); }

	/**
	 * Motivation: Lets a caller gate every op on whether construction produced a usable radio.
	 * Responsibilities: Report true only when the UART stream is open and portable radio initialization completed.
	 */
	bool IsOpen() const noexcept { return ByteStream.IsOpen() && RadioDevice.IsInitialized(); }

private:
	/** Motivation: Owns ESP-IDF UART configuration and lifetime while exposing only Core's non-blocking Core::IUartByteStream interface. */
	FEsp32UartByteStream ByteStream{};

	/** Motivation: Owns portable E32 framing and retains a reference to ByteStream for its full facade lifetime. */
	Transport::FE32LoraDevice RadioDevice{ByteStream};
};

} // namespace MicroWorld::Platform::Esp32
