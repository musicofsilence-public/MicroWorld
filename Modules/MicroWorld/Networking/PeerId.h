#pragma once

#include <cstdint>

namespace MicroWorld::Networking
{

/**
 * Motivation: Names one live role-relative connection without exposing its Messaging route.
 * Responsibilities: Pair a fixed registry slot with a non-wrapping generation and reserve an explicit invalid value.
 * Example: if (Peer.IsValid()) { Network.SendTo(Peer, Channel, Message); }
 */
struct FPeerId final
{
	/** Motivation: Reserves the slot value that never names a connection. */
	static constexpr std::uint8_t InvalidIndex = 0xFFu;

	/** Motivation: Identifies the server registry slot or client server-session slot. */
	std::uint8_t Index{InvalidIndex};

	/** Motivation: Invalidates handles from an earlier occupant of the same slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets call sites reject the default identity before issuing a Network command.
	 * Responsibilities: Report whether this id has a non-sentinel slot index.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/**
 * Motivation: Lets route validation compare the full connection identity.
 * Responsibilities: Return true only for matching slot and generation.
 */
constexpr bool operator==(const FPeerId InLeft, const FPeerId InRight) noexcept
{
	return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
}

/**
 * Motivation: Lets route validation reject a mismatched connection identity directly.
 * Responsibilities: Negate complete peer equality.
 */
constexpr bool operator!=(const FPeerId InLeft, const FPeerId InRight) noexcept { return !(InLeft == InRight); }

} // namespace MicroWorld::Networking
