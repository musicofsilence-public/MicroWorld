#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorSpawnHandle.h>
#include <MicroWorld/Engine/ActorSpawnStatus.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/FactoryOperations.h>
#include <MicroWorld/Engine/IDeferredActorSpawnStorage.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ReferenceCollector.h>
#include <cstddef>

namespace MicroWorld::Engine
{

template<std::size_t, std::size_t>
class TDeferredActorSpawnStorage;

/**
 * Motivation: Gives one world a move-only borrowed capability over a caller-owned deferred factory storage owner, so the
 *   queue and completion history cannot be shared or forged.
 * Responsibilities: Forward each operation to the borrowed storage when valid and to a safe no-op otherwise; be creatable
 *   only by the matching storage owner.
 * Example:
 *   FDeferredActorSpawnStorageReference Ref = Storage.MakeReference();
 *   if (Ref.IsValid()) { Ref.SealBarrier(); }
 */
class FDeferredActorSpawnStorageReference final
{
public:
	/**
	 * Motivation: Creates an empty capability for direct UWorld consumers that do not configure typed spawning.
	 * Responsibilities: Produce a reference that forwards every operation to a no-op.
	 */
	FDeferredActorSpawnStorageReference() noexcept = default;

	/**
	 * Motivation: Transfers one world-only mutable capability and invalidates the source.
	 * Responsibilities: Move the storage pointer and leave the source empty.
	 */
	FDeferredActorSpawnStorageReference(FDeferredActorSpawnStorageReference&& Other) noexcept : Storage(Other.Storage) { Other.Storage = nullptr; }

	/**
	 * Motivation: Prevents two worlds from mutating one queue and completion history.
	 * Responsibilities: Reject copy construction so a storage backs at most one world.
	 */
	FDeferredActorSpawnStorageReference(const FDeferredActorSpawnStorageReference&) = delete;

	/**
	 * Motivation: Prevents rebinding a world's storage after construction.
	 * Responsibilities: Reject copy assignment so the borrowed storage never changes.
	 */
	FDeferredActorSpawnStorageReference& operator=(const FDeferredActorSpawnStorageReference&) = delete;

	/**
	 * Motivation: Prevents replacing a world's borrowed storage after construction via move.
	 * Responsibilities: Reject move assignment so the borrowed storage never changes.
	 */
	FDeferredActorSpawnStorageReference& operator=(FDeferredActorSpawnStorageReference&&) = delete;

	/**
	 * Motivation: Lets a caller confirm this reference still points to caller-owned storage before use.
	 * Responsibilities: Report true only when the storage pointer is non-null.
	 */
	bool IsValid() const noexcept { return Storage != nullptr; }

	/**
	 * Motivation: Forwards one checked request-slot reservation.
	 * Responsibilities: Reserve a fresh handle from the storage or return an invalid one when unconfigured.
	 */
	FActorSpawnHandle Reserve() noexcept { return Storage != nullptr ? Storage->Reserve() : FActorSpawnHandle{}; }

	/**
	 * Motivation: Returns the raw inline bytes reserved for one factory capture.
	 * Responsibilities: Return the slot's factory storage or null when unconfigured.
	 */
	void* GetFactoryStorage(const FActorSpawnHandle InHandle) noexcept { return Storage != nullptr ? Storage->GetFactoryStorage(InHandle) : nullptr; }

	/**
	 * Motivation: Publishes type-erased factory behavior after placement capture succeeds.
	 * Responsibilities: Forward Activate to the storage when configured.
	 */
	void Activate(const FActorSpawnHandle InHandle, const FFactoryOperations& InOperations) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->Activate(InHandle, InOperations);
		}
	}

	/**
	 * Motivation: Reports only queued or construction-pending requests that still need registry capacity.
	 * Responsibilities: Forward PendingCount or return zero when unconfigured.
	 */
	std::size_t PendingCount() const noexcept { return Storage != nullptr ? Storage->PendingCount() : 0; }

	/**
	 * Motivation: Reports factory storage extent, or zero when this World was not configured for typed spawning.
	 * Responsibilities: Forward InlineBytes or return zero when unconfigured.
	 */
	std::size_t InlineBytes() const noexcept { return Storage != nullptr ? Storage->InlineBytes() : 0; }

	/**
	 * Motivation: Returns bounded completion state without exposing storage.
	 * Responsibilities: Forward GetStatus or return a default stale status when unconfigured.
	 */
	FActorSpawnStatus GetStatus(const FActorSpawnHandle InHandle) const noexcept
	{
		return Storage != nullptr ? Storage->GetStatus(InHandle) : FActorSpawnStatus{};
	}

	/**
	 * Motivation: Seals the current barrier's immutable request snapshots.
	 * Responsibilities: Forward SealBarrier to the storage when configured.
	 */
	void SealBarrier() noexcept
	{
		if (Storage != nullptr)
		{
			Storage->SealBarrier();
		}
	}

	/**
	 * Motivation: Returns current sealed queued-factory ticket count.
	 * Responsibilities: Forward SealedFactoryCount or return zero when unconfigured.
	 */
	std::size_t SealedFactoryCount() const noexcept { return Storage != nullptr ? Storage->SealedFactoryCount() : 0; }

	/**
	 * Motivation: Returns one sealed queued-factory ticket.
	 * Responsibilities: Forward SealedFactoryAt or return an invalid handle when unconfigured.
	 */
	FActorSpawnHandle SealedFactoryAt(const std::size_t InIndex) const noexcept
	{
		return Storage != nullptr ? Storage->SealedFactoryAt(InIndex) : FActorSpawnHandle{};
	}

	/**
	 * Motivation: Constructs one queued factory at the barrier.
	 * Responsibilities: Forward Construct to the storage when configured.
	 */
	void Construct(const FActorSpawnHandle InHandle, FObjectStore& InStore, const FClassRegistryRegistrationView InClasses) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->Construct(InHandle, InStore, InClasses);
		}
	}

	/**
	 * Motivation: Returns current sealed publish ticket count.
	 * Responsibilities: Forward SealedPublishCount or return zero when unconfigured.
	 */
	std::size_t SealedPublishCount() const noexcept { return Storage != nullptr ? Storage->SealedPublishCount() : 0; }

	/**
	 * Motivation: Returns one sealed publish ticket.
	 * Responsibilities: Forward SealedPublishAt or return an invalid handle when unconfigured.
	 */
	FActorSpawnHandle SealedPublishAt(const std::size_t InIndex) const noexcept
	{
		return Storage != nullptr ? Storage->SealedPublishAt(InIndex) : FActorSpawnHandle{};
	}

	/**
	 * Motivation: Resolves one retained constructed actor without publishing it.
	 * Responsibilities: Forward GetConstructedActor or return an empty reference when unconfigured.
	 */
	TObjectPtr<AActor> GetConstructedActor(const FActorSpawnHandle InHandle) const noexcept
	{
		return Storage != nullptr ? Storage->GetConstructedActor(InHandle) : TObjectPtr<AActor>{};
	}

	/**
	 * Motivation: Completes world publication for one constructed actor.
	 * Responsibilities: Forward CompletePublish to the storage when configured.
	 */
	void CompletePublish(const FActorSpawnHandle InHandle) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->CompletePublish(InHandle);
		}
	}

	/**
	 * Motivation: Keeps every unprocessed constructed actor available for the next barrier after a guard rejection.
	 * Responsibilities: Forward RestoreUnpublishedFrom to the storage when configured.
	 */
	void RestoreUnpublishedFrom(const std::size_t InStartIndex) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->RestoreUnpublishedFrom(InStartIndex);
		}
	}

	/**
	 * Motivation: Keeps sealed factories in FIFO order for a later safe construction barrier.
	 * Responsibilities: Forward RestoreUnconstructedFrom to the storage when configured.
	 */
	void RestoreUnconstructedFrom(const std::size_t InStartIndex) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->RestoreUnconstructedFrom(InStartIndex);
		}
	}

	/**
	 * Motivation: Lets a failed startup discard all sealed pre-play factory work through the narrow storage capability.
	 * Responsibilities: Forward the terminal batch abort when configured and otherwise perform no work.
	 */
	void AbortSealedBatch() noexcept
	{
		if (Storage != nullptr)
		{
			Storage->AbortSealedBatch();
		}
	}

	/**
	 * Motivation: Releases the handle state that was pinned by a removed world actor.
	 * Responsibilities: Forward ReleaseActor to the storage when configured.
	 */
	void ReleaseActor(const FObjectHandle InActorHandle) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->ReleaseActor(InActorHandle);
		}
	}

	/**
	 * Motivation: Presents queued captures and unpublished actors to collection.
	 * Responsibilities: Forward VisitReferences to the storage when configured.
	 */
	void VisitReferences(FReferenceCollector& InCollector) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->VisitReferences(InCollector);
		}
	}

private:
	template<std::size_t, std::size_t>
	friend class TDeferredActorSpawnStorage;

	/**
	 * Motivation: Creates one valid capability only from its matching caller-owned storage owner.
	 * Responsibilities: Bind the storage pointer without re-validating, since the owner validated it.
	 */
	explicit FDeferredActorSpawnStorageReference(IDeferredActorSpawnStorage& InStorage) noexcept : Storage(&InStorage) {}

	/** Motivation: Identifies caller-owned request storage whose lifetime encloses the World. */
	IDeferredActorSpawnStorage* Storage{nullptr};
};

} // namespace MicroWorld::Engine
