#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/ClassRegistryRegistrationView.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Engine/ObjectCreationResult.h>
#include <MicroWorld/Engine/ReferenceCollector.h>
#include <MicroWorld/Engine/ObjectStore.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets a caller carry one deferred-spawn request as a generation-checked pair without exposing request
 *   storage.
 * Responsibilities: Hold index and generation and report validity; a handle is local to the storage that issued it.
 * Example:
 *   FActorSpawnHandle Handle = Storage.Reserve();
 *   if (Handle.IsValid()) { Storage.Activate(Handle, Ops); }
 */
struct FActorSpawnHandle final
{
	/** Motivation: Reserves the final index value as an invalid request slot. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Motivation: Selects caller-owned request storage, or InvalidIndex when no request exists. */
	std::uint16_t Index{InvalidIndex};

	/** Motivation: Distinguishes each reuse of the same fixed request slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning storage.
	 * Responsibilities: Report true only when index and generation together look like an issued request.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }
};

/**
 * Motivation: Gives one issued deferred-spawn request a single observable lifetime vocabulary so a caller can branch on
 *   its progress without inspecting private slot state.
 * Responsibilities: Distinguish queued, spawned, failed, released, and stale states.
 * Example:
 *   if (Storage.GetStatus(Handle).State == EActorSpawnState::Spawned) { UseActor(); }
 */
enum class EActorSpawnState : std::uint8_t
{
	/** Motivation: Marks a request accepted but not yet a world-owned live actor. */
	Queued,
	/** Motivation: Marks a request whose actor now lives in the world registry. */
	Spawned,
	/** Motivation: Marks a request that failed construction and freed its capture. */
	Failed,
	/** Motivation: Marks a spawned request whose actor has since left the world. */
	Released,
	/** Motivation: Marks a handle that names no live or in-flight request. */
	Stale,
};

/**
 * Motivation: Reports why a typed factory request was not accepted before it captured arguments, so admission failures
 *   stay distinguishable from later construction failures.
 * Responsibilities: Distinguish queued from capacity, lifecycle-lock, unconfigured, factory-too-large, and
 *   factory-alignment failures.
 * Example:
 *   if (Request.Result == EActorSpawnRequestResult::FactoryTooLarge) { ShrinkCapture(); }
 */
enum class EActorSpawnRequestResult : std::uint8_t
{
	/** Motivation: Confirms the request was admitted to the queue and a handle was issued. */
	Queued,
	/** Motivation: Rejects a request when no reusable request slot remains. */
	CapacityExceeded,
	/** Motivation: Rejects a request because the owning world's lifecycle forbids new spawns. */
	LifecycleLocked,
	/** Motivation: Rejects a request because typed spawning was never configured for this world. */
	Unconfigured,
	/** Motivation: Rejects a request whose factory capture exceeds the configured inline bytes. */
	FactoryTooLarge,
	/** Motivation: Rejects a request whose factory capture alignment the storage cannot satisfy. */
	FactoryAlignmentUnsupported,
};

/**
 * Motivation: Couples immediate request admission with the handle available after successful queueing in one return
 *   value.
 * Responsibilities: Report the preflight result and carry a valid handle only when Result is Queued.
 * Example:
 *   FActorSpawnRequest R = World.DeferSpawnActor(...);
 *   if (R.Result == EActorSpawnRequestResult::Queued) { Keep(R.Handle); }
 */
struct FActorSpawnRequest final
{
	/** Motivation: Reports the preflight result without constructing an actor at the call site. */
	EActorSpawnRequestResult Result{EActorSpawnRequestResult::CapacityExceeded};

	/** Motivation: Identifies the request only when Result is Queued. */
	FActorSpawnHandle Handle{};
};

/**
 * Motivation: Reports deferred construction completion and the world-owned actor while it remains live, without letting
 *   an unpublished actor escape.
 * Responsibilities: Map private slot state to a public state, carry the construction result only, and resolve the actor
 *   only after it becomes a world-owned live entry.
 * Example:
 *   FActorSpawnStatus S = Storage.GetStatus(Handle);
 *   if (S.State == EActorSpawnState::Spawned) { S.Actor.Get()->Tick(); }
 */
struct FActorSpawnStatus final
{
	/** Motivation: Maps private construction-pending state to Queued so no unpublished actor escapes. */
	EActorSpawnState State{EActorSpawnState::Stale};

	/** Motivation: Holds object construction outcome only; BeginPlay errors remain ApplyPending results. */
	EObjectResult CompletionResult{EObjectResult::StaleHandle};

	/** Motivation: Resolves only after the actor becomes a world-owned live entry. */
	TObjectPtr<AActor> Actor{};
};

/**
 * Motivation: Lets custom deferred factory capture wrappers retain managed references during collection without tying
 *   the storage to each capture type.
 * Responsibilities: Visit a capture's references for the active collector; the default owns none.
 * Example:
 *   TDeferredActorSpawnCaptureTraits<int>::Visit(Value, Collector);
 */
template<typename T>
struct TDeferredActorSpawnCaptureTraits
{
	/**
	 * Motivation: Confirms default captures do not carry traced managed references.
	 * Responsibilities: Do nothing for a capture that owns no managed references.
	 */
	static void Visit(const T&, FReferenceCollector&) noexcept {}
};

/**
 * Motivation: Retains a directly captured managed pointer until its queued factory executes or fails.
 * Responsibilities: Present the captured reference to the active collector so it stays reachable while queued.
 * Example:
 *   TDeferredActorSpawnCaptureTraits<TObjectPtr<UActorComponent>>::Visit(Comp, Collector);
 */
template<typename T>
struct TDeferredActorSpawnCaptureTraits<TObjectPtr<T>>
{
	/**
	 * Motivation: Presents the direct captured reference to the active collector.
	 * Responsibilities: Add the captured reference to the collector.
	 */
	static void Visit(const TObjectPtr<T>& InReference, FReferenceCollector& InCollector) noexcept { InCollector.AddReferencedObject(InReference); }
};

/**
 * Motivation: Type-erases invocation, destruction, reference tracing, and descriptor resolution for one inline factory
 *   so non-template storage can drive a template factory through function pointers.
 * Responsibilities: Hold one pointer per erased operation and leave them null until a factory is activated.
 * Example:
 *   FFactoryOperations Ops{&F::Invoke, &F::Destroy, &F::VisitReferences, &F::ResolveDescriptor};
 */
struct FFactoryOperations final
{
	using FInvoke = TObjectCreationResult<AActor> (*)(void*, FObjectStore&, const FClassDescriptor&) noexcept;
	using FDestroy = void (*)(void*) noexcept;
	using FVisitReferences = void (*)(const void*, FReferenceCollector&) noexcept;
	using FResolveDescriptor = EObjectResult (*)(FClassRegistryRegistrationView, const FClassDescriptor*&) noexcept;

	/** Motivation: Invokes the factory to construct its actor in the store. */
	FInvoke Invoke{nullptr};

	/** Motivation: Destroys moved constructor values after construction or terminal failure. */
	FDestroy Destroy{nullptr};

	/** Motivation: Traces direct managed pointer captures while the queued factory owns them. */
	FVisitReferences VisitReferences{nullptr};

	/** Motivation: Resolves or registers the descriptor the factory needs to construct. */
	FResolveDescriptor ResolveDescriptor{nullptr};
};

/**
 * Motivation: Owns decayed constructor values until a safe world barrier performs managed construction, so a caller can
 *   defer an actor spawn without holding its arguments on the call stack.
 * Responsibilities: Capture arguments once after preflight, construct exactly once at the barrier through the registered
 *   descriptor, trace direct managed captures for collection, and resolve or register the actor's descriptor.
 * Example:
 *   using F = TActorFactory<AMyActor, int>;
 *   F Factory{42};
 *   auto Result = F::Invoke(&Factory, Store, Descriptor);
 */
template<typename TActor, typename... TArguments>
class TActorFactory final
{
public:
	/**
	 * Motivation: Captures decayed constructor values only after every non-mutating queue preflight has passed.
	 * Responsibilities: Move each argument into the retained tuple.
	 */
	explicit TActorFactory(TArguments... InArguments) noexcept : Arguments(std::move(InArguments)...) {}

	/**
	 * Motivation: Uses the canonical registered descriptor to construct the actor in the world's store.
	 * Responsibilities: Forward to InvokeWithArguments with the retained tuple expanded.
	 */
	static TObjectCreationResult<AActor> Invoke(void* const InFactory, FObjectStore& InStore, const FClassDescriptor& InDescriptor) noexcept
	{
		TActorFactory& Factory = *static_cast<TActorFactory*>(InFactory);
		return InvokeWithArguments(Factory, InStore, InDescriptor, std::index_sequence_for<TArguments...>{});
	}

	/**
	 * Motivation: Destroys moved constructor values exactly once after construction or terminal failure.
	 * Responsibilities: Invoke the destructor on the placement-constructed factory.
	 */
	static void Destroy(void* const InFactory) noexcept { static_cast<TActorFactory*>(InFactory)->~TActorFactory(); }

	/**
	 * Motivation: Traces direct managed pointer captures while the queued factory owns them.
	 * Responsibilities: Visit each captured argument through its capture traits.
	 */
	static void VisitReferences(const void* const InFactory, FReferenceCollector& InCollector) noexcept
	{
		const TActorFactory& Factory = *static_cast<const TActorFactory*>(InFactory);
		std::apply(
			[&InCollector](const TArguments&... InCapturedArguments) noexcept
			{ (TDeferredActorSpawnCaptureTraits<TArguments>::Visit(InCapturedArguments, InCollector), ...); },
			Factory.Arguments);
	}

	/**
	 * Motivation: Reuses a manually registered descriptor or registers a direct AActor child with a local automatic ID.
	 * Responsibilities: Find an existing descriptor by type token, else register a direct child of AActor and return the
	 *   stable owned address.
	 */
	static EObjectResult ResolveDescriptor(const FClassRegistryRegistrationView InClasses, const FClassDescriptor*& OutDescriptor) noexcept
	{
		OutDescriptor = InClasses.FindByTypeToken(ManagedObjectTypeToken<TActor>());
		if (OutDescriptor != nullptr)
		{
			return EObjectResult::Success;
		}
		const FClassDescriptor* const ActorDescriptor = InClasses.Find(AActorClassId);
		if (ActorDescriptor == nullptr)
		{
			return EObjectResult::UnknownClass;
		}
		const FClassDescriptor Candidate = MakeClassDescriptor<TActor>(0, "DeferredActor", ActorDescriptor, &TraceManagedObjectReferences);
		return InClasses.RegisterAutomatic(Candidate, OutDescriptor);
	}

private:
	/**
	 * Motivation: Expands the retained tuple at the only safe managed-construction point.
	 * Responsibilities: Move each argument into the store's NewObject and return the creation result as an AActor.
	 */
	template<std::size_t... Indices>
	static TObjectCreationResult<AActor> InvokeWithArguments(
		TActorFactory& InFactory, FObjectStore& InStore, const FClassDescriptor& InDescriptor, std::index_sequence<Indices...>) noexcept
	{
		const TObjectCreationResult<TActor> Creation = InStore.NewObject<TActor>(InDescriptor, std::move(std::get<Indices>(InFactory.Arguments))...);
		return TObjectCreationResult<AActor>{Creation.Result, TObjectPtr<AActor>{Creation.Object}};
	}

	/** Motivation: Holds decayed constructor values across the caller callback and barrier boundary. */
	std::tuple<TArguments...> Arguments;
};

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

		const TObjectCreationResult<AActor> Creation = Slot->Operations.Invoke(Slot->FactoryBytes.data(), InStore, *Descriptor);
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
