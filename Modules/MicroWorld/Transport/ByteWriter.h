#pragma once

#include <MicroWorld/Core/ByteCodecConstants.h>
#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld
{

/**
 * Appends bytes into a caller-owned fixed buffer without allocating or throwing.
 *
 * The writer observes one caller-owned `TSpan<std::uint8_t>` and tracks a write
 * cursor. A buffer bound to `{nullptr, nonzero}` is an invalid configuration:
 * every mutating operation returns `Invalid` without dereferencing null, and a
 * failed write never partially advances the cursor or alters accepted bytes.
 */
class FByteWriter final
{
public:
	/** Binds the writer to a caller-owned destination view; storage is observed, never owned. */
	constexpr explicit FByteWriter(TSpan<std::uint8_t> InStorage) noexcept : Buffer(InStorage), WritePosition(0) {}

	/** Prevents the writer from being copied while caller storage has one owner. */
	FByteWriter(const FByteWriter&) = delete;

	/** Prevents two writers from observing the same cursor over one buffer. */
	FByteWriter& operator=(const FByteWriter&) = delete;

	/** Lets a caller move a writer that no other reference observes, preserving its cursor. */
	constexpr FByteWriter(FByteWriter&& Other) noexcept : Buffer(Other.Buffer), WritePosition(Other.WritePosition) { Other.WritePosition = 0; }

	/** Lets a caller move-assign a writer that no other reference observes, preserving its cursor. */
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

	/** Defaulted so a writer with automatic storage destructs without side effects. */
	~FByteWriter() noexcept = default;

	/**
	 * Appends one byte and advances the cursor.
	 * Returns `Invalid` without advancing when the backing buffer is an invalid `{nullptr, nonzero}` view.
	 * Returns `Full` without advancing when a valid buffer has no remaining capacity.
	 */
	ETransportResult WriteByte(const std::uint8_t InValue) noexcept
	{
		if (!HasValidStorage())
		{
			return ETransportResult::Invalid;
		}
		if (WritePosition >= Buffer.Size())
		{
			return ETransportResult::Full;
		}
		Buffer[WritePosition] = InValue;
		++WritePosition;
		return ETransportResult::Success;
	}

	/**
	 * Appends a byte span and advances the cursor.
	 * An empty span is a valid no-op. A span bound to `{nullptr, nonzero}` returns `Invalid`.
	 * A span larger than the total buffer capacity returns `Invalid` because it can never fit.
	 * A span that fits the total capacity but exceeds remaining capacity returns `Full`.
	 * None of these failures advances the cursor or alters accepted bytes.
	 */
	ETransportResult Write(TSpan<const std::uint8_t> InBytes) noexcept
	{
		const std::size_t IncomingSize = InBytes.Size();
		if (IncomingSize == 0)
		{
			// An empty span is a valid no-op whether or not its data pointer is null.
			return ETransportResult::Success;
		}
		if (InBytes.Data() == nullptr)
		{
			// A null source with nonzero length cannot be copied honestly.
			return ETransportResult::Invalid;
		}
		if (!HasValidStorage())
		{
			// A null destination with nonzero capacity cannot accept any byte honestly.
			return ETransportResult::Invalid;
		}
		if (IncomingSize > Buffer.Size())
		{
			// The span can never fit the total buffer; the request is malformed.
			return ETransportResult::Invalid;
		}
		if (IncomingSize > Buffer.Size() - WritePosition)
		{
			// The span fits the total buffer but not the remaining capacity.
			return ETransportResult::Full;
		}
		std::memcpy(Buffer.Data() + WritePosition, InBytes.Data(), IncomingSize);
		WritePosition += IncomingSize;
		return ETransportResult::Success;
	}

	/** Reports the caller-owned buffer capacity observed at construction. */
	constexpr std::size_t Capacity() const noexcept { return Buffer.Size(); }

	/** Reports how many bytes have been accepted and survive a failed operation. */
	constexpr std::size_t Position() const noexcept { return WritePosition; }

	/** Reports how many more bytes a valid writer can accept before the next `Full`. */
	constexpr std::size_t Remaining() const noexcept { return Buffer.Size() - WritePosition; }

	/**
	 * Returns a read-only view of the accepted prefix without exposing mutable storage.
	 * The view is empty for an invalid `{nullptr, nonzero}` backing buffer.
	 */
	constexpr TSpan<const std::uint8_t> WrittenBytes() const noexcept
	{
		if (!HasValidStorage())
		{
			return TSpan<const std::uint8_t>(nullptr, 0);
		}
		return TSpan<const std::uint8_t>(Buffer.Data(), WritePosition);
	}

	/**
	 * Resets the cursor to zero so the caller-owned buffer can be reused.
	 * Previously accepted bytes are not zeroed; the caller owns the storage and
	 * decides when to clear it.
	 */
	constexpr void Reset() noexcept { WritePosition = 0; }

private:
	/**
	 * Reports whether the backing buffer can honestly hold bytes.
	 * A `{nullptr, 0}` view is a valid empty buffer; a `{nullptr, nonzero}` view is an invalid
	 * configuration that must never be dereferenced.
	 */
	constexpr bool HasValidStorage() const noexcept { return Buffer.Data() != nullptr || Buffer.Size() == 0; }

	/** Observes the caller-owned destination; the writer never releases or grows it. */
	TSpan<std::uint8_t> Buffer;

	/** Tracks the accepted prefix length so failures leave prior bytes intact. */
	std::size_t WritePosition;
};

/**
 * Writes a 16-bit value as two big-endian bytes (most significant byte first).
 *
 * @param InValue 16-bit value to serialize.
 * @param OutBytes Caller-owned destination for exactly two bytes.
 */
inline void WriteUint16BigEndian(const std::uint16_t InValue, std::uint8_t* const OutBytes) noexcept
{
	OutBytes[0] = static_cast<std::uint8_t>(InValue >> HighByteShift);
	OutBytes[1] = static_cast<std::uint8_t>(InValue & LowByteMask);
}

/**
 * Writes a 16-bit value as two little-endian bytes (least significant byte first).
 *
 * @param InValue 16-bit value to serialize.
 * @param OutBytes Caller-owned destination for exactly two bytes.
 */
inline void WriteUint16LittleEndian(const std::uint16_t InValue, std::uint8_t* const OutBytes) noexcept
{
	OutBytes[0] = static_cast<std::uint8_t>(InValue & LowByteMask);
	OutBytes[1] = static_cast<std::uint8_t>(InValue >> HighByteShift);
}

} // namespace MicroWorld
