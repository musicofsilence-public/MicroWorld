#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetResult.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld
{

/**
 * Reads bytes from a caller-owned fixed buffer without allocating or throwing.
 *
 * The reader observes one caller-owned `TSpan<const std::uint8_t>` and tracks a
 * read cursor. A source bound to `{nullptr, nonzero}` is an invalid
 * configuration: every consuming operation returns `Invalid` without
 * dereferencing null, and a failed read never advances the cursor or modifies
 * output parameters.
 */
class FByteReader final
{
public:
	/** Binds the reader to a caller-owned source view; storage is observed, never owned. */
	constexpr explicit FByteReader(TSpan<const std::uint8_t> InSource) noexcept : Buffer(InSource), ReadPosition(0) {}

	/** Prevents the reader from being copied while caller storage has one observer. */
	FByteReader(const FByteReader&) = delete;

	/** Prevents two readers from advancing independent cursors over one source. */
	FByteReader& operator=(const FByteReader&) = delete;

	/** Lets a caller move a reader that no other reference observes, preserving its cursor. */
	constexpr FByteReader(FByteReader&& Other) noexcept : Buffer(Other.Buffer), ReadPosition(Other.ReadPosition) { Other.ReadPosition = 0; }

	/** Lets a caller move-assign a reader that no other reference observes, preserving its cursor. */
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

	/** Defaulted so a reader with automatic storage destructs without side effects. */
	~FByteReader() noexcept = default;

	/**
	 * Reads one byte into `OutValue` and advances the cursor.
	 * Returns `Invalid` without modifying `OutValue` when the source is an invalid
	 * `{nullptr, nonzero}` view or when no byte remains to satisfy the request.
	 */
	ENetResult ReadByte(std::uint8_t& OutValue) noexcept
	{
		if (!HasValidStorage() || ReadPosition >= Buffer.Size())
		{
			return ENetResult::Invalid;
		}
		OutValue = Buffer[ReadPosition];
		++ReadPosition;
		return ENetResult::Success;
	}

	/**
	 * Reads `InDestination.Size()` bytes into the caller-owned destination and advances.
	 * An empty destination is a valid no-op. A null destination with nonzero length
	 * returns `Invalid`. A destination larger than the remaining source returns
	 * `Invalid` (a truncated request) without modifying the destination or advancing.
	 * A source bound to `{nullptr, nonzero}` returns `Invalid` for any nonzero read.
	 */
	ENetResult Read(TSpan<std::uint8_t> InDestination) noexcept
	{
		const std::size_t RequestedSize = InDestination.Size();
		if (RequestedSize == 0)
		{
			return ENetResult::Success;
		}
		if (InDestination.Data() == nullptr)
		{
			return ENetResult::Invalid;
		}
		if (!HasValidStorage())
		{
			return ENetResult::Invalid;
		}
		if (RequestedSize > Buffer.Size() - ReadPosition)
		{
			// The request exceeds the remaining source; treat it as a truncated request.
			return ENetResult::Invalid;
		}
		std::memcpy(InDestination.Data(), Buffer.Data() + ReadPosition, RequestedSize);
		ReadPosition += RequestedSize;
		return ENetResult::Success;
	}

	/**
	 * Observes the next byte without advancing the cursor.
	 * Returns `Invalid` without modifying `OutValue` when the source is an invalid
	 * `{nullptr, nonzero}` view or when no byte remains to observe.
	 */
	ENetResult PeekByte(std::uint8_t& OutValue) const noexcept
	{
		if (!HasValidStorage() || ReadPosition >= Buffer.Size())
		{
			return ENetResult::Invalid;
		}
		OutValue = Buffer[ReadPosition];
		return ENetResult::Success;
	}

	/** Reports the caller-owned source length observed at construction. */
	constexpr std::size_t Capacity() const noexcept { return Buffer.Size(); }

	/** Reports how many bytes have been consumed and survive a failed read. */
	constexpr std::size_t Position() const noexcept { return ReadPosition; }

	/** Reports how many more bytes a valid reader can return before the next `Invalid`. */
	constexpr std::size_t Remaining() const noexcept { return Buffer.Size() - ReadPosition; }

	/**
	 * Returns a read-only view of the bytes that have not yet been consumed.
	 * The view is empty whenever the backing data pointer is null — both the
	 * valid `{nullptr, 0}` empty source and the invalid `{nullptr, nonzero}`
	 * source — so no caller performs pointer arithmetic on null.
	 */
	constexpr TSpan<const std::uint8_t> RemainingBytes() const noexcept
	{
		if (Buffer.Data() == nullptr)
		{
			// A null data pointer has no honest base address for the suffix view,
			// whether the source is the valid empty `{nullptr, 0}` or an invalid `{nullptr, nonzero}`.
			return TSpan<const std::uint8_t>(nullptr, 0);
		}
		return TSpan<const std::uint8_t>(Buffer.Data() + ReadPosition, Buffer.Size() - ReadPosition);
	}

	/** Resets the cursor to zero so the caller-owned source can be re-parsed. */
	constexpr void Reset() noexcept { ReadPosition = 0; }

private:
	/**
	 * Reports whether the backing source can honestly provide bytes.
	 * A `{nullptr, 0}` view is a valid empty source; a `{nullptr, nonzero}` view is an invalid
	 * configuration that must never be dereferenced.
	 */
	constexpr bool HasValidStorage() const noexcept { return Buffer.Data() != nullptr || Buffer.Size() == 0; }

	/** Observes the caller-owned source; the reader never releases or grows it. */
	TSpan<const std::uint8_t> Buffer;

	/** Tracks the consumed prefix length so failures leave the cursor intact. */
	std::size_t ReadPosition;
};

/**
 * Reads two big-endian bytes (most significant byte first) as a 16-bit value.
 *
 * @param InBytes Caller-owned source of exactly two bytes.
 * @return The decoded 16-bit value.
 */
inline std::uint16_t ReadUint16BigEndian(const std::uint8_t* const InBytes) noexcept
{
	return static_cast<std::uint16_t>((static_cast<std::uint16_t>(InBytes[0]) << 8) | static_cast<std::uint16_t>(InBytes[1]));
}

/**
 * Reads two little-endian bytes (least significant byte first) as a 16-bit value.
 *
 * @param InBytes Caller-owned source of exactly two bytes.
 * @return The decoded 16-bit value.
 */
inline std::uint16_t ReadUint16LittleEndian(const std::uint8_t* const InBytes) noexcept
{
	return static_cast<std::uint16_t>(
		static_cast<std::uint16_t>(InBytes[0]) | static_cast<std::uint16_t>(static_cast<std::uint16_t>(InBytes[1]) << 8));
}

} // namespace MicroWorld
