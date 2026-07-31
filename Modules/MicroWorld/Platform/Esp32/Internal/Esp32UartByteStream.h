#pragma once

#include <MicroWorld/Core/IO/UartByteStream.h>

#include <cstdint>

namespace MicroWorld
{

/**
 * Plain-integer configuration for one ESP-IDF UART byte stream.
 *
 * Platform composition owns the selected UART exclusively and supplies all pin and baud policy before opening the
 * stream; this Detail type stays free of ESP-IDF enum types so RadioE32 can depend only on Core's `IUartByteStream` interface.
 */
struct FEsp32UartByteStreamConfig
{
	/** ESP-IDF UART port number stored as a plain integer outside private implementation code. */
	std::int32_t UartPort{0};

	/** GPIO wired from the UART TX signal to the attached device RX pin. */
	std::int32_t TxGpio{0};

	/** GPIO wired from the attached device TX pin to the UART RX signal. */
	std::int32_t RxGpio{0};

	/** Baud rate shared with the attached device; zero lets the platform open attempt reject configuration. */
	std::uint32_t BaudRate{0};
};

/**
 * Unsupported ESP-IDF implementation of Core's non-blocking UART byte-stream contract.
 *
 * This adapter owns one configured UART installation for its lifetime while callers retain all device configuration
 * policy. It is a platform Detail type for compatibility facades, not a supported direct-composition API.
 */
class FEsp32UartByteStream final : public IUartByteStream
{
public:
	/** Creates an inert stream that owns no UART until one successful Open call. */
	FEsp32UartByteStream() noexcept = default;

	/** Releases the exclusively owned UART installation when it is open. */
	~FEsp32UartByteStream() noexcept override;

	/** Prevents copying so one stream value owns exactly one ESP-IDF UART installation. */
	FEsp32UartByteStream(const FEsp32UartByteStream&) = delete;

	/** Prevents copying so UART ownership and close responsibility remain unique. */
	FEsp32UartByteStream& operator=(const FEsp32UartByteStream&) = delete;

	/** Prevents moving so the owned UART identity remains stable until Close or destruction. */
	FEsp32UartByteStream(FEsp32UartByteStream&&) = delete;

	/** Prevents moving so a pending close cannot transfer between adapter values. */
	FEsp32UartByteStream& operator=(FEsp32UartByteStream&&) = delete;

	/**
	 * Configures and exclusively opens one 8N1 ESP-IDF UART byte stream.
	 *
	 * Returns false when already open or when the private ESP-IDF setup fails; success installs the shared bounded
	 * ring-buffer configuration without exposing SDK types.
	 *
	 * @param InConfig Plain UART port, pin, and baud settings selected by the platform composition root.
	 * @return True only when the stream owns a configured UART installation.
	 */
	bool Open(const FEsp32UartByteStreamConfig& InConfig) noexcept;

	/** Releases the owned UART installation and makes later reads and writes report Error until another Open call. */
	void Close() noexcept;

	/** Reports whether this stream currently owns a usable configured UART installation. */
	bool IsOpen() const noexcept;

	/** Attempts one non-blocking UART write and maps sent, blocked, and failed outcomes to the Core contract. */
	EUartByteStreamResult TryWriteByte(std::uint8_t InByte) noexcept override;

	/** Attempts one non-blocking UART read and changes OutByte only after the ESP-IDF read succeeds. */
	EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept override;

private:
	/** Stores the open UART identity as a plain integer until private source code converts it for ESP-IDF calls. */
	std::int32_t UartPortNumber{0};

	/** Prevents I/O and duplicate installation before Open succeeds or after Close completes. */
	bool bOpen{false};
};

} // namespace MicroWorld
