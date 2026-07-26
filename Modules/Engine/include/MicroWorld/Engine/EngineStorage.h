#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineRegistryView.h>

#include <array>
#include <cstddef>

namespace MicroWorld
{

/**
 * Owns one world's fixed-capacity actor registry outside UWorld, so its several hundred
 * bytes are not charged to every equal-width object-store slot — the Engine README carries
 * the full accounting.
 *
 * The private array and count must outlive the world that consumes the one-shot
 * reference returned by MakeReference. Registry storage cannot be copied or moved after
 * its address becomes part of managed-object state.
 */
template<std::size_t MaxActors>
class FWorldActorRegistry final
{
public:
	/** Preserves the stable address retained by a registry reference. */
	FWorldActorRegistry() noexcept = default;

	/** Prevents two registry owners from sharing one array. */
	FWorldActorRegistry(const FWorldActorRegistry&) = delete;

	/** Prevents replacing registry storage behind a world. */
	FWorldActorRegistry& operator=(const FWorldActorRegistry&) = delete;

	/** Prevents moving registry storage after a reference may have escaped. */
	FWorldActorRegistry(FWorldActorRegistry&&) = delete;

	/** Prevents replacing registry storage behind a world. */
	FWorldActorRegistry& operator=(FWorldActorRegistry&&) = delete;

	/** Transfers the only reference that may mutate this registry to one world. */
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

	/** Prevents a view from outliving a temporary registry owner. */
	FWorldActorRegistryReference MakeReference() && = delete;

	/** Reports registration occupancy without exposing mutable storage. */
	std::size_t GetCount() const noexcept { return Count; }

	/** Reports the immutable registration capacity. */
	static constexpr std::size_t GetCapacity() noexcept { return MaxActors; }

private:
	/** Holds traced actor references without exposing post-begin mutation. */
	std::array<TObjectPtr<AActor>, MaxActors> Actors{};

	/** Records the number of entries published only through the owning world. */
	std::size_t Count{0};

	/** Holds actors queued for begin at the next deferred barrier. */
	std::array<TObjectPtr<AActor>, MaxActors> PendingSpawn{};

	/** Records the number of queued-spawn entries advanced only by the owning world. */
	std::size_t PendingSpawnCount{0};

	/** Holds registered actors queued for end and release at the next deferred barrier. */
	std::array<TObjectPtr<AActor>, MaxActors> PendingDestroy{};

	/** Records the number of queued-destroy entries advanced only by the owning world. */
	std::size_t PendingDestroyCount{0};

	/** Ensures this storage cannot be shared or rebound to a second world. */
	bool bReferenceMade{false};
};

} // namespace MicroWorld
