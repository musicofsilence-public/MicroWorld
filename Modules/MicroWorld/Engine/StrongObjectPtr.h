#pragma once

#include <MicroWorld/Engine/Object.h>
#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>

namespace MicroWorld::Engine
{

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

} // namespace MicroWorld::Engine
