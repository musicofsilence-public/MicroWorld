#pragma once

#include <MicroWorld/Core/IO/UartByteStream.h>
#include <MicroWorld/Core/IO/UartByteStreamResult.h>

#include <cstdint>

namespace MicroWorld::Platform::Esp32
{

/**
 * Motivation: Carries the plain-integer UART parameters one ESP-IDF byte stream needs at open time so this Detail
 *   type stays free of ESP-IDF enum types and RadioE32 depends only on Core's IUartByteStream interface.
 * Responsibilities: Hold UART port, TX/RX GPIO, and baud rate as plain integers.
 * Example:
 *   FEsp32UartByteStreamConfig Config;
 *   Config.BaudRate = 9600;
 */
struct FEsp32UartByteStreamConfig
{
	/** Motivation: ESP-IDF UART port number stored as a plain integer outside private implementation code. */
	std::int32_t UartPort{0};

	/** Motivation: GPIO wired from the UART TX signal to the attached device RX pin. */
	std::int32_t TxGpio{0};

	/** Motivation: GPIO wired from the attached device TX pin to the UART RX signal. */
	std::int32_t RxGpio{0};

	/** Motivation: Baud rate shared with the attached device; zero lets the platform open attempt reject configuration. */
	std::uint32_t BaudRate{0};
};

/**
 * Motivation: Adapts Core's non-blocking UART byte-stream contract to the ESP-IDF UART driver so a compatibility
 *   facade can drive a portable radio without exposing SDK types.
 * Responsibilities: Own one configured UART installation for its lifetime while callers retain all device
 *   configuration policy; stay a platform Detail type for compatibility facades, not a supported direct-composition API.
 * Example:
 *   FEsp32UartByteStream Stream;
 *   if (Stream.Open(Config)) { Stream.TryWriteByte(Byte); }
 */
class FEsp32UartByteStream final : public Core::IUartByteStream
{
public:
	/**
	 * Motivation: Creates an inert stream that owns no UART until one successful Open call.
	 * Responsibilities: Default-construct with bOpen false.
	 */
	FEsp32UartByteStream() noexcept = default;

	/**
	 * Motivation: Releases the exclusively owned UART installation when it is open so an adapter destruction never
	 *   leaks the driver.
	 * Responsibilities: Close the owned UART installation if it is open.
	 */
	~FEsp32UartByteStream() noexcept override;

	/**
	 * Motivation: Keeps one stream value owning exactly one ESP-IDF UART installation so the driver handle never aliases.
	 * Responsibilities: Reject copy construction so UART ownership and close responsibility remain unique.
	 */
	FEsp32UartByteStream(const FEsp32UartByteStream&) = delete;

	/**
	 * Motivation: Keeps one stream value owning exactly one ESP-IDF UART installation so the driver handle never aliases.
	 * Responsibilities: Reject copy assignment so UART ownership and close responsibility remain unique.
	 */
	FEsp32UartByteStream& operator=(const FEsp32UartByteStream&) = delete;

	/**
	 * Motivation: Keeps the owned UART identity stable until Close or destruction so the platform handle never relocates.
	 * Responsibilities: Reject move construction so the owned UART identity stays fixed.
	 */
	FEsp32UartByteStream(FEsp32UartByteStream&&) = delete;

	/**
	 * Motivation: Keeps the owned UART identity stable until Close or destruction so a pending close cannot transfer.
	 * Responsibilities: Reject move assignment so the close responsibility cannot transfer between adapter values.
	 */
	FEsp32UartByteStream& operator=(FEsp32UartByteStream&&) = delete;

	/**
	 * Motivation: Configures and exclusively opens one 8N1 ESP-IDF UART byte stream so callers retain all device
	 *   configuration policy.
	 * Responsibilities: Return false when already open or when the private ESP-IDF setup fails; on success install the
	 *   shared bounded ring-buffer configuration without exposing SDK types.
	 */
	bool Open(const FEsp32UartByteStreamConfig& InConfig) noexcept;

	/**
	 * Motivation: Releases the owned UART installation so the stream can be reopened or destroyed cleanly.
	 * Responsibilities: Release the owned installation and make later reads and writes report Error until another Open call.
	 */
	void Close() noexcept;

	/**
	 * Motivation: Lets a caller gate every op on whether the stream currently owns a usable installation.
	 * Responsibilities: Report the open flag set by Open and cleared by Close.
	 */
	bool IsOpen() const noexcept;

	/**
	 * Motivation: Attempts one non-blocking UART write behind the Core contract so callers stay free of ESP-IDF codes.
	 * Responsibilities: Map sent, blocked, and failed outcomes to the Core contract.
	 */
	Core::EUartByteStreamResult TryWriteByte(std::uint8_t InByte) noexcept override;

	/**
	 * Motivation: Attempts one non-blocking UART read behind the Core contract so callers stay free of ESP-IDF codes.
	 * Responsibilities: Change OutByte only after the ESP-IDF read succeeds.
	 */
	Core::EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept override;

private:
	/** Motivation: Stores the open UART identity as a plain integer until private source code converts it for ESP-IDF calls. */
	std::int32_t UartPortNumber{0};

	/** Motivation: Prevents I/O and duplicate installation before Open succeeds or after Close completes. */
	bool bOpen{false};
};

} // namespace MicroWorld::Platform::Esp32
