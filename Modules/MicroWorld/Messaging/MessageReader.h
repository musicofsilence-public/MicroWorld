#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Messaging/MessagingResult.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Messaging
{

/**
 * Motivation: Gives ADL codecs one bounded allocation-free way to decode fixed payload fields in portable byte order.
 * Responsibilities: Consume only complete U8, U16, U32, or byte spans and leave outputs unchanged on insufficient input.
 * Example: if (Reader.ReadU16(Port) != EMessagingResult::Success) { return EMessagingResult::Invalid; }
 */
class FMessageReader final
{
public:
	/**
	 * Motivation: Binds the synchronous inbound payload whose owner keeps it valid during codec execution.
	 * Responsibilities: Begin before its first byte.
	 */
	explicit FMessageReader(const Core::TSpan<const std::uint8_t> InBuffer) noexcept : Buffer(InBuffer) {}

	/**
	 * Motivation: Decodes one byte field.
	 * Responsibilities: Assign OutValue only when one byte remains.
	 */
	EMessagingResult ReadU8(std::uint8_t& OutValue) noexcept;

	/**
	 * Motivation: Decodes one two-byte little-endian field.
	 * Responsibilities: Assign OutValue only when two bytes remain.
	 */
	EMessagingResult ReadU16(std::uint16_t& OutValue) noexcept;

	/**
	 * Motivation: Decodes one four-byte little-endian field.
	 * Responsibilities: Assign OutValue only when four bytes remain.
	 */
	EMessagingResult ReadU32(std::uint32_t& OutValue) noexcept;

	/**
	 * Motivation: Decodes one fixed opaque field.
	 * Responsibilities: Copy every requested byte only when the full span remains.
	 */
	EMessagingResult ReadBytes(Core::TSpan<std::uint8_t> OutBytes) noexcept;

	/**
	 * Motivation: Lets decoders enforce exact payload consumption.
	 * Responsibilities: Return unread bytes without mutation.
	 */
	std::size_t Remaining() const noexcept { return Buffer.Size() - Offset; }

	/**
	 * Motivation: Lets callers observe decoded length without retaining reader storage.
	 * Responsibilities: Return bytes consumed without mutation.
	 */
	std::size_t Consumed() const noexcept { return Offset; }

private:
	/** Motivation: Retains the non-owned inbound bytes for one synchronous decoding operation. */
	Core::TSpan<const std::uint8_t> Buffer{};

	/** Motivation: Identifies the first unread byte in Buffer. */
	std::size_t Offset{0};
};

} // namespace MicroWorld::Messaging
