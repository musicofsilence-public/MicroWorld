#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportResult.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Transport
{

/**
 * Motivation: Appends bytes into a caller-owned fixed buffer so a codec can build a frame without allocating or throwing.
 * Responsibilities: Observe one caller-owned destination view and a write cursor, treat a {nullptr, nonzero} buffer as an
 *   invalid configuration that every mutating operation rejects without dereferencing null, and never partially advance
 *   the cursor or alter accepted bytes on a failed write.
 * Example:
 *   FByteWriter Writer(Destination);
 *   if (Writer.WriteByte(0xA5) == Core::ETransportResult::Success) { Send(Writer.WrittenBytes()); }
 */
class FByteWriter final
{
public:
	/**
	 * Motivation: Binds the writer to a caller-owned destination view at construction.
	 * Responsibilities: Observe the destination without owning it and start the cursor at zero.
	 */
	constexpr explicit FByteWriter(Core::TSpan<std::uint8_t> InStorage) noexcept : Buffer(InStorage), WritePosition(0) {}

	/**
	 * Motivation: Prevents the writer from being copied while caller storage has one owner.
	 * Responsibilities: Reject copy construction so one buffer keeps one cursor.
	 */
	FByteWriter(const FByteWriter&) = delete;

	/**
	 * Motivation: Prevents two writers from observing the same cursor over one buffer.
	 * Responsibilities: Reject copy assignment so one buffer keeps one cursor.
	 */
	FByteWriter& operator=(const FByteWriter&) = delete;

	/**
	 * Motivation: Lets a caller move a writer no other reference observes while preserving its cursor.
	 * Responsibilities: Copy the destination view and cursor, then reset the moved-from cursor to zero.
	 */
	constexpr FByteWriter(FByteWriter&& Other) noexcept : Buffer(Other.Buffer), WritePosition(Other.WritePosition) { Other.WritePosition = 0; }

	/**
	 * Motivation: Lets a caller move-assign a writer no other reference observes while preserving its cursor.
	 * Responsibilities: Guard self-assignment, copy the destination view and cursor, then reset the moved-from cursor to zero.
	 */
	constexpr FByteWriter& operator=(FByteWriter&& Other) noexcept
	{
		if (this != &Other)
		{
			Buffer = Other.Buffer;
			WritePosition = Other.WritePosition;
			Other.WritePosition = 0;
		}
		return *this;
	}

	/**
	 * Motivation: Keeps a writer with automatic storage side-effect free on destruction.
	 * Responsibilities: Default the destructor since the writer owns no resource.
	 */
	~FByteWriter() noexcept = default;

	/**
	 * Motivation: Lets a codec append one byte transactionally so a full or invalid buffer never corrupts accepted bytes.
	 * Responsibilities: Return Invalid without advancing for an invalid {nullptr, nonzero} buffer, Full without advancing
	 *   when a valid buffer has no remaining capacity, and write and advance only on success.
	 */
	Core::ETransportResult WriteByte(const std::uint8_t InValue) noexcept
	{
		if (!HasValidStorage())
		{
			return Core::ETransportResult::Invalid;
		}
		if (WritePosition >= Buffer.Size())
		{
			return Core::ETransportResult::Full;
		}
		Buffer[WritePosition] = InValue;
		++WritePosition;
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Lets a codec append a span transactionally so a full or invalid buffer never leaves a partial write.
	 * Responsibilities: Treat an empty span as a valid no-op, reject a {nullptr, nonzero} source and an invalid {nullptr,
	 *   nonzero} destination with Invalid, reject a span larger than total capacity with Invalid (it can never fit) and one
	 *   that fits total but not remaining capacity with Full, and copy and advance only on a complete write.
	 */
	Core::ETransportResult Write(Core::TSpan<const std::uint8_t> InBytes) noexcept
	{
		const std::size_t IncomingSize = InBytes.Size();
		if (IncomingSize == 0)
		{
			// An empty span is a valid no-op whether or not its data pointer is null.
			return Core::ETransportResult::Success;
		}
		if (InBytes.Data() == nullptr)
		{
			// A null source with nonzero length cannot be copied honestly.
			return Core::ETransportResult::Invalid;
		}
		if (!HasValidStorage())
		{
			// A null destination with nonzero capacity cannot accept any byte honestly.
			return Core::ETransportResult::Invalid;
		}
		if (IncomingSize > Buffer.Size())
		{
			// The span can never fit the total buffer; the request is malformed.
			return Core::ETransportResult::Invalid;
		}
		if (IncomingSize > Buffer.Size() - WritePosition)
		{
			// The span fits the total buffer but not the remaining capacity.
			return Core::ETransportResult::Full;
		}
		std::memcpy(Buffer.Data() + WritePosition, InBytes.Data(), IncomingSize);
		WritePosition += IncomingSize;
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Lets a caller observe the caller-owned buffer capacity without exposing the view.
	 * Responsibilities: Report the buffer length recorded at construction.
	 */
	constexpr std::size_t Capacity() const noexcept { return Buffer.Size(); }

	/**
	 * Motivation: Lets a caller report accepted bytes that survive a failed operation.
	 * Responsibilities: Report the accepted prefix length.
	 */
	constexpr std::size_t Position() const noexcept { return WritePosition; }

	/**
	 * Motivation: Lets a caller predict how many bytes a valid writer can still accept.
	 * Responsibilities: Report the buffer length minus the cursor.
	 */
	constexpr std::size_t Remaining() const noexcept { return Buffer.Size() - WritePosition; }

	/**
	 * Motivation: Gives a caller a view of the accepted prefix to hand off without exposing mutable storage.
	 * Responsibilities: Return an empty view for an invalid {nullptr, nonzero} backing buffer, and the accepted prefix otherwise.
	 */
	constexpr Core::TSpan<const std::uint8_t> WrittenBytes() const noexcept
	{
		if (!HasValidStorage())
		{
			return Core::TSpan<const std::uint8_t>(nullptr, 0);
		}
		return Core::TSpan<const std::uint8_t>(Buffer.Data(), WritePosition);
	}

	/**
	 * Motivation: Lets a caller reuse the caller-owned buffer from the start.
	 * Responsibilities: Reset the cursor to zero without zeroing accepted bytes, since the caller owns the storage.
	 */
	constexpr void Reset() noexcept { WritePosition = 0; }

private:
	/**
	 * Motivation: Guards every mutating operation against an invalid {nullptr, nonzero} buffer.
	 * Responsibilities: Report true for a valid empty {nullptr, 0} buffer and false for an invalid {nullptr, nonzero} one.
	 */
	constexpr bool HasValidStorage() const noexcept { return Buffer.Data() != nullptr || Buffer.Size() == 0; }

	/** Motivation: Observes the caller-owned destination, which the writer never releases or grows. */
	Core::TSpan<std::uint8_t> Buffer;

	/** Motivation: Tracks the accepted prefix length so failures leave prior bytes intact. */
	std::size_t WritePosition;
};

/**
 * Motivation: Serializes a 16-bit value as two big-endian bytes for codecs that emit the most significant byte first.
 * Responsibilities: Write exactly two bytes into the caller-owned destination.
 */
inline void WriteUint16BigEndian(const std::uint16_t InValue, std::uint8_t* const OutBytes) noexcept
{
	OutBytes[0] = static_cast<std::uint8_t>(InValue >> Core::HighByteShift);
	OutBytes[1] = static_cast<std::uint8_t>(InValue & Core::LowByteMask);
}

/**
 * Motivation: Serializes a 16-bit value as two little-endian bytes for codecs that emit the least significant byte first.
 * Responsibilities: Write exactly two bytes into the caller-owned destination.
 */
inline void WriteUint16LittleEndian(const std::uint16_t InValue, std::uint8_t* const OutBytes) noexcept
{
	OutBytes[0] = static_cast<std::uint8_t>(InValue & Core::LowByteMask);
	OutBytes[1] = static_cast<std::uint8_t>(InValue >> Core::HighByteShift);
}

} // namespace MicroWorld::Transport
