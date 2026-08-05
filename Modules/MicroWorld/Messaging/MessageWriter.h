#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Messaging/MessagingResult.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Gives ADL codecs one bounded allocation-free way to encode fixed payload fields in portable byte order.
 * Responsibilities: Append only complete U8, U16, U32, or byte spans in little-endian order and report Full without partial writes.
 * Example: Writer.WriteU16(Port);
 */
class FMessageWriter final
{
public:
	/**
	 * Motivation: Binds one caller-owned fixed buffer whose lifetime covers a synchronous codec call.
	 * Responsibilities: Begin with no bytes written.
	 */
	explicit FMessageWriter(const Core::TSpan<std::uint8_t> InBuffer) noexcept : Buffer(InBuffer) {}

	/**
	 * Motivation: Encodes one byte field.
	 * Responsibilities: Append InValue or return Full without mutation.
	 */
	EMessagingResult WriteU8(std::uint8_t InValue) noexcept;

	/**
	 * Motivation: Encodes one two-byte field portably.
	 * Responsibilities: Append InValue little-endian or return Full without mutation.
	 */
	EMessagingResult WriteU16(std::uint16_t InValue) noexcept;

	/**
	 * Motivation: Encodes one four-byte field portably.
	 * Responsibilities: Append InValue little-endian or return Full without mutation.
	 */
	EMessagingResult WriteU32(std::uint32_t InValue) noexcept;

	/**
	 * Motivation: Encodes an already bounded opaque field.
	 * Responsibilities: Append every byte or return Full without a partial copy.
	 */
	EMessagingResult WriteBytes(Core::TSpan<const std::uint8_t> InBytes) noexcept;

	/**
	 * Motivation: Lets callers pass exactly the encoded payload to Messaging.
	 * Responsibilities: Return the written prefix without mutation.
	 */
	Core::TSpan<const std::uint8_t> WrittenBytes() const noexcept { return Core::TSpan<const std::uint8_t>(Buffer.Data(), Offset); }

	/**
	 * Motivation: Lets codecs preflight bounded compound fields.
	 * Responsibilities: Return unused buffer bytes without mutation.
	 */
	std::size_t Remaining() const noexcept { return Buffer.Size() - Offset; }

	/**
	 * Motivation: Lets callers observe encoded length without retaining writer storage.
	 * Responsibilities: Return bytes written without mutation.
	 */
	std::size_t Consumed() const noexcept { return Offset; }

private:
	/** Motivation: Retains caller-owned writable storage for one synchronous encoding operation. */
	Core::TSpan<std::uint8_t> Buffer{};

	/** Motivation: Identifies the first unwritten byte in Buffer. */
	std::size_t Offset{0};
};

} // namespace MicroWorld::Messaging
