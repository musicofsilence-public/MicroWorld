#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ActorSpawnHandle.h>
#include <MicroWorld/Engine/ActorSpawnStatus.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/FactoryOperations.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>
#include <MicroWorld/Engine/ObjectStore.h>
#include <MicroWorld/Engine/ReferenceCollector.h>
#include <cstddef>

namespace MicroWorld::Engine
{

/**
 * Motivation: Defines the non-template storage operations UWorld needs after factory type erasure, so the world can drive
 *   a typed factory through one borrowed interface.
 * Responsibilities: Expose every reserve, activate, query, seal, construct, publish, restore, release, and trace
 *   operation the world needs without leaking template specifics or slot state.
 * Example:
 *   FActorSpawnHandle H = Storage.Reserve();
 *   Storage.Activate(H, Ops);
 */
class IDeferredActorSpawnStorage
{
public:
	/**
	 * Motivation: Keeps derived caller-owned storage valid while its world holds this borrowed interface.
	 * Responsibilities: Default the virtual destructor so derived teardown runs through the interface.
	 */
	virtual ~IDeferredActorSpawnStorage() noexcept = default;

	/**
	 * Motivation: Lets the world reserve one reusable fixed request slot after capacity and lifecycle preflight.
	 * Responsibilities: Return a fresh generation-checked handle or an invalid one when no slot remains.
	 */
	virtual FActorSpawnHandle Reserve() noexcept = 0;

	/**
	 * Motivation: Lets the world publish erased operations for a factory already placement-constructed in the reserved slot.
	 * Responsibilities: Store the operations and append the handle to the next barrier FIFO.
	 */
	virtual void Activate(FActorSpawnHandle InHandle, const FFactoryOperations& InOperations) noexcept = 0;

	/**
	 * Motivation: Lets the world report requests that still consume future actor-registry capacity.
	 * Responsibilities: Count only queued and construction-pending requests.
	 */
	virtual std::size_t PendingCount() const noexcept = 0;

	/**
	 * Motivation: Lets the world report caller-selected inline factory bytes for non-mutating template layout preflight.
	 * Responsibilities: Return the compile-time inline factory extent.
	 */
	virtual std::size_t InlineBytes() const noexcept = 0;

	/**
	 * Motivation: Lets the world report the public completion state for one generation-checked request handle.
	 * Responsibilities: Map private slot state to the public status or stale for an invalid handle.
	 */
	virtual FActorSpawnStatus GetStatus(FActorSpawnHandle InHandle) const noexcept = 0;

	/**
	 * Motivation: Lets the world seal both FIFO lanes before callback dispatch makes new requests possible.
	 * Responsibilities: Freeze the factory and publish queues into sealed snapshots and clear the live queues.
	 */
	virtual void SealBarrier() noexcept = 0;

	/**
	 * Motivation: Lets the world report the number of queued-factory tickets sealed for the current barrier.
	 * Responsibilities: Return the frozen factory ticket count.
	 */
	virtual std::size_t SealedFactoryCount() const noexcept = 0;

	/**
	 * Motivation: Lets the world read one sealed queued-factory ticket.
	 * Responsibilities: Return the ticket at the index or an invalid handle out of range.
	 */
	virtual FActorSpawnHandle SealedFactoryAt(std::size_t InIndex) const noexcept = 0;

	/**
	 * Motivation: Lets the world construct exactly one sealed factory or record its terminal object result.
	 * Responsibilities: Resolve the descriptor, invoke the factory, destroy the capture, and retain the actor for
	 *   guarded publication or mark the slot failed.
	 */
	virtual void Construct(FActorSpawnHandle InHandle, FObjectStore& InStore, FClassRegistryRegistrationView InClasses) noexcept = 0;

	/**
	 * Motivation: Lets the world read the ordered publish tickets (old pending actors first, then this barrier's
	 *   constructions).
	 * Responsibilities: Return the frozen publish ticket count.
	 */
	virtual std::size_t SealedPublishCount() const noexcept = 0;

	/**
	 * Motivation: Lets the world read one sealed constructed-pending-publish ticket.
	 * Responsibilities: Return the ticket at the index or an invalid handle out of range.
	 */
	virtual FActorSpawnHandle SealedPublishAt(std::size_t InIndex) const noexcept = 0;

	/**
	 * Motivation: Lets the world read a retained actor only when the ticket still names an unpublished constructed request.
	 * Responsibilities: Return the actor for a construction-pending slot or an empty reference otherwise.
	 */
	virtual TObjectPtr<AActor> GetConstructedActor(FActorSpawnHandle InHandle) const noexcept = 0;

	/**
	 * Motivation: Lets the world change one constructed actor request to Spawned after world ownership and BeginPlay dispatch.
	 * Responsibilities: Move a construction-pending slot to Spawned, pinning it until the actor leaves the registry.
	 */
	virtual void CompletePublish(FActorSpawnHandle InHandle) noexcept = 0;

	/**
	 * Motivation: Lets the world restore unprocessed constructed actors to the front of the next barrier's publish FIFO.
	 * Responsibilities: Requeue the unpublished suffix in FIFO order after a guard rejection.
	 */
	virtual void RestoreUnpublishedFrom(std::size_t InStartIndex) noexcept = 0;

	/**
	 * Motivation: Lets the world restore unconstructed factory tickets when an active collector blocks Phase 1.
	 * Responsibilities: Requeue sealed factories in FIFO order for a later safe construction barrier.
	 */
	virtual void RestoreUnconstructedFrom(std::size_t InStartIndex) noexcept = 0;

	/**
	 * Motivation: Lets failed startup discard the sealed batch instead of preserving pre-play work for a runtime retry.
	 * Responsibilities: Destroy unconstructed factories, release retained unpublished actors, and clear sealed queues.
	 */
	virtual void AbortSealedBatch() noexcept = 0;

	/**
	 * Motivation: Lets the world release exactly the pinned spawned request whose actor leaves the world registry.
	 * Responsibilities: Find and release the spawned slot whose actor handle matches, leaving others untouched.
	 */
	virtual void ReleaseActor(FObjectHandle InActorHandle) noexcept = 0;

	/**
	 * Motivation: Lets the world trace queued captures and temporarily retained constructed actors for collection.
	 * Responsibilities: Present every queued factory capture and construction-pending actor to the collector.
	 */
	virtual void VisitReferences(FReferenceCollector& InCollector) noexcept = 0;

	/**
	 * Motivation: Lets the world read writable inline bytes for a valid reserved request slot.
	 * Responsibilities: Return the slot's factory storage while the exact generation remains queued, else null.
	 */
	virtual void* GetFactoryStorage(FActorSpawnHandle InHandle) noexcept = 0;
};

} // namespace MicroWorld::Engine
