#pragma once

#include <MicroWorld/Object/Object.h>

#include <type_traits>
#include <utility>

namespace MicroWorld
{

class FObjectStore;

namespace ObjectDetail
{

	/** Enables conversions only after both complete endpoints prove managed ancestry. */
	template<typename From, typename To, typename = void>
	struct TIsManagedObjectPointerConversion : std::false_type
	{
	};

	/** Accepts an accessible derived-to-base or same-type conversion between managed types. */
	template<typename From, typename To>
	struct TIsManagedObjectPointerConversion<
		From,
		To,
		std::void_t<
			decltype(static_cast<UObject*>(std::declval<typename std::remove_cv<From>::type*>())),
			decltype(static_cast<UObject*>(std::declval<typename std::remove_cv<To>::type*>()))>> : std::is_convertible<From*, To*>
	{
	};

} // namespace ObjectDetail

/** Resolves one generation-checked handle without changing object-store state. */
UObject* ResolveObjectHandle(const FObjectStore& Store, FObjectHandle Handle) noexcept;

/** Registers one independently owned explicit root after capacity validation. */
EObjectResult AddObjectRoot(FObjectStore& Store, FObjectHandle Handle) noexcept;

/** Releases one root token immediately, including during guarded callback cleanup. */
void ReleaseObjectRoot(FObjectStore& Store, FObjectHandle Handle) noexcept;

/**
 * Holds a traced managed reference without implicitly rooting its target.
 *
 * The pointer contributes reachability only when a managed object's explicit
 * reference visitor presents it to FReferenceCollector.
 */
template<typename T>
class TObjectPtr
{
public:
	/** Creates an empty traced reference that resolves to null. */
	TObjectPtr() noexcept = default;

	/**
	 * Adopts one traced identity from a reference whose target is convertible to T.
	 *
	 * The conversion preserves store and generation so a derived-to-base reference
	 * keeps tracing the same live object. The constraint independently limits
	 * static type conversion to managed same-type or derived-to-base endpoints;
	 * it does not perform runtime narrowing or reflection.
	 */
	template<typename U, typename = std::enable_if_t<ObjectDetail::TIsManagedObjectPointerConversion<U, T>::value>>
	TObjectPtr(const TObjectPtr<U>& Other) noexcept : Store(Other.Store), Object(Other.Object)
	{
	}

	/** Resolves index plus generation on every access and never caches a slot address. */
	T* Get() const noexcept
	{
		if (Store == nullptr)
		{
			return nullptr;
		}
		return static_cast<T*>(ResolveObjectHandle(*Store, Object));
	}

	/** Returns local stable identity for explicit tracing and diagnostics. */
	FObjectHandle Handle() const noexcept { return Object; }

	/** Confirms that this reference belongs to the store performing traversal. */
	bool BelongsTo(const FObjectStore& ObjectStore) const noexcept { return Store == &ObjectStore; }

	/** Reports whether the handle currently resolves to a live, non-pending object. */
	explicit operator bool() const noexcept { return Get() != nullptr; }

private:
	friend class FObjectStore;
	template<typename>
	friend class TWeakObjectPtr;
	template<typename>
	friend class TObjectPtr;

	/** Creates a typed reference only after the store publishes a matching object lifetime. */
	TObjectPtr(FObjectStore& ObjectStore, const FObjectHandle ObjectHandle) noexcept : Store(&ObjectStore), Object(ObjectHandle) {}

	/** Identifies the store that owns handle validation and object storage. */
	FObjectStore* Store{nullptr};

	/** Retains stable identity without retaining or exposing a raw object address. */
	FObjectHandle Object{};
};

/** Observes one managed object without tracing or keeping it reachable. */
template<typename T>
class TWeakObjectPtr
{
public:
	/** Creates an expired weak observation. */
	TWeakObjectPtr() noexcept = default;

	/** Observes the same store and identity without registering a root. */
	explicit TWeakObjectPtr(const TObjectPtr<T> ObjectPointer) noexcept : Store(ObjectPointer.Store), Object(ObjectPointer.Object) {}

	/** Resolves index plus generation on every access and returns null after expiry. */
	T* Get() const noexcept
	{
		if (Store == nullptr)
		{
			return nullptr;
		}
		return static_cast<T*>(ResolveObjectHandle(*Store, Object));
	}

	/** Reports reclaimed, pending-destroy, retired, stale, and empty observations uniformly. */
	bool IsExpired() const noexcept { return Get() == nullptr; }

	/** Returns local stable identity for diagnostics without changing reachability. */
	FObjectHandle Handle() const noexcept { return Object; }

private:
	/** Identifies the store that owns generation validation. */
	FObjectStore* Store{nullptr};

	/** Retains observation identity without retaining a slot address. */
	FObjectHandle Object{};
};

/** Owns exactly one independently counted explicit object-store root token. */
template<typename T>
class TStrongObjectPtr final
{
public:
	/** Creates an empty root owner that consumes no root-table capacity. */
	TStrongObjectPtr() noexcept = default;

	/** Releases exactly this token even during guarded callbacks so RAII cleanup cannot leak it. */
	~TStrongObjectPtr() noexcept { Reset(); }

	/** Prevents two owners from releasing one root token. */
	TStrongObjectPtr(const TStrongObjectPtr&) = delete;

	/** Prevents assigning two owners to one root token. */
	TStrongObjectPtr& operator=(const TStrongObjectPtr&) = delete;

	/** Transfers one root token without changing root-table occupancy. */
	TStrongObjectPtr(TStrongObjectPtr&& Other) noexcept : Store(Other.Store), Object(Other.Object)
	{
		Other.Store = nullptr;
		Other.Object = {};
	}

	/** Releases the current token, then transfers one token from Other. */
	TStrongObjectPtr& operator=(TStrongObjectPtr&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		Reset();
		Store = Other.Store;
		Object = Other.Object;
		Other.Store = nullptr;
		Other.Object = {};
		return *this;
	}

	/** Resolves the rooted identity without caching a slot address. */
	T* Get() const noexcept
	{
		if (Store == nullptr)
		{
			return nullptr;
		}
		return static_cast<T*>(ResolveObjectHandle(*Store, Object));
	}

	/** Returns local stable identity without changing root ownership. */
	FObjectHandle Handle() const noexcept { return Object; }

	/** Reports whether this instance currently owns a resolvable root token. */
	explicit operator bool() const noexcept { return Get() != nullptr; }

	/** Immediately releases this token without triggering destruction or collection. */
	void Reset() noexcept
	{
		if (Store != nullptr)
		{
			ReleaseObjectRoot(*Store, Object);
			Store = nullptr;
			Object = {};
		}
	}

private:
	friend class FObjectStore;

	/** Adopts one token only after the store's fallible AddRoot operation succeeds. */
	TStrongObjectPtr(FObjectStore& ObjectStore, const FObjectHandle ObjectHandle) noexcept : Store(&ObjectStore), Object(ObjectHandle) {}

	/** Identifies the store holding this instance's independently counted root. */
	FObjectStore* Store{nullptr};

	/** Retains the rooted lifetime identity without retaining a raw object address. */
	FObjectHandle Object{};
};

/** Couples explicit root-registration failure with optional strong ownership. */
template<typename T>
struct TStrongObjectPointerResult
{
	/** Reports root capacity, stale identity, pending destruction, or success. */
	EObjectResult Result{EObjectResult::RootCapacityExceeded};

	/** Owns one root token only when Result is Success. */
	TStrongObjectPtr<T> Pointer{};
};

} // namespace MicroWorld
