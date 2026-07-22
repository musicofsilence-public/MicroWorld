#pragma once

#include <MicroWorld/Object/ObjectPtr.h>

#include <cstddef>

namespace MicroWorld
{

class AActor;
class UActorComponent;
class UWorld;
template<std::size_t MaxComponents>
class FActorComponentRegistry;
template<std::size_t MaxActors>
class FWorldActorRegistry;

/**
 * Move-only lease over one caller-owned fixed component registry.
 *
 * Only FActorComponentRegistry can create a lease, and only AActor can inspect
 * or mutate it. This keeps the owning array and count inaccessible to callers
 * after construction while AActor remains a non-template type.
 */
class FActorComponentRegistryBase final
{
public:
	/** Transfers the only usable lease and invalidates the source. */
	FActorComponentRegistryBase(FActorComponentRegistryBase&& Other) noexcept
		: Components(Other.Components), Capacity(Other.Capacity), Count(Other.Count)
	{
		Other.Components = nullptr;
		Other.Capacity = 0;
		Other.Count = nullptr;
	}

	/** Prevents two actors from sharing one mutable registry lease. */
	FActorComponentRegistryBase(const FActorComponentRegistryBase&) = delete;

	/** Prevents rebinding an actor's registry after construction. */
	FActorComponentRegistryBase& operator=(const FActorComponentRegistryBase&) = delete;

	/** Prevents rebinding an actor's registry after construction. */
	FActorComponentRegistryBase& operator=(FActorComponentRegistryBase&&) = delete;

private:
	friend class AActor;
	template<std::size_t>
	friend class FActorComponentRegistry;

	/** Creates an invalid lease when registry storage has already been claimed. */
	FActorComponentRegistryBase() noexcept = default;

	/** Creates one validated lease from its owning fixed registry. */
	FActorComponentRegistryBase(TObjectPtr<UActorComponent>* InComponents, const std::size_t InCapacity, std::size_t& InCount) noexcept
		: Components(InComponents), Capacity(InCapacity), Count(&InCount)
	{
	}

	/** Reports whether this lease still identifies one fixed registry. */
	bool IsValid() const noexcept { return Count != nullptr && (Capacity == 0 || Components != nullptr) && *Count <= Capacity; }

	/** Returns the maximum number of components accepted by this registry. */
	std::size_t GetCapacity() const noexcept { return Capacity; }

	/** Returns the number of components registered by the owning actor. */
	std::size_t GetCount() const noexcept { return Count != nullptr ? *Count : 0; }

	/** Returns one registered component reference by validated internal index. */
	const TObjectPtr<UActorComponent>& At(const std::size_t Index) const noexcept { return Components[Index]; }

	/** Publishes one validated component and advances the private live count. */
	void Add(const TObjectPtr<UActorComponent> Component) noexcept
	{
		Components[*Count] = Component;
		++*Count;
	}

	/** Points at the private caller-owned component array. */
	TObjectPtr<UActorComponent>* Components{nullptr};

	/** Records the immutable capacity of the caller-owned component array. */
	std::size_t Capacity{0};

	/** Points at the private caller-owned live count advanced only by AActor. */
	std::size_t* Count{nullptr};
};

/**
 * Move-only lease over one caller-owned fixed actor registry.
 *
 * Only FWorldActorRegistry can create a lease, and only UWorld can inspect or
 * mutate it. This prevents forged views, shared mutable storage, and caller
 * mutation after lifecycle dispatch begins.
 */
class FWorldActorRegistryBase final
{
public:
	/** Transfers the only usable lease and invalidates the source. */
	FWorldActorRegistryBase(FWorldActorRegistryBase&& Other) noexcept
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

	/** Prevents two worlds from sharing one mutable registry lease. */
	FWorldActorRegistryBase(const FWorldActorRegistryBase&) = delete;

	/** Prevents rebinding a world's registry after construction. */
	FWorldActorRegistryBase& operator=(const FWorldActorRegistryBase&) = delete;

	/** Prevents rebinding a world's registry after construction. */
	FWorldActorRegistryBase& operator=(FWorldActorRegistryBase&&) = delete;

private:
	friend class UWorld;
	template<std::size_t>
	friend class FWorldActorRegistry;

	/** Creates an invalid lease when registry storage has already been claimed. */
	FWorldActorRegistryBase() noexcept = default;

	/** Creates one validated lease from its owning fixed registry and pending lists. */
	FWorldActorRegistryBase(
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

	/** Reports whether this lease still identifies one fixed registry and its pending lists. */
	bool IsValid() const noexcept
	{
		const bool bHandlesPresent = Count != nullptr && PendingSpawnCount != nullptr && PendingDestroyCount != nullptr;
		const bool bArraysPresent = Capacity == 0 || (Actors != nullptr && PendingSpawn != nullptr && PendingDestroy != nullptr);
		const bool bCountsBounded =
			bHandlesPresent && *Count <= Capacity && *PendingSpawnCount <= Capacity && *PendingDestroyCount <= Capacity;
		return bHandlesPresent && bArraysPresent && bCountsBounded;
	}

	/** Returns the maximum number of actors accepted by this registry. */
	std::size_t GetCapacity() const noexcept { return Capacity; }

	/** Returns the number of actors registered by the owning world. */
	std::size_t GetCount() const noexcept { return Count != nullptr ? *Count : 0; }

	/** Returns one registered actor reference by validated internal index. */
	const TObjectPtr<AActor>& At(const std::size_t Index) const noexcept { return Actors[Index]; }

	/** Publishes one validated actor and advances the private live count. */
	void Add(const TObjectPtr<AActor> Actor) noexcept
	{
		Actors[*Count] = Actor;
		++*Count;
	}

	/** Removes the live actor at Index and shifts later survivors left, preserving order. */
	void RemoveAt(const std::size_t Index) noexcept
	{
		for (std::size_t Slot = Index + 1; Slot < *Count; ++Slot)
		{
			Actors[Slot - 1] = Actors[Slot];
		}
		--*Count;
		Actors[*Count] = TObjectPtr<AActor>{};
	}

	/** Returns the number of actors queued to begin at the next deferred barrier. */
	std::size_t GetPendingSpawnCount() const noexcept { return PendingSpawnCount != nullptr ? *PendingSpawnCount : 0; }

	/** Returns one queued-spawn actor reference by validated internal index. */
	const TObjectPtr<AActor>& PendingSpawnAt(const std::size_t Index) const noexcept { return PendingSpawn[Index]; }

	/** Appends one actor to the bounded pending-spawn list. */
	void AddPendingSpawn(const TObjectPtr<AActor> Actor) noexcept
	{
		PendingSpawn[*PendingSpawnCount] = Actor;
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
	const TObjectPtr<AActor>& PendingDestroyAt(const std::size_t Index) const noexcept { return PendingDestroy[Index]; }

	/** Appends one actor to the bounded pending-destroy list. */
	void AddPendingDestroy(const TObjectPtr<AActor> Actor) noexcept
	{
		PendingDestroy[*PendingDestroyCount] = Actor;
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
