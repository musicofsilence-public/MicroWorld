#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineRegistryView.h>

#include <array>
#include <cstddef>

namespace MicroWorld::Engine
{

/**
 * Motivation: Owns one world's fixed-capacity actor registry outside UWorld so its several hundred bytes are not charged
 *   to every equal-width object-store slot.
 * Responsibilities: Outlive the world that consumes the one-shot reference from MakeReference, keep a stable address, and
 *   never be copied or moved once that address has escaped into managed-object state.
 * Example:
 *   FWorldActorRegistry<4> Registry;
 *   FWorldActorRegistryReference Ref = Registry.MakeReference();
 */
template<std::size_t MaxActors>
class FWorldActorRegistry final
{
public:
	/**
	 * Motivation: Preserves the stable address a registry reference retains for the world's lifetime.
	 * Responsibilities: Default-construct the storage without rebinding it to any world yet.
	 */
	FWorldActorRegistry() noexcept = default;

	/**
	 * Motivation: Prevents two registry owners from sharing one array behind a stable address.
	 * Responsibilities: Reject copy construction so registry storage stays single-owner.
	 */
	FWorldActorRegistry(const FWorldActorRegistry&) = delete;

	/**
	 * Motivation: Prevents replacing registry storage behind a world through assignment.
	 * Responsibilities: Reject copy assignment so registry storage stays single-owner.
	 */
	FWorldActorRegistry& operator=(const FWorldActorRegistry&) = delete;

	/**
	 * Motivation: Prevents moving registry storage after a reference may have escaped into managed state.
	 * Responsibilities: Reject move construction so the registry address stays put.
	 */
	FWorldActorRegistry(FWorldActorRegistry&&) = delete;

	/**
	 * Motivation: Prevents replacing registry storage behind a world through move assignment.
	 * Responsibilities: Reject move assignment so the registry address stays put.
	 */
	FWorldActorRegistry& operator=(FWorldActorRegistry&&) = delete;

	/**
	 * Motivation: Transfers the only reference that may mutate this registry to one world, exactly once.
	 * Responsibilities: Return a valid reference on the first call and an empty one thereafter, leaving the registry
	 *   unshared across worlds.
	 */
	FWorldActorRegistryReference MakeReference() & noexcept
	{
		if (bReferenceMade)
		{
			return {};
		}
		bReferenceMade = true;
		return FWorldActorRegistryReference{
			Actors.data(), MaxActors, Count, PendingSpawn.data(), PendingSpawnCount, PendingDestroy.data(), PendingDestroyCount};
	}

	/**
	 * Motivation: Prevents a view from outliving a temporary registry owner.
	 * Responsibilities: Reject MakeReference on an rvalue so no escaped reference survives its owner.
	 */
	FWorldActorRegistryReference MakeReference() && = delete;

	/**
	 * Motivation: Lets a caller report registration occupancy without exposing mutable storage.
	 * Responsibilities: Return the current registered actor count.
	 */
	std::size_t GetCount() const noexcept { return Count; }

	/**
	 * Motivation: Lets a caller test registration against the fixed limit without magic numbers.
	 * Responsibilities: Report the compile-time upper bound on registered actors.
	 */
	static constexpr std::size_t GetCapacity() noexcept { return MaxActors; }

private:
	/** Motivation: Holds traced actor references without exposing post-begin mutation. */
	std::array<TObjectPtr<AActor>, MaxActors> Actors{};

	/** Motivation: Records the number of entries published only through the owning world. */
	std::size_t Count{0};

	/** Motivation: Holds actors queued for begin at the next deferred barrier. */
	std::array<TObjectPtr<AActor>, MaxActors> PendingSpawn{};

	/** Motivation: Records the number of queued-spawn entries advanced only by the owning world. */
	std::size_t PendingSpawnCount{0};

	/** Motivation: Holds registered actors queued for end and release at the next deferred barrier. */
	std::array<TObjectPtr<AActor>, MaxActors> PendingDestroy{};

	/** Motivation: Records the number of queued-destroy entries advanced only by the owning world. */
	std::size_t PendingDestroyCount{0};

	/** Motivation: Ensures this storage cannot be shared or rebound to a second world. */
	bool bReferenceMade{false};
};

} // namespace MicroWorld::Engine
