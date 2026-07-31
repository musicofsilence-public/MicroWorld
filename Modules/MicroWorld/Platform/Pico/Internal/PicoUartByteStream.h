#pragma once

#include <MicroWorld/Core/IO/UartByteStream.h>
#include <MicroWorld/Platform/Pico/Internal/PicoUartPlatform.h>

#include <cstdint>

namespace MicroWorld
{

/**
 * Plain Pico UART configuration owned by the platform composition root.
 *
 * The byte stream validates this hardware identity before opening; device policy remains outside this unsupported
 * Detail type and no Pico SDK type crosses the public header boundary.
 */
struct FPicoUartConfig
{
	/** RP2040 UART hardware index: `0` for UART0 or `1` for UART1. */
	std::uint8_t UartIndex{0};

	/** RP2040 GPIO routed from UART TX to the attached device RX pin. */
	unsigned int TxGpio{0};

	/** RP2040 GPIO routed from the attached device TX pin to UART RX. */
	unsigned int RxGpio{1};

	/** Exact UART baud rate shared with the attached device; zero is invalid. */
	std::uint32_t BaudRate{0};
};

/**
 * Unsupported Pico implementation of Core's non-blocking UART byte-stream contract.
 *
 * The stream owns one validated UART open/close lifetime while borrowing the SDK binding; it is a platform Detail
 * extension interface for compatibility facades, not a universal hardware abstraction or supported direct-composition API.
 */
class FPicoUartByteStream final : public IUartByteStream
{
public:
	/** Creates an inert stream that borrows the process-lifetime Pico SDK binding. */
	FPicoUartByteStream() noexcept;

	/** Creates an inert stream that borrows a supplied binding for host tests or alternate Pico wiring. */
	explicit FPicoUartByteStream(IPicoUartPlatform& InPlatform) noexcept;

	/** Releases the exclusively owned UART when Open succeeded. */
	~FPicoUartByteStream() noexcept override;

	/** Prevents copying so one stream owns exactly one UART identity and close responsibility. */
	FPicoUartByteStream(const FPicoUartByteStream&) = delete;

	/** Prevents copying so a borrowed platform binding remains associated with one stream lifetime. */
	FPicoUartByteStream& operator=(const FPicoUartByteStream&) = delete;

	/** Prevents moving so the UART identity and borrowed binding remain stable. */
	FPicoUartByteStream(FPicoUartByteStream&&) = delete;

	/** Prevents moving so the pending UART close cannot transfer between stream values. */
	FPicoUartByteStream& operator=(FPicoUartByteStream&&) = delete;

	/**
	 * Validates and exclusively opens one RP2040 UART for non-blocking byte traffic.
	 *
	 * Returns false when already open, for an unsupported index/pin mapping or zero baud, or when the SDK cannot
	 * achieve the requested baud exactly; a failed open after SDK initialization rolls the UART back.
	 *
	 * @param InConfig UART identity, GPIO routing, and exact baud rate.
	 * @return True only after the stream owns a configured UART.
	 */
	bool Open(const FPicoUartConfig& InConfig) noexcept;

	/** Releases the owned UART and makes later reads and writes report Error until another successful Open. */
	void Close() noexcept;

	/** Reports whether this stream owns a successfully opened UART. */
	bool IsOpen() const noexcept;

	/** Attempts one byte write, returning Unavailable when the UART has no non-blocking transmit capacity. */
	EUartByteStreamResult TryWriteByte(std::uint8_t InByte) noexcept override;

	/** Attempts one byte read and changes OutByte only after the borrowed platform binding supplies a byte. */
	EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept override;

private:
	/** Reports whether a GPIO can carry TX for the selected RP2040 UART. */
	static bool IsValidTransmitPin(std::uint8_t InUartIndex, unsigned int InPin) noexcept;

	/** Reports whether a GPIO can carry RX for the selected RP2040 UART. */
	static bool IsValidReceivePin(std::uint8_t InUartIndex, unsigned int InPin) noexcept;

	/** Borrows the platform binding that must outlive this stream and confines Pico SDK calls to the platform edge. */
	IPicoUartPlatform& Platform;

	/** Stores the initialized UART identity for each bounded byte operation and later close. */
	std::uint8_t UartIndexValue{0};

	/** Prevents platform access before successful Open and protects exclusive ownership from a double Open. */
	bool bOpen{false};
};

} // namespace MicroWorld
