#pragma once

#include <MicroWorld/Engine/Actor.h>
#include <MicroWorld/Engine/EngineClassIds.h>
#include <MicroWorld/Object/ObjectStore.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

/** Identifies one bounded deferred-spawn request generation. */
struct FActorSpawnHandle final
{
	/** Reserves the final index value as an invalid request slot. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Selects caller-owned request storage, or InvalidIndex when no request exists. */
	std::uint16_t Index{InvalidIndex};

	/** Distinguishes each reuse of the same fixed request slot. */
	std::uint32_t Generation{0};

	/** Confirms this pair represents an issued request handle. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }
};

/** Reports the observable lifetime of one issued deferred-spawn request. */
enum class EActorSpawnState : std::uint8_t
{
	Queued,
	Spawned,
	Failed,
	Released,
	Stale,
};

/** Reports why a typed factory request was not accepted before it captured arguments. */
enum class EActorSpawnRequestResult : std::uint8_t
{
	Queued,
	CapacityExceeded,
	LifecycleLocked,
	Unconfigured,
	FactoryTooLarge,
	FactoryAlignmentUnsupported,
};

/** Couples immediate request admission with the handle available after successful queueing. */
struct FActorSpawnRequest final
{
	/** Reports the preflight result without constructing an actor at the call site. */
	EActorSpawnRequestResult Result{EActorSpawnRequestResult::CapacityExceeded};

	/** Identifies the request only when Result is Queued. */
	FActorSpawnHandle Handle{};
};

/** Reports deferred construction completion and the world-owned actor while it remains live. */
struct FActorSpawnStatus final
{
	/** Maps private construction-pending state to Queued so no unpublished actor escapes. */
	EActorSpawnState State{EActorSpawnState::Stale};

	/** Holds object construction outcome only; BeginPlay errors remain ApplyPending results. */
	EObjectResult CompletionResult{EObjectResult::StaleHandle};

	/** Resolves only after the actor becomes a world-owned live entry. */
	TObjectPtr<AActor> Actor{};
};

/** Lets custom deferred factory capture wrappers retain managed references during collection. */
template<typename T>
struct TDeferredActorSpawnCaptureTraits
{
	/** Default captures do not carry traced managed references. */
	static void Visit(const T&, FReferenceCollector&) noexcept {}
};

/** Retains a directly captured managed pointer until its queued factory executes or fails. */
template<typename T>
struct TDeferredActorSpawnCaptureTraits<TObjectPtr<T>>
{
	/** Presents the direct captured reference to the active collector. */
	static void Visit(const TObjectPtr<T>& InReference, FReferenceCollector& InCollector) noexcept { InCollector.AddReferencedObject(InReference); }
};

namespace DeferredActorSpawnDetail
{

	/** Type-erases invocation, destruction, reference tracing, and descriptor resolution for one inline factory. */
	struct FFactoryOperations final
	{
		using FInvoke = TObjectCreationResult<AActor> (*)(void*, FObjectStore&, const FClassDescriptor&) noexcept;
		using FDestroy = void (*)(void*) noexcept;
		using FVisitReferences = void (*)(const void*, FReferenceCollector&) noexcept;
		using FResolveDescriptor = EObjectResult (*)(FClassRegistryRegistrationView, const FClassDescriptor*&) noexcept;

		FInvoke Invoke{nullptr};
		FDestroy Destroy{nullptr};
		FVisitReferences VisitReferences{nullptr};
		FResolveDescriptor ResolveDescriptor{nullptr};
	};

	/** Owns decayed constructor values until a safe world barrier performs managed construction. */
	template<typename TActor, typename... TArguments>
	class TActorFactory final
	{
	public:
	/** Captures decayed constructor values only after every non-mutating queue preflight has passed. */
	explicit TActorFactory(TArguments... InArguments) noexcept : Arguments(std::move(InArguments)...) {}

		/** Uses the canonical registered descriptor to construct the actor in the world's store. */
		static TObjectCreationResult<AActor> Invoke(void* const InFactory, FObjectStore& InStore, const FClassDescriptor& InDescriptor) noexcept
		{
			TActorFactory& Factory = *static_cast<TActorFactory*>(InFactory);
			return InvokeWithArguments(Factory, InStore, InDescriptor, std::index_sequence_for<TArguments...>{});
		}

		/** Destroys moved constructor values exactly once after construction or terminal failure. */
		static void Destroy(void* const InFactory) noexcept { static_cast<TActorFactory*>(InFactory)->~TActorFactory(); }

		/** Traces direct managed pointer captures while the queued factory owns them. */
		static void VisitReferences(const void* const InFactory, FReferenceCollector& InCollector) noexcept
		{
			const TActorFactory& Factory = *static_cast<const TActorFactory*>(InFactory);
			std::apply(
				[&InCollector](const TArguments&... InCapturedArguments) noexcept
				{ (TDeferredActorSpawnCaptureTraits<TArguments>::Visit(InCapturedArguments, InCollector), ...); },
				Factory.Arguments);
		}

		/** Reuses a manually registered descriptor or registers a direct AActor child with a local automatic ID. */
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
		/** Expands the retained tuple at the only safe managed-construction point. */
		template<std::size_t... Indices>
		static TObjectCreationResult<AActor> InvokeWithArguments(
			TActorFactory& InFactory, FObjectStore& InStore, const FClassDescriptor& InDescriptor, std::index_sequence<Indices...>) noexcept
		{
			const TObjectCreationResult<TActor> Creation =
				InStore.NewObject<TActor>(InDescriptor, std::move(std::get<Indices>(InFactory.Arguments))...);
			return TObjectCreationResult<AActor>{Creation.Result, TObjectPtr<AActor>{Creation.Object}};
		}

		/** Holds decayed constructor values across the caller callback and barrier boundary. */
		std::tuple<TArguments...> Arguments;
	};

} // namespace DeferredActorSpawnDetail

/** Defines the non-template storage operations UWorld needs after factory type erasure. */
class IDeferredActorSpawnStorage
{
public:
	/** Keeps derived caller-owned storage valid while its world holds this borrowed interface. */
	virtual ~IDeferredActorSpawnStorage() noexcept = default;

	/** Reserves one reusable fixed request slot after World finishes capacity and lifecycle preflight. */
	virtual FActorSpawnHandle Reserve() noexcept = 0;

	/** Publishes erased operations for a factory already placement-constructed in the reserved slot. */
	virtual void Activate(FActorSpawnHandle InHandle, const DeferredActorSpawnDetail::FFactoryOperations& InOperations) noexcept = 0;

	/** Reports requests that still consume future actor-registry capacity. */
	virtual std::size_t PendingCount() const noexcept = 0;

	/** Reports caller-selected inline factory bytes for non-mutating template layout preflight. */
	virtual std::size_t InlineBytes() const noexcept = 0;

	/** Reports the public completion state for one generation-checked request handle. */
	virtual FActorSpawnStatus GetStatus(FActorSpawnHandle InHandle) const noexcept = 0;

	/** Seals both FIFO lanes before callback dispatch makes new requests possible. */
	virtual void SealBarrier() noexcept = 0;

	/** Returns the number of queued-factory tickets sealed for the current barrier. */
	virtual std::size_t SealedFactoryCount() const noexcept = 0;

	/** Returns one sealed queued-factory ticket. */
	virtual FActorSpawnHandle SealedFactoryAt(std::size_t InIndex) const noexcept = 0;

	/** Constructs exactly one sealed factory or records its terminal object result. */
	virtual void Construct(FActorSpawnHandle InHandle, FObjectStore& InStore, FClassRegistryRegistrationView InClasses) noexcept = 0;

	/** Returns the ordered publish tickets: old pending actors first, then this barrier's constructions. */
	virtual std::size_t SealedPublishCount() const noexcept = 0;

	/** Returns one sealed constructed-pending-publish ticket. */
	virtual FActorSpawnHandle SealedPublishAt(std::size_t InIndex) const noexcept = 0;

	/** Returns a retained actor only when the ticket still names an unpublished constructed request. */
	virtual TObjectPtr<AActor> GetConstructedActor(FActorSpawnHandle InHandle) const noexcept = 0;

	/** Changes one constructed actor request to Spawned after world ownership and BeginPlay dispatch. */
	virtual void CompletePublish(FActorSpawnHandle InHandle) noexcept = 0;

	/** Restores unprocessed constructed actors to the front of the next barrier's publish FIFO. */
	virtual void RestoreUnpublishedFrom(std::size_t InStartIndex) noexcept = 0;

	/** Restores unconstructed factory tickets when an active collector blocks Phase 1. */
	virtual void RestoreUnconstructedFrom(std::size_t InStartIndex) noexcept = 0;

	/** Releases exactly the pinned spawned request whose actor leaves the world registry. */
	virtual void ReleaseActor(FObjectHandle InActorHandle) noexcept = 0;

	/** Traces queued captures and temporarily retained constructed actors. */
	virtual void VisitReferences(FReferenceCollector& InCollector) noexcept = 0;

	/** Returns writable inline bytes for a valid reserved request slot. */
	virtual void* GetFactoryStorage(FActorSpawnHandle InHandle) noexcept = 0;
};

/** Move-only borrowed capability over one caller-owned deferred factory storage owner. */
class FDeferredActorSpawnStorageReference final
{
public:
	/** Creates an empty capability for direct UWorld consumers that do not configure typed spawning. */
	FDeferredActorSpawnStorageReference() noexcept = default;

	/** Transfers one world-only mutable capability and invalidates the source. */
	FDeferredActorSpawnStorageReference(FDeferredActorSpawnStorageReference&& Other) noexcept : Storage(Other.Storage) { Other.Storage = nullptr; }

	/** Prevents two worlds from mutating one queue and completion history. */
	FDeferredActorSpawnStorageReference(const FDeferredActorSpawnStorageReference&) = delete;

	/** Prevents rebinding a world's storage after construction. */
	FDeferredActorSpawnStorageReference& operator=(const FDeferredActorSpawnStorageReference&) = delete;

	/** Prevents replacing a world's borrowed storage after construction. */
	FDeferredActorSpawnStorageReference& operator=(FDeferredActorSpawnStorageReference&&) = delete;

	/** Reports whether this reference still points to caller-owned storage. */
	bool IsValid() const noexcept { return Storage != nullptr; }

	/** Forwards one checked request-slot reservation. */
	FActorSpawnHandle Reserve() noexcept { return Storage != nullptr ? Storage->Reserve() : FActorSpawnHandle{}; }

	/** Returns the raw inline bytes reserved for one factory capture. */
	void* GetFactoryStorage(const FActorSpawnHandle InHandle) noexcept { return Storage != nullptr ? Storage->GetFactoryStorage(InHandle) : nullptr; }

	/** Publishes type-erased factory behavior after placement capture succeeds. */
	void Activate(const FActorSpawnHandle InHandle, const DeferredActorSpawnDetail::FFactoryOperations& InOperations) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->Activate(InHandle, InOperations);
		}
	}

	/** Reports only queued or construction-pending requests that still need registry capacity. */
	std::size_t PendingCount() const noexcept { return Storage != nullptr ? Storage->PendingCount() : 0; }

	/** Reports factory storage extent, or zero when this World was not configured for typed spawning. */
	std::size_t InlineBytes() const noexcept { return Storage != nullptr ? Storage->InlineBytes() : 0; }

	/** Returns bounded completion state without exposing storage. */
	FActorSpawnStatus GetStatus(const FActorSpawnHandle InHandle) const noexcept
	{
		return Storage != nullptr ? Storage->GetStatus(InHandle) : FActorSpawnStatus{};
	}

	/** Seals the current barrier's immutable request snapshots. */
	void SealBarrier() noexcept
	{
		if (Storage != nullptr)
		{
			Storage->SealBarrier();
		}
	}

	/** Returns current sealed queued-factory ticket count. */
	std::size_t SealedFactoryCount() const noexcept { return Storage != nullptr ? Storage->SealedFactoryCount() : 0; }

	/** Returns one sealed queued-factory ticket. */
	FActorSpawnHandle SealedFactoryAt(const std::size_t InIndex) const noexcept
	{
		return Storage != nullptr ? Storage->SealedFactoryAt(InIndex) : FActorSpawnHandle{};
	}

	/** Constructs one queued factory at the barrier. */
	void Construct(const FActorSpawnHandle InHandle, FObjectStore& InStore, const FClassRegistryRegistrationView InClasses) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->Construct(InHandle, InStore, InClasses);
		}
	}

	/** Returns current sealed publish ticket count. */
	std::size_t SealedPublishCount() const noexcept { return Storage != nullptr ? Storage->SealedPublishCount() : 0; }

	/** Returns one sealed publish ticket. */
	FActorSpawnHandle SealedPublishAt(const std::size_t InIndex) const noexcept
	{
		return Storage != nullptr ? Storage->SealedPublishAt(InIndex) : FActorSpawnHandle{};
	}

	/** Resolves one retained constructed actor without publishing it. */
	TObjectPtr<AActor> GetConstructedActor(const FActorSpawnHandle InHandle) const noexcept
	{
		return Storage != nullptr ? Storage->GetConstructedActor(InHandle) : TObjectPtr<AActor>{};
	}

	/** Completes world publication for one constructed actor. */
	void CompletePublish(const FActorSpawnHandle InHandle) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->CompletePublish(InHandle);
		}
	}

	/** Keeps every unprocessed constructed actor available for the next barrier after a guard rejection. */
	void RestoreUnpublishedFrom(const std::size_t InStartIndex) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->RestoreUnpublishedFrom(InStartIndex);
		}
	}

	/** Keeps sealed factories in FIFO order for a later safe construction barrier. */
	void RestoreUnconstructedFrom(const std::size_t InStartIndex) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->RestoreUnconstructedFrom(InStartIndex);
		}
	}

	/** Releases the handle state that was pinned by a removed world actor. */
	void ReleaseActor(const FObjectHandle InActorHandle) noexcept
	{
		if (Storage != nullptr)
		{
			Storage->ReleaseActor(InActorHandle);
		}
	}

	/** Presents queued captures and unpublished actors to collection. */
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

	/** Creates one valid capability only from its matching caller-owned storage owner. */
	explicit FDeferredActorSpawnStorageReference(IDeferredActorSpawnStorage& InStorage) noexcept : Storage(&InStorage) {}

	/** Identifies caller-owned request storage whose lifetime encloses the World. */
	IDeferredActorSpawnStorage* Storage{nullptr};
};

/** Owns all bounded factory storage, FIFO tickets, and completion history for one world. */
template<std::size_t MaxRequests, std::size_t InlineFactoryBytes>
class TDeferredActorSpawnStorage final : public IDeferredActorSpawnStorage
{
public:
	static_assert(MaxRequests <= static_cast<std::size_t>(FActorSpawnHandle::InvalidIndex), "Deferred request count must fit FActorSpawnHandle.");
	static_assert(InlineFactoryBytes > 0, "Deferred factories require non-zero inline storage.");

	/** Preserves every raw factory address retained by a world reference. */
	TDeferredActorSpawnStorage() noexcept = default;

	/** Destroys any queued captures before caller-owned bytes disappear. */
	~TDeferredActorSpawnStorage() noexcept override
	{
		for (FSlot& Slot : Slots)
		{
			DestroyFactory(Slot);
		}
	}

	/** Prevents copying stable factory storage and request generations. */
	TDeferredActorSpawnStorage(const TDeferredActorSpawnStorage&) = delete;
	TDeferredActorSpawnStorage& operator=(const TDeferredActorSpawnStorage&) = delete;
	TDeferredActorSpawnStorage(TDeferredActorSpawnStorage&&) = delete;
	TDeferredActorSpawnStorage& operator=(TDeferredActorSpawnStorage&&) = delete;

	/** Transfers the one mutable queue capability to one world. */
	FDeferredActorSpawnStorageReference MakeReference() & noexcept
	{
		if (bReferenceMade)
		{
			return FDeferredActorSpawnStorageReference{};
		}
		bReferenceMade = true;
		return FDeferredActorSpawnStorageReference(*this);
	}

	/** Prevents a temporary storage owner from escaping into a world. */
	FDeferredActorSpawnStorageReference MakeReference() && = delete;

	/** Reserves the first terminal or never-used slot without constructing any capture. */
	FActorSpawnHandle Reserve() noexcept override
	{
		for (std::size_t Index = 0; Index < MaxRequests; ++Index)
		{
			FSlot& Slot = Slots[Index];
			if (Slot.State != ESlotState::Free && Slot.State != ESlotState::Failed && Slot.State != ESlotState::Released)
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

	/** Returns raw bytes only while the exact request generation remains queued. */
	void* GetFactoryStorage(const FActorSpawnHandle InHandle) noexcept override
	{
		FSlot* const Slot = FindSlot(InHandle);
		return Slot != nullptr && Slot->State == ESlotState::Queued ? Slot->FactoryBytes.data() : nullptr;
	}

	/** Activates one factory and appends its immutable handle to the next barrier FIFO. */
	void Activate(const FActorSpawnHandle InHandle, const DeferredActorSpawnDetail::FFactoryOperations& InOperations) noexcept override
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

	/** Counts only factory and publish-pending requests that are not yet live actors. */
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

	/** Reports the compile-time inline factory extent owned by this caller-provided storage. */
	std::size_t InlineBytes() const noexcept override { return InlineFactoryBytes; }

	/** Maps each internal slot state to the public handle contract. */
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

	/** Freezes both FIFO lanes before any barrier callback can append new work. */
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

	/** Reports the frozen factory request count. */
	std::size_t SealedFactoryCount() const noexcept override { return SealedFactoryCountValue; }

	/** Returns a frozen factory ticket or an invalid handle for an out-of-range query. */
	FActorSpawnHandle SealedFactoryAt(const std::size_t InIndex) const noexcept override
	{
		return InIndex < SealedFactoryCountValue ? SealedFactoryQueue[InIndex] : FActorSpawnHandle{};
	}

	/** Resolves descriptor identity, constructs safely, then retains the actor for guarded publication. */
	void Construct(const FActorSpawnHandle InHandle, FObjectStore& InStore, const FClassRegistryRegistrationView InClasses) noexcept override
	{
		FSlot* const Slot = FindSlot(InHandle);
		if (Slot == nullptr || Slot->State != ESlotState::Queued || Slot->Operations.Invoke == nullptr
			|| Slot->Operations.ResolveDescriptor == nullptr)
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

	/** Reports the ordered frozen publish list, including this barrier's successful constructions. */
	std::size_t SealedPublishCount() const noexcept override { return SealedPublishCountValue; }

	/** Returns one frozen publish ticket or invalid for an out-of-range query. */
	FActorSpawnHandle SealedPublishAt(const std::size_t InIndex) const noexcept override
	{
		return InIndex < SealedPublishCountValue ? SealedPublishQueue[InIndex] : FActorSpawnHandle{};
	}

	/** Returns a retained actor while it is intentionally hidden from the public handle state. */
	TObjectPtr<AActor> GetConstructedActor(const FActorSpawnHandle InHandle) const noexcept override
	{
		const FSlot* const Slot = FindSlot(InHandle);
		return Slot != nullptr && Slot->State == ESlotState::ConstructedPendingPublish ? Slot->Actor : TObjectPtr<AActor>{};
	}

	/** Pins a successful request until the actor later leaves the live world registry. */
	void CompletePublish(const FActorSpawnHandle InHandle) noexcept override
	{
		FSlot* const Slot = FindSlot(InHandle);
		if (Slot != nullptr && Slot->State == ESlotState::ConstructedPendingPublish)
		{
			Slot->State = ESlotState::Spawned;
		}
	}

	/** Restores the unfinished suffix in FIFO order after publication cannot acquire a dispatch guard. */
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

	/** Restores sealed factories in FIFO order when construction remains unsafe during an active collection. */
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

	/** Releases only the pinned request that names an actor removed from this World. */
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

	/** Keeps queued managed captures and unpublished actors reachable through one World trace edge. */
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
	/** Distinguishes reusable slots from every private request and publish phase. */
	enum class ESlotState : std::uint8_t
	{
		Free,
		Queued,
		ConstructedPendingPublish,
		Spawned,
		Failed,
		Released,
		Retired,
	};

	/** Holds one request's lifetime state, exact factory bytes, and temporary or live actor identity. */
	struct alignas(std::max_align_t) FSlot final
	{
		/** Tracks generation-checked request identity independently of object-store slots. */
		std::uint32_t Generation{0};

		/** Keeps a factory or actor in the phase that controls reclamation and handle status. */
		ESlotState State{ESlotState::Free};

		/** Preserves the construction-only terminal result after factory destruction. */
		EObjectResult CompletionResult{EObjectResult::Success};

		/** Retains constructed and spawned actors without exposing construction-pending objects publicly. */
		TObjectPtr<AActor> Actor{};

		/** Erases factory behavior without extending Core delegate policy. */
		DeferredActorSpawnDetail::FFactoryOperations Operations{};

		/** Holds one caller-selected-size factory without heap allocation. */
		std::array<std::byte, InlineFactoryBytes> FactoryBytes{};
	};

	/** Returns a slot only when the caller supplies its current valid generation. */
	FSlot* FindSlot(const FActorSpawnHandle InHandle) noexcept
	{
		return InHandle.IsValid() && InHandle.Index < MaxRequests && Slots[InHandle.Index].Generation == InHandle.Generation ? &Slots[InHandle.Index]
																															 : nullptr;
	}

	/** Provides a const generation-checked slot lookup for query and tracing paths. */
	const FSlot* FindSlot(const FActorSpawnHandle InHandle) const noexcept
	{
		return InHandle.IsValid() && InHandle.Index < MaxRequests && Slots[InHandle.Index].Generation == InHandle.Generation ? &Slots[InHandle.Index]
																															 : nullptr;
	}

	/** Destroys active factory capture state and clears callable operations exactly once. */
	static void DestroyFactory(FSlot& InSlot) noexcept
	{
		if (InSlot.Operations.Destroy != nullptr)
		{
			InSlot.Operations.Destroy(InSlot.FactoryBytes.data());
			InSlot.Operations = {};
		}
	}

	/** Records an exact construction failure after safely releasing the factory capture. */
	static void CompleteFailure(FSlot& InSlot, const EObjectResult InResult) noexcept
	{
		DestroyFactory(InSlot);
		InSlot.Actor = {};
		InSlot.CompletionResult = InResult;
		InSlot.State = ESlotState::Failed;
	}

	/** Holds all request slots at fixed capacity for one World lifetime. */
	std::array<FSlot, MaxRequests> Slots{};

	/** Queues factories accepted after the current barrier seal. */
	std::array<FActorSpawnHandle, MaxRequests> FactoryQueue{};

	/** Counts queued factories not yet copied into a barrier snapshot. */
	std::size_t FactoryQueueCount{0};

	/** Queues constructed actors retained for a later guarded publication phase. */
	std::array<FActorSpawnHandle, MaxRequests> PublishQueue{};

	/** Counts constructed actors awaiting a future barrier snapshot. */
	std::size_t PublishQueueCount{0};

	/** Freezes factory work that callbacks must not extend in this barrier. */
	std::array<FActorSpawnHandle, MaxRequests> SealedFactoryQueue{};

	/** Counts sealed factories available to Phase 1. */
	std::size_t SealedFactoryCountValue{0};

	/** Holds pre-existing publish tickets first, followed by new Phase 1 constructions. */
	std::array<FActorSpawnHandle, MaxRequests> SealedPublishQueue{};

	/** Counts ordered Phase 2 publication tickets. */
	std::size_t SealedPublishCountValue{0};

	/** Ensures one caller-owned storage instance cannot back two Worlds. */
	bool bReferenceMade{false};
};

} // namespace MicroWorld
