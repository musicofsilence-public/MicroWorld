#pragma once

#include <MicroWorld/Engine/ObjectPtr.h>

#include <cstddef>

namespace MicroWorld
{

class AActor;
class UWorld;
template<std::size_t MaxActors>
class FWorldActorRegistry;

/**
 * Move-only reference over one caller-owned fixed actor registry.
 *
 * Only FWorldActorRegistry can create a reference, and only UWorld can inspect or
 * mutate it. This prevents forged views, shared mutable storage, and caller
 * mutation after lifecycle dispatch begins.
 */
class FWorldActorRegistryReference final
{
public:
	/** Transfers the only usable reference and invalidates the source. */
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

	/** Prevents two worlds from sharing one mutable registry reference. */
	FWorldActorRegistryReference(const FWorldActorRegistryReference&) = delete;

	/** Prevents rebinding a world's registry after construction. */
	FWorldActorRegistryReference& operator=(const FWorldActorRegistryReference&) = delete;

	/** Prevents rebinding a world's registry after construction. */
	FWorldActorRegistryReference& operator=(FWorldActorRegistryReference&&) = delete;

private:
	// UWorld reads and mutates its own actor registry only through this reference.
	friend class UWorld;

	// The owning fixed registry is the only type that can mint a valid reference over its storage.
	template<std::size_t>
	friend class FWorldActorRegistry;

	/** Creates an invalid reference when registry storage has already been claimed. */
	FWorldActorRegistryReference() noexcept = default;

	/** Creates one validated reference from its owning fixed registry and pending lists. */
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

	/** Reports whether this reference still identifies one fixed registry and its pending lists. */
	bool IsValid() const noexcept
	{
		const bool bHandlesPresent = Count != nullptr && PendingSpawnCount != nullptr && PendingDestroyCount != nullptr;
		const bool bArraysPresent = Capacity == 0 || (Actors != nullptr && PendingSpawn != nullptr && PendingDestroy != nullptr);
		const bool bCountsBounded = bHandlesPresent && *Count <= Capacity && *PendingSpawnCount <= Capacity && *PendingDestroyCount <= Capacity;
		return bHandlesPresent && bArraysPresent && bCountsBounded;
	}

	/** Returns the maximum number of actors accepted by this registry. */
	std::size_t GetCapacity() const noexcept { return Capacity; }

	/** Returns the number of actors registered by the owning world. */
	std::size_t GetCount() const noexcept { return Count != nullptr ? *Count : 0; }

	/** Returns one registered actor reference by validated internal index. */
	const TObjectPtr<AActor>& At(const std::size_t InIndex) const noexcept { return Actors[InIndex]; }

	/** Publishes one validated actor and advances the private live count. */
	void Add(const TObjectPtr<AActor> InActor) noexcept
	{
		Actors[*Count] = InActor;
		++*Count;
	}

	/** Removes the live actor at Index and shifts later survivors left, preserving order. */
	void RemoveAt(const std::size_t InIndex) noexcept
	{
		for (std::size_t Slot = InIndex + 1; Slot < *Count; ++Slot)
		{
			Actors[Slot - 1] = Actors[Slot];
		}
		--*Count;
		Actors[*Count] = TObjectPtr<AActor>{};
	}

	/** Returns the number of actors queued to begin at the next deferred barrier. */
	std::size_t GetPendingSpawnCount() const noexcept { return PendingSpawnCount != nullptr ? *PendingSpawnCount : 0; }

	/** Returns one queued-spawn actor reference by validated internal index. */
	const TObjectPtr<AActor>& PendingSpawnAt(const std::size_t InIndex) const noexcept { return PendingSpawn[InIndex]; }

	/** Appends one actor to the bounded pending-spawn list. */
	void AddPendingSpawn(const TObjectPtr<AActor> InActor) noexcept
	{
		PendingSpawn[*PendingSpawnCount] = InActor;
		++*PendingSpawnCount;
	}

	/** Drops every pending-spawn entry after the barrier has begun them. */
	void ClearPendingSpawn() noexcept
	{
		for (std::size_t Slot = 0; Slot < *PendingSpawnCount; ++Slot)
		{
			PendingSpawn[Slot] = TObjectPtr<AActor>{};
		}
		*PendingSpawnCount = 0;
	}

	/** Returns the number of actors queued to end and release at the next deferred barrier. */
	std::size_t GetPendingDestroyCount() const noexcept { return PendingDestroyCount != nullptr ? *PendingDestroyCount : 0; }

	/** Returns one queued-destroy actor reference by validated internal index. */
	const TObjectPtr<AActor>& PendingDestroyAt(const std::size_t InIndex) const noexcept { return PendingDestroy[InIndex]; }

	/** Appends one actor to the bounded pending-destroy list. */
	void AddPendingDestroy(const TObjectPtr<AActor> InActor) noexcept
	{
		PendingDestroy[*PendingDestroyCount] = InActor;
		++*PendingDestroyCount;
	}

	/** Drops every pending-destroy entry after the barrier has ended them. */
	void ClearPendingDestroy() noexcept
	{
		for (std::size_t Slot = 0; Slot < *PendingDestroyCount; ++Slot)
		{
			PendingDestroy[Slot] = TObjectPtr<AActor>{};
		}
		*PendingDestroyCount = 0;
	}

	/** Points at the private caller-owned actor array. */
	TObjectPtr<AActor>* Actors{nullptr};

	/** Records the immutable capacity shared by the actor array and both pending lists. */
	std::size_t Capacity{0};

	/** Points at the private caller-owned live count advanced only by UWorld. */
	std::size_t* Count{nullptr};

	/** Points at the private caller-owned pending-spawn array filled only by UWorld. */
	TObjectPtr<AActor>* PendingSpawn{nullptr};

	/** Points at the private caller-owned pending-spawn count advanced only by UWorld. */
	std::size_t* PendingSpawnCount{nullptr};

	/** Points at the private caller-owned pending-destroy array filled only by UWorld. */
	TObjectPtr<AActor>* PendingDestroy{nullptr};

	/** Points at the private caller-owned pending-destroy count advanced only by UWorld. */
	std::size_t* PendingDestroyCount{nullptr};
};

} // namespace MicroWorld
