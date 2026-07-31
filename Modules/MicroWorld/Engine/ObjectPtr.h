#pragma once

#include <MicroWorld/Engine/Object.h>

#include <type_traits>
#include <utility>

namespace MicroWorld::Engine
{

class FObjectStore;

/**
 * Motivation: Enables pointer conversions only after both endpoints prove managed ancestry, so unmanaged types never
 *   silently become convertible through a generic helper.
 * Responsibilities: Report false unless both From and To derive from UObject, and never perform runtime narrowing.
 * Example:
 *   static_assert(!TIsManagedObjectPointerConversion<int, UObject>::value);
 */
template<typename From, typename To, typename = void>
struct TIsManagedObjectPointerConversion : std::false_type
{
};

/**
 * Motivation: Accepts an accessible derived-to-base or same-type conversion between managed types while rejecting
 *   narrowing and unmanaged endpoints.
 * Responsibilities: Report standard convertibility only when both endpoints are UObject-derived, with no reflection or
 *   runtime check.
 * Example:
 *   static_assert(TIsManagedObjectPointerConversion<UActorComponent, UObject>::value);
 */
template<typename From, typename To>
struct TIsManagedObjectPointerConversion<
	From,
	To,
	std::void_t<
		decltype(static_cast<UObject*>(std::declval<typename std::remove_cv<From>::type*>())),
		decltype(static_cast<UObject*>(std::declval<typename std::remove_cv<To>::type*>()))>> : std::is_convertible<From*, To*>
{
};

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

/**
 * Motivation: Observes one managed object without tracing it or keeping it reachable across collection.
 * Responsibilities: Carry store and generation-checked identity, resolve on every access without caching an address,
 *   and report expiry uniformly for reclaimed, pending-destroy, retired, stale, and empty observations.
 * Example:
 *   TWeakObjectPtr<UActorComponent> Weak(Comp);
 *   if (!Weak.IsExpired()) { Weak.Get()->Tick(); }
 */
template<typename T>
class TWeakObjectPtr
{
public:
	/**
	 * Motivation: Lets a caller default-construct an expired weak observation.
	 * Responsibilities: Produce an observation that resolves to null and registers no root.
	 */
	TWeakObjectPtr() noexcept = default;

	/**
	 * Motivation: Lets a caller observe the same store and identity as a traced reference without registering a root.
	 * Responsibilities: Copy store and handle without changing reachability.
	 */
	explicit TWeakObjectPtr(const TObjectPtr<T> InObjectPointer) noexcept : Store(InObjectPointer.Store), TargetHandle(InObjectPointer.TargetHandle)
	{
	}

	/**
	 * Motivation: Lets a caller dereference the weak observation on every access without a stale cached address.
	 * Responsibilities: Resolve index plus generation and return T* for a live object or null after expiry.
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
	 * Motivation: Lets a caller branch on expiry without distinguishing the cause of unreachability.
	 * Responsibilities: Report true whenever Get returns null.
	 */
	bool IsExpired() const noexcept { return Get() == nullptr; }

	/**
	 * Motivation: Lets diagnostics carry the observation's local stable identity without changing reachability.
	 * Responsibilities: Return the stored handle without resolving or registering anything.
	 */
	FObjectHandle Handle() const noexcept { return TargetHandle; }

private:
	/** Motivation: Identifies the store that owns generation validation. */
	FObjectStore* Store{nullptr};

	/** Motivation: Retains observation identity without retaining a slot address. */
	FObjectHandle TargetHandle{};
};

/**
 * Motivation: Owns exactly one independently counted explicit object-store root token so RAII keeps a target alive
 *   without the holder holding a traced reference.
 * Responsibilities: Acquire one token on construction and release exactly it on destruction, survive copy rejection and
 *   move transfer, and never leak a token even during guarded callbacks.
 * Example:
 *   TStrongObjectPtr<UWorld> World = Store.AcquireRoot(Handle, Result).Pointer;
 */
template<typename T>
class TStrongObjectPtr final
{
public:
	/**
	 * Motivation: Lets a caller default-construct an empty root owner that consumes no root-table capacity.
	 * Responsibilities: Produce an owner with no store and no token.
	 */
	TStrongObjectPtr() noexcept = default;

	/**
	 * Motivation: Ensures the owned token is released exactly once even during guarded callbacks.
	 * Responsibilities: Call Reset and tolerate no further access.
	 */
	~TStrongObjectPtr() noexcept { Reset(); }

	/**
	 * Motivation: Prevents two owners from releasing one root token.
	 * Responsibilities: Reject copy construction so each token has a single owner.
	 */
	TStrongObjectPtr(const TStrongObjectPtr&) = delete;

	/**
	 * Motivation: Prevents assigning two owners to one root token.
	 * Responsibilities: Reject copy assignment so each token has a single owner.
	 */
	TStrongObjectPtr& operator=(const TStrongObjectPtr&) = delete;

	/**
	 * Motivation: Transfers one root token without changing root-table occupancy.
	 * Responsibilities: Move store and handle and leave the source empty.
	 */
	TStrongObjectPtr(TStrongObjectPtr&& Other) noexcept : Store(Other.Store), TargetHandle(Other.TargetHandle)
	{
		Other.Store = nullptr;
		Other.TargetHandle = {};
	}

	/**
	 * Motivation: Releases the current token, then transfers one token from Other.
	 * Responsibilities: Handle self-assignment, release the prior token, and leave the source empty.
	 */
	TStrongObjectPtr& operator=(TStrongObjectPtr&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		Reset();
		Store = Other.Store;
		TargetHandle = Other.TargetHandle;
		Other.Store = nullptr;
		Other.TargetHandle = {};
		return *this;
	}

	/**
	 * Motivation: Lets a caller dereference the rooted identity without caching a slot address.
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
	 * Motivation: Lets diagnostics carry the rooted local stable identity without changing root ownership.
	 * Responsibilities: Return the stored handle without resolving or releasing anything.
	 */
	FObjectHandle Handle() const noexcept { return TargetHandle; }

	/**
	 * Motivation: Lets a caller branch on whether this instance currently owns a resolvable root token.
	 * Responsibilities: Report true only when Get returns a non-null live object.
	 */
	explicit operator bool() const noexcept { return Get() != nullptr; }

	/**
	 * Motivation: Lets a caller release the token immediately without triggering destruction or collection.
	 * Responsibilities: Release the token exactly once and become empty, tolerating an already-empty instance.
	 */
	void Reset() noexcept
	{
		if (Store != nullptr)
		{
			ReleaseObjectRoot(*Store, TargetHandle);
			Store = nullptr;
			TargetHandle = {};
		}
	}

private:
	friend class FObjectStore;

	/**
	 * Motivation: Lets the store hand out a strong owner only after its fallible AddRoot operation succeeds.
	 * Responsibilities: Bind store and handle without validating, since the store has already reserved the token.
	 */
	TStrongObjectPtr(FObjectStore& InObjectStore, const FObjectHandle InObjectHandle) noexcept : Store(&InObjectStore), TargetHandle(InObjectHandle)
	{
	}

	/** Motivation: Identifies the store holding this instance's independently counted root. */
	FObjectStore* Store{nullptr};

	/** Motivation: Retains the rooted lifetime identity without retaining a raw object address. */
	FObjectHandle TargetHandle{};
};

/**
 * Motivation: Couples explicit root-registration failure with optional strong ownership in one return value.
 * Responsibilities: Report the registration outcome and own a strong pointer only when Result is Success.
 * Example:
 *   auto Outcome = Store.AcquireRoot(Handle);
 *   if (Outcome.Result == EObjectResult::Success) { Keep(Outcome.Pointer); }
 */
template<typename T>
struct TStrongObjectPointerResult
{
	/** Motivation: Reports root capacity, stale identity, pending destruction, or success. */
	EObjectResult Result{EObjectResult::RootCapacityExceeded};

	/** Motivation: Owns one root token only when Result is Success. */
	TStrongObjectPtr<T> Pointer{};
};

} // namespace MicroWorld::Engine
