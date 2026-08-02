#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportDevice.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Transport
{

/**
 * Motivation: Reads bytes from a caller-owned fixed buffer so a parser can advance a cursor without allocating or throwing.
 * Responsibilities: Observe one caller-owned source view and a read cursor, treat a {nullptr, nonzero} source as an invalid
 *   configuration that every consuming operation rejects without dereferencing null, and never advance the cursor or modify
 *   output parameters on a failed read.
 * Example:
 *   FByteReader Reader(Source);
 *   std::uint8_t Byte = 0;
 *   if (Reader.ReadByte(Byte) == Core::ETransportResult::Success) { Parse(Byte); }
 */
class FByteReader final
{
public:
	/**
	 * Motivation: Binds the reader to a caller-owned source view at construction.
	 * Responsibilities: Observe the source without owning it and start the cursor at zero.
	 */
	constexpr explicit FByteReader(Core::TSpan<const std::uint8_t> InSource) noexcept : Buffer(InSource), ReadPosition(0) {}

	/**
	 * Motivation: Prevents the reader from being copied while caller storage has one observer.
	 * Responsibilities: Reject copy construction so one source keeps one cursor.
	 */
	FByteReader(const FByteReader&) = delete;

	/**
	 * Motivation: Prevents two readers from advancing independent cursors over one source.
	 * Responsibilities: Reject copy assignment so one source keeps one cursor.
	 */
	FByteReader& operator=(const FByteReader&) = delete;

	/**
	 * Motivation: Lets a caller move a reader no other reference observes while preserving its cursor.
	 * Responsibilities: Copy the source view and cursor, then reset the moved-from cursor to zero.
	 */
	constexpr FByteReader(FByteReader&& Other) noexcept : Buffer(Other.Buffer), ReadPosition(Other.ReadPosition) { Other.ReadPosition = 0; }

	/**
	 * Motivation: Lets a caller move-assign a reader no other reference observes while preserving its cursor.
	 * Responsibilities: Guard self-assignment, copy the source view and cursor, then reset the moved-from cursor to zero.
	 */
	constexpr FByteReader& operator=(FByteReader&& Other) noexcept
	{
		if (this != &Other)
		{
			Buffer = Other.Buffer;
			ReadPosition = Other.ReadPosition;
			Other.ReadPosition = 0;
		}
		return *this;
	}

	/**
	 * Motivation: Keeps a reader with automatic storage side-effect free on destruction.
	 * Responsibilities: Default the destructor since the reader owns no resource.
	 */
	~FByteReader() noexcept = default;

	/**
	 * Motivation: Lets a parser consume one byte transactionally so a failed read never corrupts the output.
	 * Responsibilities: Return Invalid without modifying OutValue when the source is an invalid {nullptr, nonzero} view or no
	 *   byte remains; otherwise write the byte and advance the cursor.
	 */
	Core::ETransportResult ReadByte(std::uint8_t& OutValue) noexcept
	{
		if (!HasValidStorage() || ReadPosition >= Buffer.Size())
		{
			return Core::ETransportResult::Invalid;
		}
		OutValue = Buffer[ReadPosition];
		++ReadPosition;
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Lets a parser read a fixed run of bytes transactionally so a short source never partially fills the destination.
	 * Responsibilities: Treat an empty destination as a valid no-op, reject a null destination with nonzero length and an
	 *   invalid {nullptr, nonzero} source with Invalid, reject a truncated request (more than remaining) with Invalid without
	 *   modifying the destination or advancing, and copy and advance only on a complete read.
	 */
	Core::ETransportResult Read(Core::TSpan<std::uint8_t> InDestination) noexcept
	{
		const std::size_t RequestedSize = InDestination.Size();
		if (RequestedSize == 0)
		{
			return Core::ETransportResult::Success;
		}
		if (InDestination.Data() == nullptr)
		{
			return Core::ETransportResult::Invalid;
		}
		if (!HasValidStorage())
		{
			return Core::ETransportResult::Invalid;
		}
		if (RequestedSize > Buffer.Size() - ReadPosition)
		{
			// The request exceeds the remaining source; treat it as a truncated request.
			return Core::ETransportResult::Invalid;
		}
		std::memcpy(InDestination.Data(), Buffer.Data() + ReadPosition, RequestedSize);
		ReadPosition += RequestedSize;
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Lets a parser look ahead one byte without consuming it.
	 * Responsibilities: Return Invalid without modifying OutValue when the source is an invalid {nullptr, nonzero} view or no
	 *   byte remains; otherwise write the byte and leave the cursor unchanged.
	 */
	Core::ETransportResult PeekByte(std::uint8_t& OutValue) const noexcept
	{
		if (!HasValidStorage() || ReadPosition >= Buffer.Size())
		{
			return Core::ETransportResult::Invalid;
		}
		OutValue = Buffer[ReadPosition];
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Lets a caller observe the caller-owned source length without exposing the view.
	 * Responsibilities: Report the source length recorded at construction.
	 */
	constexpr std::size_t Capacity() const noexcept { return Buffer.Size(); }

	/**
	 * Motivation: Lets a caller report progress that survives a failed read.
	 * Responsibilities: Report the consumed prefix length.
	 */
	constexpr std::size_t Position() const noexcept { return ReadPosition; }

	/**
	 * Motivation: Lets a caller predict how many bytes a valid reader can still return.
	 * Responsibilities: Report the source length minus the cursor.
	 */
	constexpr std::size_t Remaining() const noexcept { return Buffer.Size() - ReadPosition; }

	/**
	 * Motivation: Gives a parser a suffix view it can hand off without exposing mutable storage.
	 * Responsibilities: Return an empty view whenever the backing data pointer is null (both the valid {nullptr, 0} and the
	 *   invalid {nullptr, nonzero} source), so no caller performs pointer arithmetic on null.
	 */
	constexpr Core::TSpan<const std::uint8_t> RemainingBytes() const noexcept
	{
		if (Buffer.Data() == nullptr)
		{
			// A null data pointer has no honest base address for the suffix view,
			// whether the source is the valid empty `{nullptr, 0}` or an invalid `{nullptr, nonzero}`.
			return Core::TSpan<const std::uint8_t>(nullptr, 0);
		}
		return Core::TSpan<const std::uint8_t>(Buffer.Data() + ReadPosition, Buffer.Size() - ReadPosition);
	}

	/**
	 * Motivation: Lets a caller re-parse the same caller-owned source from the start.
	 * Responsibilities: Reset the cursor to zero without touching the source bytes.
	 */
	constexpr void Reset() noexcept { ReadPosition = 0; }

private:
	/**
	 * Motivation: Guards every consuming operation against an invalid {nullptr, nonzero} source.
	 * Responsibilities: Report true for a valid empty {nullptr, 0} source and false for an invalid {nullptr, nonzero} one.
	 */
	constexpr bool HasValidStorage() const noexcept { return Buffer.Data() != nullptr || Buffer.Size() == 0; }

	/** Motivation: Observes the caller-owned source, which the reader never releases or grows. */
	Core::TSpan<const std::uint8_t> Buffer;

	/** Motivation: Tracks the consumed prefix length so failures leave the cursor intact. */
	std::size_t ReadPosition;
};

/**
 * Motivation: Decodes a 16-bit value from two big-endian bytes for codecs that store the most significant byte first.
 * Responsibilities: Read exactly two bytes and return their decoded value.
 */
inline std::uint16_t ReadUint16BigEndian(const std::uint8_t* const InBytes) noexcept
{
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(InBytes[0]) << Core::HighByteShift) | static_cast<std::uint16_t>(InBytes[1]));
}

/**
 * Motivation: Decodes a 16-bit value from two little-endian bytes for codecs that store the least significant byte first.
 * Responsibilities: Read exactly two bytes and return their decoded value.
 */
inline std::uint16_t ReadUint16LittleEndian(const std::uint8_t* const InBytes) noexcept
{
	return static_cast<std::uint16_t>(
		static_cast<std::uint16_t>(InBytes[0]) | static_cast<std::uint16_t>(static_cast<std::uint16_t>(InBytes[1]) << Core::HighByteShift));
}

} // namespace MicroWorld::Transport
