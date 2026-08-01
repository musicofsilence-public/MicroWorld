#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every transport device one opaque fixed-size address value without per-device allocation.
 * Responsibilities: Hold device-defined bytes and an active length without assigning meaning to their encoding.
 * Example:
 *   FDeviceAddress Address = MakeLoopbackAddress(0);
 *   if (Address == Peer) { Deliver(); }
 */
struct FDeviceAddress
{
	/** Motivation: Sizes fixed address storage for the largest currently supported device address. */
	static constexpr std::size_t MaxBytes = 12;

	/** Motivation: Holds device-defined address bytes where only the leading Size bytes are meaningful. */
	std::array<std::uint8_t, MaxBytes> Bytes{};

	/** Motivation: Counts meaningful leading bytes so empty represents the broadcast or default route. */
	std::uint8_t Size{0};
};

/**
 * Motivation: Lets transport peers compare addresses by their meaningful content.
 * Responsibilities: Return true only when the active lengths and all active leading bytes match.
 * Example:
 *   if (Destination == Source) { Deliver(); }
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
 * Motivation: Lets callers test address inequality directly.
 * Responsibilities: Return the negation of address equality.
 * Example:
 *   if (Destination != Source) { Forward(); }
 */
constexpr bool operator!=(const FDeviceAddress& InLeft, const FDeviceAddress& InRight) noexcept
{
	return !(InLeft == InRight);
}

/**
 * Motivation: Provides the canonical address for broadcast or a device's default route.
 * Responsibilities: Return an empty address whose active length is zero.
 * Example:
 *   Device.TrySend(MakeBroadcastAddress(), Packet);
 */
constexpr FDeviceAddress MakeBroadcastAddress() noexcept
{
	return FDeviceAddress{};
}

/**
 * Motivation: Builds a one-byte loopback address for routing by port index.
 * Responsibilities: Store InPortIndex in the first byte and set the active length to one.
 * Example:
 *   FDeviceAddress Address = MakeLoopbackAddress(1);
 */
constexpr FDeviceAddress MakeLoopbackAddress(const std::uint8_t InPortIndex) noexcept
{
	FDeviceAddress Address{};
	Address.Bytes[0] = InPortIndex;
	Address.Size = 1;
	return Address;
}

} // namespace MicroWorld::Core
