#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorComponent.h>
#include <MicroWorld/Engine/EngineRegistryView.h>

#include <array>
#include <cstddef>

namespace MicroWorld
{

/**
 * Owns one actor's fixed-capacity component registry.
 *
 * The private array and count must outlive the actor that consumes the one-shot
 * lease returned by MakeView. Registry storage cannot be copied or moved after
 * its address becomes part of managed-object state.
 */
template<std::size_t MaxComponents>
class FActorComponentRegistry final
{
public:
	/** Preserves the stable address retained by a registry lease. */
	FActorComponentRegistry() noexcept = default;

	/** Prevents two registry owners from sharing one array. */
	FActorComponentRegistry(const FActorComponentRegistry&) = delete;

	/** Prevents replacing registry storage behind an actor. */
	FActorComponentRegistry& operator=(const FActorComponentRegistry&) = delete;

	/** Prevents moving registry storage after a lease may have escaped. */
	FActorComponentRegistry(FActorComponentRegistry&&) = delete;

	/** Prevents replacing registry storage behind an actor. */
	FActorComponentRegistry& operator=(FActorComponentRegistry&&) = delete;

	/** Transfers the only lease that may mutate this registry to one actor. */
	FActorComponentRegistryBase MakeView() & noexcept
	{
		if (bLeaseIssued)
		{
			return {};
		}
		bLeaseIssued = true;
		return FActorComponentRegistryBase{Components.data(), MaxComponents, Count};
	}

	/** Prevents a view from outliving a temporary registry owner. */
	FActorComponentRegistryBase MakeView() && = delete;

	/** Reports registration occupancy without exposing mutable storage. */
	std::size_t GetCount() const noexcept { return Count; }

	/** Reports the immutable registration capacity. */
	static constexpr std::size_t GetCapacity() noexcept { return MaxComponents; }

private:
	/** Holds traced component references without exposing post-begin mutation. */
	std::array<TObjectPtr<UActorComponent>, MaxComponents> Components{};

	/** Records the number of entries published only through the owning actor. */
	std::size_t Count{0};

	/** Ensures this storage cannot be shared or rebound to a second actor. */
	bool bLeaseIssued{false};
};

/**
 * Owns one world's fixed-capacity actor registry.
 *
 * The private array and count must outlive the world that consumes the one-shot
 * lease returned by MakeView. Registry storage cannot be copied or moved after
 * its address becomes part of managed-object state.
 */
template<std::size_t MaxActors>
class FWorldActorRegistry final
{
public:
	/** Preserves the stable address retained by a registry lease. */
	FWorldActorRegistry() noexcept = default;

	/** Prevents two registry owners from sharing one array. */
	FWorldActorRegistry(const FWorldActorRegistry&) = delete;

	/** Prevents replacing registry storage behind a world. */
	FWorldActorRegistry& operator=(const FWorldActorRegistry&) = delete;

	/** Prevents moving registry storage after a lease may have escaped. */
	FWorldActorRegistry(FWorldActorRegistry&&) = delete;

	/** Prevents replacing registry storage behind a world. */
	FWorldActorRegistry& operator=(FWorldActorRegistry&&) = delete;

	/** Transfers the only lease that may mutate this registry to one world. */
	FWorldActorRegistryBase MakeView() & noexcept
	{
		if (bLeaseIssued)
		{
			return {};
		}
		bLeaseIssued = true;
		return FWorldActorRegistryBase{
			Actors.data(), MaxActors, Count, PendingSpawn.data(), PendingSpawnCount, PendingDestroy.data(), PendingDestroyCount};
	}

	/** Prevents a view from outliving a temporary registry owner. */
	FWorldActorRegistryBase MakeView() && = delete;

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
	bool bLeaseIssued{false};
};

} // namespace MicroWorld
