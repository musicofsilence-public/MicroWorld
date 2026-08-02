#pragma once

#include <MicroWorld/Platform/Pico/Internal/PicoE32LoraPlatform.h>
#include <MicroWorld/Platform/Pico/Internal/PicoUartByteStream.h>
#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Platform::Pico
{

/**
 * Motivation: Describes one RP2040 UART connection to an E32 module without leaking Pico SDK types into device callers.
 * Responsibilities: Hold UART index, TX/RX GPIO routing, baud rate, and local node id for the application entry point to pass
 *   into Initialize; the device takes the UART exclusively and never shares it.
 * Example:
 *   FPicoE32LoraConfig Config{0, 0, 1, 115200, 0x42};
 *   Device.Initialize(Config);
 */
struct FPicoE32LoraConfig
{
	/** Motivation: Selects the RP2040 UART hardware block: 0 for UART0 or 1 for UART1. */
	std::uint8_t UartIndex{0};

	/** Motivation: Names the RP2040 GPIO routed from UART TX to the E32 RXD pin. */
	unsigned int TxGpio{0};

	/** Motivation: Names the RP2040 GPIO routed from the E32 TXD pin to UART RX. */
	unsigned int RxGpio{1};

	/** Motivation: Pins the exact baud rate configured on the E32; zero is treated as invalid. */
	std::uint32_t BaudRate{0};

	/** Motivation: Carries the source node id stamped into every outgoing MicroWorld frame. */
	std::uint8_t LocalNodeId{0};
};

/**
 * Motivation: Gives RP2040 Pico code a thin entry point into the portable RadioE32 transport without entangling it
 *   with UART wiring.
 * Responsibilities: Borrow the SDK UART through a byte stream and delegate framing to RadioE32; construction is inert
 *   so static storage is safe before main, Initialize opens one exclusive UART, and transparent-mode destination
 *   addresses stay shape-checked metadata rather than on-air routing.
 * Example:
 *   static FPicoLoraDevice Device;
 *   if (Device.Initialize(Config) == ETransportResult::Success) { Device.PreAdvance(NowMilliseconds); }
 */
class FPicoLoraDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Lets one device live in static storage safe before main, borrowing the production SDK binding.
	 * Responsibilities: Start closed and defer all UART and framing work to Initialize.
	 */
	FPicoLoraDevice() noexcept;

	/**
	 * Motivation: Lets host policy tests or alternate Pico wiring inject their own UART binding.
	 * Responsibilities: Start closed against the supplied binding and defer framing to Initialize.
	 */
	explicit FPicoLoraDevice(IPicoE32LoraPlatform& InPlatform) noexcept;

	/**
	 * Motivation: Guarantees the exclusively owned UART is released when a device value ends.
	 * Responsibilities: Let the delegated byte stream deinitialize the UART when initialization succeeded.
	 */
	~FPicoLoraDevice() noexcept override;

	/**
	 * Motivation: Keeps one facade the sole owner of its byte stream and delegated transport state.
	 * Responsibilities: Reject copy construction so UART ownership stays with one facade.
	 */
	FPicoLoraDevice(const FPicoLoraDevice&) = delete;

	/**
	 * Motivation: Keeps the byte stream and delegated transport state owned by one facade after construction.
	 * Responsibilities: Reject copy assignment so UART ownership cannot split across two values.
	 */
	FPicoLoraDevice& operator=(const FPicoLoraDevice&) = delete;

	/**
	 * Motivation: Keeps byte-stream ownership and the delegated transport reference stable across the facade lifetime.
	 * Responsibilities: Reject move construction so the borrowed UART and transport reference cannot relocate.
	 */
	FPicoLoraDevice(FPicoLoraDevice&&) = delete;

	/**
	 * Motivation: Keeps the borrowed UART and transport reference tied to the value that holds them.
	 * Responsibilities: Reject move assignment so byte-stream ownership cannot transfer between facades.
	 */
	FPicoLoraDevice& operator=(FPicoLoraDevice&&) = delete;

	/**
	 * Motivation: Lets a caller bring one Pico device online behind a single validated entry point.
	 * Responsibilities: Refuse a second open with Unavailable, reject an unsupported index/pin mapping, zero baud, or an
	 *   unmatchable baud with Invalid, then return the delegated RadioE32 initialization result and roll back the UART
	 *   when framing fails.
	 */
	::MicroWorld::Core::ETransportResult Initialize(const FPicoE32LoraConfig& InConfig) noexcept;

	/**
	 * Motivation: Lets a caller queue one outgoing packet for later physical progress without partial sends.
	 * Responsibilities: Return Unavailable while closed, Invalid for a malformed address/span or oversize packet, Full
	 *   while a prior frame remains queued, and Success once the complete encoded frame is queued for the pre-advance turn.
	 */
	::MicroWorld::Core::ETransportResult TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Lets a caller pump one bounded receive step and get at most one whole frame back.
	 * Responsibilities: Advance the delegated device transactionally, preserving destination, sender address, and result
	 *   on every non-success outcome and retaining the decoded frame on Full for a later retry with a larger destination.
	 */
	::MicroWorld::Core::ETransportResult TryReceive(
		Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Lets a caller size outgoing packets against the shared transport limit without framing surprises.
	 * Responsibilities: Report the delegated shared E32 payload capacity excluding framing overhead.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Lets the runtime drain queued bytes toward the radio in bounded steps instead of blocking loops.
	 * Responsibilities: Advance up to one complete encoded frame's worth of bytes when the UART is writable, by forwarding the
	 *   turn to the portable radio device that owns the framing.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override;

	/**
	 * Motivation: Lets a caller gate send, receive, and teardown logic on a single live check.
	 * Responsibilities: Report true only when the byte stream is open and the delegated RadioE32 device is initialized.
	 */
	bool IsOpen() const noexcept;

private:
	/** Motivation: Owns the configured RP2040 UART lifetime and gives RadioE32 bounded SDK-free byte operations. */
	FPicoUartByteStream ByteStream{};

	/** Motivation: Owns portable E32 framing while borrowing the preceding byte stream for its full facade lifetime. */
	::MicroWorld::Transport::FE32LoraDevice RadioDevice{ByteStream};
};

} // namespace MicroWorld::Platform::Pico
