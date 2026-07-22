#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Opaque fixed-size transport address shared by every Net driver.
 *
 * Holds up to `MaxBytes` driver-defined bytes plus an active length, so one type
 * spans a 6-byte UDP IPv4+port, a 1-2 byte LoRa node id, and a 1-byte loopback
 * port index without allocating. The address ascribes no meaning to the bytes;
 * each driver documents the encoding it writes and reads.
 */
struct FNetAddress
{
	/** Maximum address bytes any driver may store; sized for IPv4+port with headroom. */
	static constexpr std::size_t MaxBytes = 12;

	/** Driver-defined address bytes; only the leading `Size` bytes are meaningful. */
	std::array<std::uint8_t, MaxBytes> Bytes{};

	/** Count of meaningful leading bytes in `Bytes`; the remaining bytes are unspecified. */
	std::uint8_t Size{0};
};

/** Compares two addresses by active length and the leading `Size` bytes only. */
constexpr bool operator==(const FNetAddress& Left, const FNetAddress& Right) noexcept
{
	if (Left.Size != Right.Size)
	{
		return false;
	}
	for (std::size_t Index = 0; Index < Left.Size; ++Index)
	{
		if (Left.Bytes[Index] != Right.Bytes[Index])
		{
			return false;
		}
	}
	return true;
}

/** Negates `operator==` so callers can test address inequality directly. */
constexpr bool operator!=(const FNetAddress& Left, const FNetAddress& Right) noexcept
{
	return !(Left == Right);
}

/** Builds a 1-byte loopback address whose single byte is the destination port index. */
constexpr FNetAddress MakeLoopbackAddress(const std::uint8_t PortIndex) noexcept
{
	FNetAddress Address{};
	Address.Bytes[0] = PortIndex;
	Address.Size = 1;
	return Address;
}

} // namespace MicroWorld
