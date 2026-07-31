#pragma once

#include <MicroWorld/Engine/ObjectPtr.h>

#include <cstddef>

namespace MicroWorld::Engine
{

class AActor;
class UWorld;
template<std::size_t MaxActors>
class FWorldActorRegistry;

/**
 * Motivation: Lets one world reach a fixed actor registry's storage through a move-only reference so the storage cannot be
 *   forged, shared, or mutated by a caller after lifecycle dispatch begins.
 * Responsibilities: Carry pointers into one caller-owned registry and its pending lists, validate before access, and be
 *   creatable only by the owning FWorldActorRegistry and inspectable only by UWorld.
 * Example:
 *   FWorldActorRegistryReference Ref = Registry.MakeReference();
 *   if (Ref.IsValid()) { Ref.Add(Actor); }
 */
class FWorldActorRegistryReference final
{
public:
	/**
	 * Motivation: Transfers the only usable reference to a registry and invalidates the source.
	 * Responsibilities: Move every pointer and leave the source empty.
	 */
	FWorldActorRegistryReference(FWorldActorRegistryReference&& Other) noexcept
		: Actors(Other.Actors)
		, Capacity(Other.Capacity)
		, Count(Other.Count)
		, PendingSpawn(Other.PendingSpawn)
		, PendingSpawnCount(Other.PendingSpawnCount)
		, PendingDestroy(Other.PendingDestroy)
		, PendingDestroyCount(Other.PendingDestroyCount)
	{
		Other.Actors = nullptr;
		Other.Capacity = 0;
		Other.Count = nullptr;
		Other.PendingSpawn = nullptr;
		Other.PendingSpawnCount = nullptr;
		Other.PendingDestroy = nullptr;
		Other.PendingDestroyCount = nullptr;
	}

	/**
	 * Motivation: Prevents two worlds from sharing one mutable registry reference.
	 * Responsibilities: Reject copy construction so each registry has one holder.
	 */
	FWorldActorRegistryReference(const FWorldActorRegistryReference&) = delete;

	/**
	 * Motivation: Prevents rebinding a world's registry after construction.
	 * Responsibilities: Reject copy assignment so the registry binding never changes.
	 */
	FWorldActorRegistryReference& operator=(const FWorldActorRegistryReference&) = delete;

	/**
	 * Motivation: Prevents rebinding a world's registry after construction via move.
	 * Responsibilities: Reject move assignment so the registry binding never changes.
	 */
	FWorldActorRegistryReference& operator=(FWorldActorRegistryReference&&) = delete;

private:
	// UWorld reads and mutates its own actor registry only through this reference.
	friend class UWorld;

	// The owning fixed registry is the only type that can mint a valid reference over its storage.
	template<std::size_t>
	friend class FWorldActorRegistry;

	/**
	 * Motivation: Creates an invalid reference when registry storage has already been claimed or for an empty holder.
	 * Responsibilities: Produce a reference that reports invalid on every query.
	 */
	FWorldActorRegistryReference() noexcept = default;

	/**
	 * Motivation: Lets the owning fixed registry mint one validated reference over its storage and pending lists.
	 * Responsibilities: Bind all pointers and counts the registry validated without re-validating.
	 */
	FWorldActorRegistryReference(
		TObjectPtr<AActor>* InActors,
		const std::size_t InCapacity,
		std::size_t& InCount,
		TObjectPtr<AActor>* InPendingSpawn,
		std::size_t& InPendingSpawnCount,
		TObjectPtr<AActor>* InPendingDestroy,
		std::size_t& InPendingDestroyCount) noexcept
		: Actors(InActors)
		, Capacity(InCapacity)
		, Count(&InCount)
		, PendingSpawn(InPendingSpawn)
		, PendingSpawnCount(&InPendingSpawnCount)
		, PendingDestroy(InPendingDestroy)
		, PendingDestroyCount(&InPendingDestroyCount)
	{
	}

	/**
	 * Motivation: Lets UWorld confirm a reference still identifies one fixed registry and its pending lists before use.
	 * Responsibilities: Report true only when all handles and arrays are present and every count is bounded by capacity.
	 */
	bool IsValid() const noexcept
	{
		const bool bHandlesPresent = Count != nullptr && PendingSpawnCount != nullptr && PendingDestroyCount != nullptr;
		const bool bArraysPresent = Capacity == 0 || (Actors != nullptr && PendingSpawn != nullptr && PendingDestroy != nullptr);
		const bool bCountsBounded = bHandlesPresent && *Count <= Capacity && *PendingSpawnCount <= Capacity && *PendingDestroyCount <= Capacity;
		return bHandlesPresent && bArraysPresent && bCountsBounded;
	}

	/**
	 * Motivation: Lets UWorld read the maximum number of actors accepted by this registry.
	 * Responsibilities: Return the capacity the registry validated at mint time.
	 */
	std::size_t GetCapacity() const noexcept { return Capacity; }

	/**
	 * Motivation: Lets UWorld read how many actors the owning world has registered.
	 * Responsibilities: Return the live count or zero for an empty reference.
	 */
	std::size_t GetCount() const noexcept { return Count != nullptr ? *Count : 0; }

	/**
	 * Motivation: Lets UWorld read one registered actor reference by its validated internal index.
	 * Responsibilities: Return the actor at the index with no bounds re-check beyond the registry's validation.
	 */
	const TObjectPtr<AActor>& At(const std::size_t InIndex) const noexcept { return Actors[InIndex]; }

	/**
	 * Motivation: Lets UWorld publish one validated actor into the registry.
	 * Responsibilities: Store the actor at the live count and advance the count exactly once.
	 */
	void Add(const TObjectPtr<AActor> InActor) noexcept
	{
		Actors[*Count] = InActor;
		++*Count;
	}

	/**
	 * Motivation: Lets UWorld remove the live actor at an index without disturbing relative order.
	 * Responsibilities: Shift later survivors left, advance the count down, and clear the freed slot.
	 */
	void RemoveAt(const std::size_t InIndex) noexcept
	{
		for (std::size_t Slot = InIndex + 1; Slot < *Count; ++Slot)
		{
			Actors[Slot - 1] = Actors[Slot];
		}
		--*Count;
		Actors[*Count] = TObjectPtr<AActor>{};
	}

	/**
	 * Motivation: Lets UWorld read how many actors are queued to begin at the next deferred barrier.
	 * Responsibilities: Return the pending-spawn count or zero for an empty reference.
	 */
	std::size_t GetPendingSpawnCount() const noexcept { return PendingSpawnCount != nullptr ? *PendingSpawnCount : 0; }

	/**
	 * Motivation: Lets UWorld read one queued-spawn actor reference by its validated internal index.
	 * Responsibilities: Return the pending-spawn actor at the index without re-checking bounds.
	 */
	const TObjectPtr<AActor>& PendingSpawnAt(const std::size_t InIndex) const noexcept { return PendingSpawn[InIndex]; }

	/**
	 * Motivation: Lets UWorld append one actor to the bounded pending-spawn list.
	 * Responsibilities: Store the actor at the pending-spawn count and advance it exactly once.
	 */
	void AddPendingSpawn(const TObjectPtr<AActor> InActor) noexcept
	{
		PendingSpawn[*PendingSpawnCount] = InActor;
		++*PendingSpawnCount;
	}

	/**
	 * Motivation: Lets UWorld drop every pending-spawn entry after the barrier has begun them.
	 * Responsibilities: Clear each slot and reset the pending-spawn count to zero.
	 */
	void ClearPendingSpawn() noexcept
	{
		for (std::size_t Slot = 0; Slot < *PendingSpawnCount; ++Slot)
		{
			PendingSpawn[Slot] = TObjectPtr<AActor>{};
		}
		*PendingSpawnCount = 0;
	}

	/**
	 * Motivation: Lets UWorld read how many actors are queued to end and release at the next deferred barrier.
	 * Responsibilities: Return the pending-destroy count or zero for an empty reference.
	 */
	std::size_t GetPendingDestroyCount() const noexcept { return PendingDestroyCount != nullptr ? *PendingDestroyCount : 0; }

	/**
	 * Motivation: Lets UWorld read one queued-destroy actor reference by its validated internal index.
	 * Responsibilities: Return the pending-destroy actor at the index without re-checking bounds.
	 */
	const TObjectPtr<AActor>& PendingDestroyAt(const std::size_t InIndex) const noexcept { return PendingDestroy[InIndex]; }

	/**
	 * Motivation: Lets UWorld append one actor to the bounded pending-destroy list.
	 * Responsibilities: Store the actor at the pending-destroy count and advance it exactly once.
	 */
	void AddPendingDestroy(const TObjectPtr<AActor> InActor) noexcept
	{
		PendingDestroy[*PendingDestroyCount] = InActor;
		++*PendingDestroyCount;
	}

	/**
	 * Motivation: Lets UWorld drop every pending-destroy entry after the barrier has ended them.
	 * Responsibilities: Clear each slot and reset the pending-destroy count to zero.
	 */
	void ClearPendingDestroy() noexcept
	{
		for (std::size_t Slot = 0; Slot < *PendingDestroyCount; ++Slot)
		{
			PendingDestroy[Slot] = TObjectPtr<AActor>{};
		}
		*PendingDestroyCount = 0;
	}

	/** Motivation: Points at the private caller-owned actor array. */
	TObjectPtr<AActor>* Actors{nullptr};

	/** Motivation: Records the immutable capacity shared by the actor array and both pending lists. */
	std::size_t Capacity{0};

	/** Motivation: Points at the private caller-owned live count advanced only by UWorld. */
	std::size_t* Count{nullptr};

	/** Motivation: Points at the private caller-owned pending-spawn array filled only by UWorld. */
	TObjectPtr<AActor>* PendingSpawn{nullptr};

	/** Motivation: Points at the private caller-owned pending-spawn count advanced only by UWorld. */
	std::size_t* PendingSpawnCount{nullptr};

	/** Motivation: Points at the private caller-owned pending-destroy array filled only by UWorld. */
	TObjectPtr<AActor>* PendingDestroy{nullptr};

	/** Motivation: Points at the private caller-owned pending-destroy count advanced only by UWorld. */
	std::size_t* PendingDestroyCount{nullptr};
};

} // namespace MicroWorld::Engine
