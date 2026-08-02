#pragma once

#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Platform/Esp32/Esp32E32LoraConfig.h>
#include <MicroWorld/Platform/Esp32/Internal/Esp32UartByteStream.h>
#include <MicroWorld/Platform/Esp32/Internal/Esp32UartByteStreamConfig.h>
#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Gives PlatformEsp32 one header-defined compatibility facade over the portable E32 LoRa device so an
 *   application composes the radio without depending on the optional RadioE32 package unless it includes this header.
 * Responsibilities: Own ESP-IDF UART lifetime through the internal byte stream while delegating framing and bounded
 *   progress to FE32LoraDevice, and keep all methods inline so non-LoRa PlatformEsp32 consumers stay independent.
 * Example:
 *   FEsp32LoraDevice Radio(Config);
 *   if (Radio.IsOpen()) { Radio.PreAdvance(NowMilliseconds); }
 */
class FEsp32LoraDevice final : public Core::ITransportDevice
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

		const Core::ETransportResult InitializeResult = RadioDevice.Initialize(InConfig.LocalNodeId);
		if (InitializeResult != Core::ETransportResult::Success)
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
	 *   run the pre-advance turn regularly (TTransportHost already does so).
	 */
	Core::ETransportResult TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override
	{
		return RadioDevice.TrySend(InTo, InPacket);
	}

	/**
	 * Motivation: Pumps bounded UART input and transactionally delivers at most one decoded frame.
	 * Responsibilities: Delegate to FE32LoraDevice.TryReceive; every non-success result preserves caller outputs,
	 *   Full retains the decoded frame for a larger retry, and a UART failure maps to Invalid.
	 */
	Core::ETransportResult TryReceive(
		Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override
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
	 * Responsibilities: Advance the queued frame through bounded physical UART progress by forwarding the turn to the portable
	 *   radio device that owns the framing.
	 */
	void PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override { RadioDevice.PreAdvance(InNowMilliseconds); }

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
