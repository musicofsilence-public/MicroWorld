#pragma once

#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Carries one peer's generation-checked identity so a reused slot never answers to a stale id.
 * Responsibilities: Pair a slot index with a generation and report validity; a handle is local to the host that issued it.
 * Example:
 *   FPeerId Peer = Host.GetServerPeer();
 *   if (Peer.IsValid()) { Host.SendTo(Peer, Channel, Payload); }
 */
struct FPeerId
{
	/** Motivation: Reserves the index that names no peer so the default identity is deliberately invalid. */
	static constexpr std::uint8_t InvalidIndex = 0xFF;

	/** Motivation: Holds the peer slot index, or InvalidIndex; the host also reserves 0xFE for a local peer. */
	std::uint8_t Index{InvalidIndex};

	/** Motivation: Holds the slot generation at issue time; a later eviction bumps it so this id goes stale. */
	std::uint8_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its host.
	 * Responsibilities: Report whether the identity names a routable peer rather than the invalid default.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/**
 * Motivation: Lets containers compare two peer ids by complete stable identity.
 * Responsibilities: Return true only when both index and generation match.
 */
constexpr bool operator==(const FPeerId InLeft, const FPeerId InRight) noexcept
{
	return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
}

/**
 * Motivation: Lets callers test peer inequality directly rather than negating operator== by hand.
 * Responsibilities: Return the negation of operator==.
 */
constexpr bool operator!=(const FPeerId InLeft, const FPeerId InRight) noexcept
{
	return !(InLeft == InRight);
}

} // namespace MicroWorld::Transport
