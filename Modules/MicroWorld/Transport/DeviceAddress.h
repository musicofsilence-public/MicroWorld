#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport::Address
{

/**
 * Motivation: Gives every Transport device one opaque fixed-size address type so a UDP endpoint, a LoRa node id,
 *   and a loopback port index all flow through the same interface without per-device allocation.
 * Responsibilities: Hold up to MaxBytes device-defined bytes plus an active length, and ascribe no meaning to the
 *   bytes so each device owns its own encoding.
 * Example:
 *   FDeviceAddress A = MakeLoopbackAddress(0);
 *   if (A == Other) { Route(); }
 */
struct FDeviceAddress
{
	/** Motivation: Sizes the fixed storage so a 6-byte UDP IPv4+port address fits with headroom for other devices. */
	static constexpr std::size_t MaxBytes = 12;

	/** Motivation: Holds device-defined address bytes where only the leading Size are meaningful. */
	std::array<std::uint8_t, MaxBytes> Bytes{};

	/** Motivation: Counts meaningful leading bytes so trailing storage stays unspecified. */
	std::uint8_t Size{0};
};

/**
 * Motivation: Lets containers and peers compare two addresses by complete meaningful content.
 * Responsibilities: Return true only when both the active length and every leading byte match.
 */
constexpr bool operator==(const FDeviceAddress& InLeft, const FDeviceAddress& InRight) noexcept
{
	if (InLeft.Size != InRight.Size)
	{
		return false;
	}
	for (std::size_t Index = 0; Index < InLeft.Size; ++Index)
	{
		if (InLeft.Bytes[Index] != InRight.Bytes[Index])
		{
			return false;
		}
	}
	return true;
}

/**
 * Motivation: Lets callers test address inequality directly rather than negating operator== by hand.
 * Responsibilities: Return the negation of operator==.
 */
constexpr bool operator!=(const FDeviceAddress& InLeft, const FDeviceAddress& InRight) noexcept
{
	return !(InLeft == InRight);
}

/**
 * Motivation: Builds a 1-byte loopback address so an in-process network can route by port index.
 * Responsibilities: Stamp the port index into the first byte and set the active length to one.
 */
constexpr FDeviceAddress MakeLoopbackAddress(const std::uint8_t InPortIndex) noexcept
{
	FDeviceAddress Address{};
	Address.Bytes[0] = InPortIndex;
	Address.Size = 1;
	return Address;
}

} // namespace MicroWorld::Transport::Address
