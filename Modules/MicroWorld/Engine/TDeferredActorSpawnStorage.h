#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorSpawnHandle.h>
#include <MicroWorld/Engine/ActorSpawnState.h>
#include <MicroWorld/Engine/ActorSpawnStatus.h>
#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/DeferredActorSpawnStorageReference.h>
#include <MicroWorld/Engine/FactoryOperations.h>
#include <MicroWorld/Engine/IDeferredActorSpawnStorage.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ReferenceCollector.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace MicroWorld::Engine
{

/**
 * Motivation: Owns all bounded factory storage, FIFO tickets, and completion history for one world so deferred actor
 *   spawning stays allocation-free and lifecycle-safe.
 * Responsibilities: Reserve and activate request slots, seal and construct factories at the world barrier, publish
 *   constructed actors under a guard, restore work after a rejection, and keep queued captures and unpublished actors
 *   reachable for collection.
 * Example:
 *   TDeferredActorSpawnStorage<4, 64> Storage;
 *   FDeferredActorSpawnStorageReference Ref = Storage.MakeReference();
 */
template<std::size_t MaxRequests, std::size_t InlineFactoryBytes>
class TDeferredActorSpawnStorage final : public IDeferredActorSpawnStorage
{
public:
	static_assert(MaxRequests <= static_cast<std::size_t>(FActorSpawnHandle::InvalidIndex), "Deferred request count must fit FActorSpawnHandle.");
	static_assert(InlineFactoryBytes > 0, "Deferred factories require non-zero inline storage.");

	/**
	 * Motivation: Preserves every raw factory address retained by a world reference.
	 * Responsibilities: Default-construct the storage with all slots free.
	 */
	TDeferredActorSpawnStorage() noexcept = default;

	/**
	 * Motivation: Ensures any queued captures are destroyed before the caller-owned bytes disappear.
	 * Responsibilities: Run each active factory's Destroy through its erased operations.
	 */
	~TDeferredActorSpawnStorage() noexcept override
	{
		for (FSlot& Slot : Slots)
		{
			DestroyFactory(Slot);
		}
	}

	/**
	 * Motivation: Prevents copying stable factory storage and request generations.
	 * Responsibilities: Reject copy construction and assignment so addresses and generations stay single-owner.
	 */
	TDeferredActorSpawnStorage(const TDeferredActorSpawnStorage&) = delete;

	/**
	 * Motivation: Prevents copy assignment from relocating stable factory storage and request generations.
	 * Responsibilities: Reject copy assignment so addresses and generations stay single-owner.
	 */
	TDeferredActorSpawnStorage& operator=(const TDeferredActorSpawnStorage&) = delete;

	/**
	 * Motivation: Prevents moving stable factory storage and request generations.
	 * Responsibilities: Reject move construction so addresses and generations stay single-owner.
	 */
	TDeferredActorSpawnStorage(TDeferredActorSpawnStorage&&) = delete;

	/**
	 * Motivation: Prevents move assignment from relocating stable factory storage and request generations.
	 * Responsibilities: Reject move assignment so addresses and generations stay single-owner.
	 */
	TDeferredActorSpawnStorage& operator=(TDeferredActorSpawnStorage&&) = delete;

	/**
	 * Motivation: Transfers the one mutable queue capability to one world, exactly once.
	 * Responsibilities: Return a valid reference on the first call and an empty one thereafter.
	 */
	FDeferredActorSpawnStorageReference MakeReference() & noexcept
	{
		if (bReferenceMade)
		{
			return FDeferredActorSpawnStorageReference{};
		}
		bReferenceMade = true;
		return FDeferredActorSpawnStorageReference(*this);
	}

	/**
	 * Motivation: Prevents a temporary storage owner from escaping into a world.
	 * Responsibilities: Reject MakeReference on an rvalue so no escaped reference survives its owner.
	 */
	FDeferredActorSpawnStorageReference MakeReference() && = delete;

	/**
	 * Motivation: Reserves the first terminal or never-used slot without constructing any capture.
	 * Responsibilities: Find a reusable slot, retire a slot that would wrap its generation, else advance the generation
	 *   and publish a fresh handle.
	 */
	FActorSpawnHandle Reserve() noexcept override
	{
		for (std::size_t Index = 0; Index < MaxRequests; ++Index)
		{
			FSlot& Slot = Slots[Index];
			if (!IsSlotReusable(Slot))
			{
				continue;
			}
			if (Slot.Generation == std::numeric_limits<std::uint32_t>::max())
			{
				Slot.State = ESlotState::Retired;
				continue;
			}
			++Slot.Generation;
			Slot.State = ESlotState::Queued;
			Slot.CompletionResult = EObjectResult::Success;
			Slot.Actor = {};
			return FActorSpawnHandle{static_cast<std::uint16_t>(Index), Slot.Generation};
		}
		return {};
	}

	/**
	 * Motivation: Returns raw bytes only while the exact request generation remains queued.
	 * Responsibilities: Return the slot's factory storage for a matching queued generation, else null.
	 */
	void* GetFactoryStorage(const FActorSpawnHandle InHandle) noexcept override
	{
		FSlot* const Slot = FindSlot(InHandle);
		return Slot != nullptr && Slot->State == ESlotState::Queued ? Slot->FactoryBytes.data() : nullptr;
	}

	/**
	 * Motivation: Activates one factory and appends its immutable handle to the next barrier FIFO.
	 * Responsibilities: Store the operations on a matching queued slot and enqueue its handle.
	 */
	void Activate(const FActorSpawnHandle InHandle, const FFactoryOperations& InOperations) noexcept override
	{
		FSlot* const Slot = FindSlot(InHandle);
		if (Slot == nullptr || Slot->State != ESlotState::Queued)
		{
			return;
		}
		Slot->Operations = InOperations;
		FactoryQueue[FactoryQueueCount] = InHandle;
		++FactoryQueueCount;
	}

	/**
	 * Motivation: Counts only factory and publish-pending requests that are not yet live actors.
	 * Responsibilities: Sum the Queued and ConstructedPendingPublish slots.
	 */
	std::size_t PendingCount() const noexcept override
	{
		std::size_t Count = 0;
		for (const FSlot& Slot : Slots)
		{
			if (Slot.State == ESlotState::Queued || Slot.State == ESlotState::ConstructedPendingPublish)
			{
				++Count;
			}
		}
		return Count;
	}

	/**
	 * Motivation: Reports the compile-time inline factory extent owned by this caller-provided storage.
	 * Responsibilities: Return InlineFactoryBytes for non-mutating template layout preflight.
	 */
	std::size_t InlineBytes() const noexcept override { return InlineFactoryBytes; }

	/**
	 * Motivation: Maps each internal slot state to the public handle contract.
	 * Responsibilities: Return the public status for a matching slot or a stale default otherwise.
	 */
	FActorSpawnStatus GetStatus(const FActorSpawnHandle InHandle) const noexcept override
	{
		const FSlot* const Slot = FindSlot(InHandle);
		if (Slot == nullptr)
		{
			return {};
		}
		switch (Slot->State)
		{
			case ESlotState::Queued:
			case ESlotState::ConstructedPendingPublish:
				return FActorSpawnStatus{EActorSpawnState::Queued, EObjectResult::Success, {}};
			case ESlotState::Spawned:
				return FActorSpawnStatus{EActorSpawnState::Spawned, Slot->CompletionResult, Slot->Actor};
			case ESlotState::Failed:
				return FActorSpawnStatus{EActorSpawnState::Failed, Slot->CompletionResult, {}};
			case ESlotState::Released:
				return FActorSpawnStatus{EActorSpawnState::Released, Slot->CompletionResult, {}};
			default:
				return {};
		}
	}

	/**
	 * Motivation: Freezes both FIFO lanes before any barrier callback can append new work.
	 * Responsibilities: Snapshot the factory and publish queues into sealed arrays and clear the live queues.
	 */
	void SealBarrier() noexcept override
	{
		SealedFactoryCountValue = FactoryQueueCount;
		for (std::size_t Index = 0; Index < FactoryQueueCount; ++Index)
		{
			SealedFactoryQueue[Index] = FactoryQueue[Index];
		}
		FactoryQueueCount = 0;

		SealedPublishCountValue = PublishQueueCount;
		for (std::size_t Index = 0; Index < PublishQueueCount; ++Index)
		{
			SealedPublishQueue[Index] = PublishQueue[Index];
		}
		PublishQueueCount = 0;
	}

	/**
	 * Motivation: Reports the frozen factory request count.
	 * Responsibilities: Return SealedFactoryCountValue.
	 */
	std::size_t SealedFactoryCount() const noexcept override { return SealedFactoryCountValue; }

	/**
	 * Motivation: Returns a frozen factory ticket or an invalid handle for an out-of-range query.
	 * Responsibilities: Return the sealed factory ticket at the index or an invalid handle.
	 */
	FActorSpawnHandle SealedFactoryAt(const std::size_t InIndex) const noexcept override
	{
		return InIndex < SealedFactoryCountValue ? SealedFactoryQueue[InIndex] : FActorSpawnHandle{};
	}

	/**
	 * Motivation: Resolves descriptor identity, constructs safely, then retains the actor for guarded publication.
	 * Responsibilities: Resolve the descriptor, invoke the factory, destroy the capture, and move the slot to
	 *   construction-pending or failure.
	 */
	void Construct(const FActorSpawnHandle InHandle, FObjectStore& InStore, const FClassRegistryRegistrationView InClasses) noexcept override
	{
		FSlot* const Slot = FindSlot(InHandle);
		if (!IsSlotReadyForConstruction(Slot))
		{
			return;
		}

		const FClassDescriptor* Descriptor = nullptr;
		const EObjectResult DescriptorResult = Slot->Operations.ResolveDescriptor(InClasses, Descriptor);
		if (DescriptorResult != EObjectResult::Success || Descriptor == nullptr)
		{
			CompleteFailure(*Slot, DescriptorResult != EObjectResult::Success ? DescriptorResult : EObjectResult::UnknownClass);
			return;
		}

		const TObjectCreationResult<AActor> Creation = Slot->Operations.Invoke(Slot->FactoryBytes.data(), InStore, *Descriptor, InClasses);
		DestroyFactory(*Slot);
		if (Creation.Result != EObjectResult::Success)
		{
			CompleteFailure(*Slot, Creation.Result);
			return;
		}

		Slot->Actor = Creation.Object;
		Slot->CompletionResult = EObjectResult::Success;
		Slot->State = ESlotState::ConstructedPendingPublish;
		SealedPublishQueue[SealedPublishCountValue] = InHandle;
		++SealedPublishCountValue;
	}

	/**
	 * Motivation: Reports the ordered frozen publish list, including this barrier's successful constructions.
	 * Responsibilities: Return SealedPublishCountValue.
	 */
	std::size_t SealedPublishCount() const noexcept override { return SealedPublishCountValue; }

	/**
	 * Motivation: Returns one frozen publish ticket or invalid for an out-of-range query.
	 * Responsibilities: Return the sealed publish ticket at the index or an invalid handle.
	 */
	FActorSpawnHandle SealedPublishAt(const std::size_t InIndex) const noexcept override
	{
		return InIndex < SealedPublishCountValue ? SealedPublishQueue[InIndex] : FActorSpawnHandle{};
	}

	/**
	 * Motivation: Returns a retained actor while it is intentionally hidden from the public handle state.
	 * Responsibilities: Return the actor for a construction-pending slot or an empty reference otherwise.
	 */
	TObjectPtr<AActor> GetConstructedActor(const FActorSpawnHandle InHandle) const noexcept override
	{
		const FSlot* const Slot = FindSlot(InHandle);
		return Slot != nullptr && Slot->State == ESlotState::ConstructedPendingPublish ? Slot->Actor : TObjectPtr<AActor>{};
	}

	/**
	 * Motivation: Pins a successful request until the actor later leaves the live world registry.
	 * Responsibilities: Move a construction-pending slot to Spawned.
	 */
	void CompletePublish(const FActorSpawnHandle InHandle) noexcept override
	{
		FSlot* const Slot = FindSlot(InHandle);
		if (Slot != nullptr && Slot->State == ESlotState::ConstructedPendingPublish)
		{
			Slot->State = ESlotState::Spawned;
		}
	}

	/**
	 * Motivation: Restores the unfinished suffix in FIFO order after publication cannot acquire a dispatch guard.
	 * Responsibilities: Requeue the unpublished construction-pending tickets and reset the sealed publish count.
	 */
	void RestoreUnpublishedFrom(const std::size_t InStartIndex) noexcept override
	{
		for (std::size_t Index = InStartIndex; Index < SealedPublishCountValue; ++Index)
		{
			const FActorSpawnHandle Ticket = SealedPublishQueue[Index];
			const FSlot* const Slot = FindSlot(Ticket);
			if (Slot != nullptr && Slot->State == ESlotState::ConstructedPendingPublish)
			{
				PublishQueue[PublishQueueCount] = Ticket;
				++PublishQueueCount;
			}
		}
		SealedPublishCountValue = 0;
	}

	/**
	 * Motivation: Restores sealed factories in FIFO order when construction remains unsafe during an active collection.
	 * Responsibilities: Requeue the unconstructed queued tickets and reset the sealed factory count.
	 */
	void RestoreUnconstructedFrom(const std::size_t InStartIndex) noexcept override
	{
		for (std::size_t Index = InStartIndex; Index < SealedFactoryCountValue; ++Index)
		{
			const FActorSpawnHandle Ticket = SealedFactoryQueue[Index];
			const FSlot* const Slot = FindSlot(Ticket);
			if (Slot != nullptr && Slot->State == ESlotState::Queued)
			{
				FactoryQueue[FactoryQueueCount] = Ticket;
				++FactoryQueueCount;
			}
		}
		SealedFactoryCountValue = 0;
	}

	/**
	 * Motivation: Makes a failed World startup terminal for its sealed pre-play composition batch.
	 * Responsibilities: Destroy queued captures, release unpublished actor references, and make every request slot reusable.
	 */
	void AbortSealedBatch() noexcept override
	{
		for (FSlot& Slot : Slots)
		{
			if (Slot.State == ESlotState::Queued)
			{
				DestroyFactory(Slot);
			}
			if (Slot.State == ESlotState::Queued || Slot.State == ESlotState::ConstructedPendingPublish)
			{
				Slot.Actor = {};
				Slot.CompletionResult = EObjectResult::LifecycleLocked;
				Slot.State = ESlotState::Failed;
			}
		}
		FactoryQueueCount = 0;
		PublishQueueCount = 0;
		SealedFactoryCountValue = 0;
		SealedPublishCountValue = 0;
	}

	/**
	 * Motivation: Releases only the pinned request that names an actor removed from this World.
	 * Responsibilities: Find the spawned slot whose actor handle matches and move it to Released.
	 */
	void ReleaseActor(const FObjectHandle InActorHandle) noexcept override
	{
		for (FSlot& Slot : Slots)
		{
			if (Slot.State == ESlotState::Spawned && Slot.Actor.Handle() == InActorHandle)
			{
				Slot.Actor = {};
				Slot.State = ESlotState::Released;
				return;
			}
		}
	}

	/**
	 * Motivation: Keeps queued managed captures and unpublished actors reachable through one World trace edge.
	 * Responsibilities: Visit each queued factory capture and each construction-pending actor with the collector.
	 */
	void VisitReferences(FReferenceCollector& InCollector) noexcept override
	{
		for (const FSlot& Slot : Slots)
		{
			if (Slot.State == ESlotState::Queued && Slot.Operations.VisitReferences != nullptr)
			{
				Slot.Operations.VisitReferences(Slot.FactoryBytes.data(), InCollector);
			}
			else if (Slot.State == ESlotState::ConstructedPendingPublish)
			{
				InCollector.AddReferencedObject(Slot.Actor);
			}
		}
	}

private:
	/**
	 * Motivation: Distinguishes reusable slots from every private request and publish phase.
	 * Responsibilities: Name the free, queued, construction-pending, spawned, failed, released, and retired states.
	 * Example:
	 *   if (Slot.State == ESlotState::Queued) { Construct(); }
	 */
	enum class ESlotState : std::uint8_t
	{
		/** Motivation: Marks a slot available for a fresh reservation. */
		Free,
		/** Motivation: Marks a slot holding an accepted factory awaiting a barrier. */
		Queued,
		/** Motivation: Marks a slot holding an actor awaiting guarded publication. */
		ConstructedPendingPublish,
		/** Motivation: Marks a slot whose actor is now world-owned. */
		Spawned,
		/** Motivation: Marks a slot whose construction failed and freed its capture. */
		Failed,
		/** Motivation: Marks a slot whose spawned actor has since left the world. */
		Released,
		/** Motivation: Permanently removes a slot before its generation could wrap. */
		Retired,
	};

	/**
	 * Motivation: Holds one request's lifetime state, exact factory bytes, and temporary or live actor identity in fixed
	 *   caller-owned storage.
	 * Responsibilities: Carry generation, state, completion result, actor, operations, and inline factory bytes for one
	 *   slot.
	 * Example:
	 *   FSlot Slot;
	 *   Slot.State = ESlotState::Queued;
	 */
	struct alignas(std::max_align_t) FSlot final
	{
		/** Motivation: Tracks generation-checked request identity independently of object-store slots. */
		std::uint32_t Generation{0};

		/** Motivation: Keeps a factory or actor in the phase that controls reclamation and handle status. */
		ESlotState State{ESlotState::Free};

		/** Motivation: Preserves the construction-only terminal result after factory destruction. */
		EObjectResult CompletionResult{EObjectResult::Success};

		/** Motivation: Retains constructed and spawned actors without exposing construction-pending objects publicly. */
		TObjectPtr<AActor> Actor{};

		/** Motivation: Erases factory behavior without extending Core delegate policy. */
		FFactoryOperations Operations{};

		/** Motivation: Holds one caller-selected-size factory without heap allocation. */
		std::array<std::byte, InlineFactoryBytes> FactoryBytes{};
	};

	/**
	 * Motivation: Reports whether a slot is in one of the states that allow a fresh reservation.
	 * Responsibilities: Return true for Free, Failed, and Released slots.
	 */
	static bool IsSlotReusable(const FSlot& InSlot) noexcept
	{
		return InSlot.State == ESlotState::Free || InSlot.State == ESlotState::Failed || InSlot.State == ESlotState::Released;
	}

	/**
	 * Motivation: Reports whether a queued slot carries the factory operations Construct requires.
	 * Responsibilities: Return true only for a queued slot with Invoke and ResolveDescriptor set.
	 */
	static bool IsSlotReadyForConstruction(const FSlot* const InSlot) noexcept
	{
		if (InSlot == nullptr || InSlot->State != ESlotState::Queued)
		{
			return false;
		}
		return InSlot->Operations.Invoke != nullptr && InSlot->Operations.ResolveDescriptor != nullptr;
	}

	/**
	 * Motivation: Reports whether a handle names a slot at a valid index whose generation still matches.
	 * Responsibilities: Check validity, index bounds, and generation equality together.
	 */
	bool IsHandleSlotMatch(const FActorSpawnHandle InHandle) const noexcept
	{
		return InHandle.IsValid() && InHandle.Index < MaxRequests && Slots[InHandle.Index].Generation == InHandle.Generation;
	}

	/**
	 * Motivation: Returns a slot only when the caller supplies its current valid generation.
	 * Responsibilities: Return the matching slot or null for an invalid or stale handle.
	 */
	FSlot* FindSlot(const FActorSpawnHandle InHandle) noexcept
	{
		if (!IsHandleSlotMatch(InHandle))
		{
			return nullptr;
		}
		return &Slots[InHandle.Index];
	}

	/**
	 * Motivation: Provides a const generation-checked slot lookup for query and tracing paths.
	 * Responsibilities: Return the matching slot or null for an invalid or stale handle.
	 */
	const FSlot* FindSlot(const FActorSpawnHandle InHandle) const noexcept
	{
		if (!IsHandleSlotMatch(InHandle))
		{
			return nullptr;
		}
		return &Slots[InHandle.Index];
	}

	/**
	 * Motivation: Destroys active factory capture state and clears callable operations exactly once.
	 * Responsibilities: Call the erased Destroy when present and reset the operations to null.
	 */
	static void DestroyFactory(FSlot& InSlot) noexcept
	{
		if (InSlot.Operations.Destroy != nullptr)
		{
			InSlot.Operations.Destroy(InSlot.FactoryBytes.data());
			InSlot.Operations = {};
		}
	}

	/**
	 * Motivation: Records an exact construction failure after safely releasing the factory capture.
	 * Responsibilities: Destroy the capture, clear the actor, store the result, and move the slot to Failed.
	 */
	static void CompleteFailure(FSlot& InSlot, const EObjectResult InResult) noexcept
	{
		DestroyFactory(InSlot);
		InSlot.Actor = {};
		InSlot.CompletionResult = InResult;
		InSlot.State = ESlotState::Failed;
	}

	/** Motivation: Holds all request slots at fixed capacity for one World lifetime. */
	std::array<FSlot, MaxRequests> Slots{};

	/** Motivation: Queues factories accepted after the current barrier seal. */
	std::array<FActorSpawnHandle, MaxRequests> FactoryQueue{};

	/** Motivation: Counts queued factories not yet copied into a barrier snapshot. */
	std::size_t FactoryQueueCount{0};

	/** Motivation: Queues constructed actors retained for a later guarded publication phase. */
	std::array<FActorSpawnHandle, MaxRequests> PublishQueue{};

	/** Motivation: Counts constructed actors awaiting a future barrier snapshot. */
	std::size_t PublishQueueCount{0};

	/** Motivation: Freezes factory work that callbacks must not extend in this barrier. */
	std::array<FActorSpawnHandle, MaxRequests> SealedFactoryQueue{};

	/** Motivation: Counts sealed factories available to Phase 1. */
	std::size_t SealedFactoryCountValue{0};

	/** Motivation: Holds pre-existing publish tickets first, followed by new Phase 1 constructions. */
	std::array<FActorSpawnHandle, MaxRequests> SealedPublishQueue{};

	/** Motivation: Counts ordered Phase 2 publication tickets. */
	std::size_t SealedPublishCountValue{0};

	/** Motivation: Ensures one caller-owned storage instance cannot back two Worlds. */
	bool bReferenceMade{false};
};

} // namespace MicroWorld::Engine
