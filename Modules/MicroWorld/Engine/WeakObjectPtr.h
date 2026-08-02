#pragma once

#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>

namespace MicroWorld::Engine
{

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

} // namespace MicroWorld::Engine
