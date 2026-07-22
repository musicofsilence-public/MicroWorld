#pragma once

#include <MicroWorld/Object/ClassDescriptor.h>
#include <MicroWorld/Object/ObjectHandle.h>

namespace MicroWorld
{

class FGarbageCollector;
class FObjectStore;
class FReferenceCollector;
class UObject;

/** Dispatches the managed object's protected virtual reference visitor for a descriptor callback. */
void TraceManagedObjectReferences(UObject& Object, FReferenceCollector& Collector) noexcept;

/**
 * Provides store-owned identity, tracing, and deferred destruction to managed objects.
 *
 * Applications never delete a UObject. The owning FObjectStore invokes
 * BeginDestroy once and then uses the registered descriptor to call the exact
 * derived destructor at an explicit mutation barrier.
 */
class UObject
{
public:
	/** Prevents copying store identity into storage that the store does not own. */
	UObject(const UObject&) = delete;

	/** Prevents replacing one published store identity with another. */
	UObject& operator=(const UObject&) = delete;

	/** Prevents moving a managed object away from its stable slot. */
	UObject(UObject&&) = delete;

	/** Prevents moving another identity into this managed object's stable slot. */
	UObject& operator=(UObject&&) = delete;

	/** Returns the generation-checked local identity assigned after construction. */
	FObjectHandle GetObjectHandle() const noexcept { return Handle; }

	/** Returns the explicit no-RTTI descriptor that owns tracing and destruction. */
	const FClassDescriptor& GetClassDescriptor() const noexcept { return *Descriptor; }

	/** Reports whether the explicit destruction barrier has made this object unreachable. */
	bool IsPendingDestroy() const noexcept { return bPendingDestroy; }

protected:
	/** Allows only derived managed classes to construct inside store-selected storage. */
	UObject() noexcept = default;

	/** Keeps deletion behind the descriptor/store boundary while supporting exact derived destruction. */
	virtual ~UObject() noexcept = default;

	/** Returns the canonical store assigned after publication, or null during unmanaged construction. */
	FObjectStore* GetObjectStore() const noexcept { return Store; }

	/** Exposes outgoing managed references to the iterative collector without reflection. */
	virtual void VisitReferences(FReferenceCollector&) noexcept {}

	/** Releases non-managed resources once before exact destruction at the mutation barrier. */
	virtual void BeginDestroy() noexcept {}

private:
	friend class FGarbageCollector;
	friend class FObjectStore;
	friend class FReferenceCollector;
	friend void TraceManagedObjectReferences(UObject& Object, FReferenceCollector& Collector) noexcept;

	/** Identifies the only store allowed to resolve and destroy this object. */
	FObjectStore* Store{nullptr};

	/** Retains stable local identity without exposing the slot address. */
	FObjectHandle Handle{};

	/** Selects exact tracing, ancestry, layout, and destructor behavior without RTTI. */
	const FClassDescriptor* Descriptor{nullptr};

	/** Prevents tracing, rooting, or repeating BeginDestroy after destruction is requested. */
	bool bPendingDestroy{false};
};

} // namespace MicroWorld
