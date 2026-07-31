#pragma once

#include <MicroWorld/Engine/ClassDescriptor.h>
#include <MicroWorld/Engine/ObjectHandle.h>

namespace MicroWorld::Engine
{

class FGarbageCollector;
class FObjectStore;
class FReferenceCollector;
class UObject;

/**
 * Motivation: Lets the default class descriptor dispatch a managed object's protected reference visitor to the collector.
 * Responsibilities: Call the object's VisitReferences through the tracer signature the descriptor stores.
 */
void TraceManagedObjectReferences(UObject& InObject, FReferenceCollector& InCollector) noexcept;

/**
 * Motivation: Gives every managed object store-owned identity, tracing, and deferred destruction so applications never
 *   delete a UObject directly.
 * Responsibilities: Hold store, handle, and descriptor identity stamped on publish, expose reference visiting and
 *   BeginDestroy for the store and collector, and keep deletion behind the descriptor/store boundary.
 * Example:
 *   UObject& Object = *Store.Resolve(Handle);
 *   if (Object.IsPendingDestroy()) { Skip(); }
 */
class UObject
{
public:
	/**
	 * Motivation: Prevents copying store identity into storage the store does not own.
	 * Responsibilities: Reject copy construction so each identity stays with its slot.
	 */
	UObject(const UObject&) = delete;

	/**
	 * Motivation: Prevents replacing one published store identity with another.
	 * Responsibilities: Reject copy assignment so each identity stays with its slot.
	 */
	UObject& operator=(const UObject&) = delete;

	/**
	 * Motivation: Prevents moving a managed object away from its stable slot.
	 * Responsibilities: Reject move construction so slot identity stays fixed.
	 */
	UObject(UObject&&) = delete;

	/**
	 * Motivation: Prevents moving another identity into this managed object's stable slot.
	 * Responsibilities: Reject move assignment so slot identity stays fixed.
	 */
	UObject& operator=(UObject&&) = delete;

	/**
	 * Motivation: Lets a caller read the generation-checked local identity assigned after construction.
	 * Responsibilities: Return the stored handle without resolving or mutating state.
	 */
	FObjectHandle GetObjectHandle() const noexcept { return Handle; }

	/**
	 * Motivation: Lets a caller read the explicit no-RTTI descriptor that owns tracing and destruction.
	 * Responsibilities: Return the descriptor stamped at publish.
	 */
	const FClassDescriptor& GetClassDescriptor() const noexcept { return *Descriptor; }

	/**
	 * Motivation: Lets a caller branch on whether the destruction barrier has made this object unreachable.
	 * Responsibilities: Report the pending-destroy flag and nothing else.
	 */
	bool IsPendingDestroy() const noexcept { return bPendingDestroy; }

protected:
	/**
	 * Motivation: Allows only derived managed classes to construct inside store-selected storage.
	 * Responsibilities: Default-construct with null identity until the store publishes it.
	 */
	UObject() noexcept = default;

	/**
	 * Motivation: Keeps deletion behind the descriptor/store boundary while supporting exact derived destruction.
	 * Responsibilities: Default the virtual destructor so the store's exact destructor pointer runs derived teardown.
	 */
	virtual ~UObject() noexcept = default;

	/**
	 * Motivation: Lets a derived object reach the canonical store assigned after publication, or null during unmanaged
	 *   construction.
	 * Responsibilities: Return the stored store pointer without resolving handles.
	 */
	FObjectStore* GetObjectStore() const noexcept { return Store; }

	/**
	 * Motivation: Exposes outgoing managed references to the iterative collector without reflection.
	 * Responsibilities: Override to register each traced reference with the collector; default owns none.
	 */
	virtual void VisitReferences(FReferenceCollector&) noexcept {}

	/**
	 * Motivation: Lets a derived object release non-managed resources once before exact destruction at the mutation
	 *   barrier.
	 * Responsibilities: Override to release resources; the store invokes it exactly once.
	 */
	virtual void BeginDestroy() noexcept {}

private:
	// The owning store stamps this object's Store, Handle, and Descriptor identity
	// on publish and reads Store to reject foreign handles.
	friend class FObjectStore;

	// The default descriptor tracer calls the protected VisitReferences to hand
	// this object's managed references to the collector during a mark cycle.
	friend void TraceManagedObjectReferences(UObject& InObject, FReferenceCollector& InCollector) noexcept;

	/** Motivation: Identifies the only store allowed to resolve and destroy this object. */
	FObjectStore* Store{nullptr};

	/** Motivation: Retains stable local identity without exposing the slot address. */
	FObjectHandle Handle{};

	/** Motivation: Selects exact tracing, ancestry, layout, and destructor behavior without RTTI. */
	const FClassDescriptor* Descriptor{nullptr};

	/** Motivation: Prevents tracing, rooting, or repeating BeginDestroy after destruction is requested. */
	bool bPendingDestroy{false};
};

} // namespace MicroWorld::Engine
