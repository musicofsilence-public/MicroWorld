#pragma once

#include <MicroWorld/Core/IO/UartByteStream.h>
#include <MicroWorld/Platform/Pico/Internal/PicoUartPlatform.h>

#include <cstdint>

namespace MicroWorld::Platform::Pico
{

/**
 * Motivation: Carries one RP2040 UART identity to the platform entry point without dragging Pico SDK types into
 *   the public header.
 * Responsibilities: Hold UART index, TX/RX GPIO routing, and an exact baud rate for the byte stream to validate.
 * Example:
 *   FPicoUartConfig Config{0, 0, 1, 115200};
 *   Stream.Open(Config);
 */
struct FPicoUartConfig
{
	/** Motivation: Selects the RP2040 UART hardware block: 0 for UART0 or 1 for UART1. */
	std::uint8_t UartIndex{0};

	/** Motivation: Names the RP2040 GPIO routed from UART TX to the attached device RX pin. */
	unsigned int TxGpio{0};

	/** Motivation: Names the RP2040 GPIO routed from the attached device TX pin to UART RX. */
	unsigned int RxGpio{1};

	/** Motivation: Pins the exact baud rate shared with the attached device; zero is treated as invalid. */
	std::uint32_t BaudRate{0};
};

/**
 * Motivation: Backs Core's non-blocking UART byte-stream contract with one RP2040 UART while keeping SDK types out of
 *   portable byte-stream callers.
 * Responsibilities: Own one validated open/close UART lifetime while borrowing the platform binding; it is a platform
 *   Detail extension for compatibility facades, not a universal hardware abstraction or supported direct-composition API.
 * Example:
 *   FPicoUartByteStream Stream;
 *   if (Stream.Open(Config)) { Stream.TryWriteByte(0x55); }
 */
class FPicoUartByteStream final : public Core::IUartByteStream
{
public:
	/**
	 * Motivation: Lets one stream start inert so static storage is safe before main and before a UART is chosen.
	 * Responsibilities: Borrow the process-lifetime Pico SDK binding and leave the stream closed.
	 */
	FPicoUartByteStream() noexcept;

	/**
	 * Motivation: Lets host tests or alternate Pico wiring inject their own binding instead of the SDK one.
	 * Responsibilities: Borrow the supplied binding and leave the stream closed.
	 */
	explicit FPicoUartByteStream(IPicoUartPlatform& InPlatform) noexcept;

	/**
	 * Motivation: Guarantees no opened UART leaks when a stream value ends.
	 * Responsibilities: Close the exclusively owned UART when a prior Open succeeded.
	 */
	~FPicoUartByteStream() noexcept override;

	/**
	 * Motivation: Keeps one stream the sole owner of its UART identity and close responsibility.
	 * Responsibilities: Reject copy construction so a borrowed binding stays tied to one stream lifetime.
	 */
	FPicoUartByteStream(const FPicoUartByteStream&) = delete;

	/**
	 * Motivation: Keeps the borrowed platform binding associated with one stream lifetime after construction.
	 * Responsibilities: Reject copy assignment so UART ownership cannot split across two values.
	 */
	FPicoUartByteStream& operator=(const FPicoUartByteStream&) = delete;

	/**
	 * Motivation: Keeps the UART identity and borrowed binding stable across the stream's lifetime.
	 * Responsibilities: Reject move construction so the pending UART close cannot transfer between values.
	 */
	FPicoUartByteStream(FPicoUartByteStream&&) = delete;

	/**
	 * Motivation: Keeps the pending UART close tied to the value that opened it.
	 * Responsibilities: Reject move assignment so close responsibility cannot transfer between stream values.
	 */
	FPicoUartByteStream& operator=(FPicoUartByteStream&&) = delete;

	/**
	 * Motivation: Lets a caller claim one UART for non-blocking byte traffic behind a single validated call.
	 * Responsibilities: Refuse a second open, an unsupported index/pin mapping or zero baud, and a baud the SDK cannot
	 *   match exactly; roll back a partial SDK open on failure and report success only after owning the configured UART.
	 */
	bool Open(const FPicoUartConfig& InConfig) noexcept;

	/**
	 * Motivation: Lets a caller release the UART before destruction and make the stream safe to reopen.
	 * Responsibilities: Release the owned UART and leave later reads and writes reporting Error until another Open.
	 */
	void Close() noexcept;

	/**
	 * Motivation: Lets a caller gate byte work and reopen logic on whether the stream holds a live UART.
	 * Responsibilities: Report whether this stream owns a successfully opened UART.
	 */
	bool IsOpen() const noexcept;

	/**
	 * Motivation: Lets portable framing push one byte without blocking on a full UART.
	 * Responsibilities: Write one byte when the UART has non-blocking capacity, returning Unavailable when it does not
	 *   and Error when the stream is closed.
	 */
	Core::EUartByteStreamResult TryWriteByte(std::uint8_t InByte) noexcept override;

	/**
	 * Motivation: Lets portable framing poll one byte without blocking on an empty UART.
	 * Responsibilities: Fill OutByte only after the borrowed binding supplies a byte, returning Unavailable when none is
	 *   ready and Error when the stream is closed.
	 */
	Core::EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept override;

private:
	/**
	 * Motivation: Stops an invalid TX pin from reaching the SDK so a failed pin mapping is reported before opening.
	 * Responsibilities: Report whether a GPIO can carry TX for the selected RP2040 UART.
	 */
	static bool IsValidTransmitPin(std::uint8_t InUartIndex, unsigned int InPin) noexcept;

	/**
	 * Motivation: Stops an invalid RX pin from reaching the SDK so a failed pin mapping is reported before opening.
	 * Responsibilities: Report whether a GPIO can carry RX for the selected RP2040 UART.
	 */
	static bool IsValidReceivePin(std::uint8_t InUartIndex, unsigned int InPin) noexcept;

	/** Motivation: Borrows the platform binding that must outlive this stream and confines Pico SDK calls to the platform edge. */
	IPicoUartPlatform& Platform;

	/** Motivation: Stores the initialized UART identity for each bounded byte operation and later close. */
	std::uint8_t UartIndexValue{0};

	/** Motivation: Prevents platform access before a successful Open and protects exclusive ownership from a double Open. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Pico
