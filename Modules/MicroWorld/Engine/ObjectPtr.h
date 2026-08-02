#pragma once

#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectResult.h>
#include <MicroWorld/Engine/IsManagedObjectPointerConversion.h>

#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

class FObjectStore;

template<typename>
class TWeakObjectPtr;

/**
 * Motivation: Lets a traced reference resolve its target through one generation-checked entry point without changing store state.
 * Responsibilities: Return a UObject pointer for a live, matching-generation slot or null, touching no store occupancy.
 */
UObject* ResolveObjectHandle(const FObjectStore& InStore, FObjectHandle InHandle) noexcept;

/**
 * Motivation: Lets a caller keep one managed object alive as an explicit root after capacity validation.
 * Responsibilities: Reserve one root-table entry for the handle or report why it cannot.
 */
EObjectResult AddObjectRoot(FObjectStore& InStore, FObjectHandle InHandle) noexcept;

/**
 * Motivation: Lets RAII cleanup release one root token immediately, even while guarded callbacks run.
 * Responsibilities: Free the root-table entry exactly once and tolerate a guard being held.
 */
void ReleaseObjectRoot(FObjectStore& InStore, FObjectHandle InHandle) noexcept;

/**
 * Motivation: Holds one traced managed reference that contributes reachability only when an object's reference visitor
 *   presents it, without implicitly rooting its target.
 * Responsibilities: Carry store and generation-checked identity, resolve on every access without caching an address, and
 *   confine conversion to managed same-type or derived-to-base endpoints.
 * Example:
 *   TObjectPtr<UActorComponent> Comp;
 *   if (Comp.Get() != nullptr) { Comp->Tick(); }
 */
template<typename T>
class TObjectPtr
{
public:
	/**
	 * Motivation: Lets a caller default-construct a traced reference that resolves to null.
	 * Responsibilities: Produce an empty reference that consumes no store or root-table resource.
	 */
	TObjectPtr() noexcept = default;

	/**
	 * Motivation: Adopts one traced identity from a reference whose target is convertible to T.
	 * Responsibilities: Preserve store and generation so a derived-to-base reference keeps tracing the same live object,
	 *   limiting static conversion to managed same-type or derived-to-base endpoints with no runtime narrowing.
	 */
	template<typename U, typename = std::enable_if_t<TIsManagedObjectPointerConversion<U, T>::value>>
	TObjectPtr(const TObjectPtr<U>& InOther) noexcept : Store(InOther.Store), TargetHandle(InOther.TargetHandle)
	{
	}

	/**
	 * Motivation: Lets a caller dereference the traced reference on every access without a stale cached address.
	 * Responsibilities: Resolve index plus generation and return T* for a live object or null.
	 */
	T* Get() const noexcept
	{
		if (Store == nullptr)
		{
			return nullptr;
		}
		return static_cast<T*>(ResolveObjectHandle(*Store, TargetHandle));
	}

	/**
	 * Motivation: Lets explicit tracing and diagnostics carry the reference's local stable identity.
	 * Responsibilities: Return the stored handle without resolving or changing reachability.
	 */
	FObjectHandle Handle() const noexcept { return TargetHandle; }

	/**
	 * Motivation: Lets a collector confirm that a reference belongs to the store performing traversal.
	 * Responsibilities: Compare the owning store pointer and nothing else.
	 */
	bool BelongsTo(const FObjectStore& InObjectStore) const noexcept { return Store == &InObjectStore; }

	/**
	 * Motivation: Lets a caller branch on whether the reference currently resolves to a live, non-pending object.
	 * Responsibilities: Report true only when Get returns a non-null live object.
	 */
	explicit operator bool() const noexcept { return Get() != nullptr; }

private:
	friend class FObjectStore;
	template<typename>
	friend class TWeakObjectPtr;
	template<typename>
	friend class TObjectPtr;

	/**
	 * Motivation: Lets the store hand out a typed reference only after it publishes a matching object lifetime.
	 * Responsibilities: Bind store and handle without validating, since the store has already validated them.
	 */
	TObjectPtr(FObjectStore& InObjectStore, const FObjectHandle InObjectHandle) noexcept : Store(&InObjectStore), TargetHandle(InObjectHandle) {}

	/** Motivation: Identifies the store that owns handle validation and object storage. */
	FObjectStore* Store{nullptr};

	/** Motivation: Retains stable identity without retaining or exposing a raw object address. */
	FObjectHandle TargetHandle{};
};

} // namespace MicroWorld::Engine
