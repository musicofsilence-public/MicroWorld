#pragma once

namespace MicroWorld::Engine
{

class FObjectStore;

/**
 * Motivation: Prevents object publication, destruction, root acquisition, and collection while one bounded callback
 *   cascade runs, so a BeginPlay or EndPlay dispatch cannot be reentered.
 * Responsibilities: Try to reserve the mutation boundary once and release it at scope exit; tolerate acquisition failure
 *   as a non-success rather than blocking, and still allow an existing root token to be released so noexcept RAII cleanup
 *   cannot leak reachability.
 * Example:
 *   FObjectStoreDispatchGuard Guard(Store);
 *   if (Guard.IsAcquired()) { Dispatch(); }
 */
class FObjectStoreDispatchGuard final
{
public:
	/**
	 * Motivation: Tries to exclude lifetime-changing work from one callback cascade.
	 * Responsibilities: Reserve the dispatch boundary or leave the guard not acquired.
	 */
	explicit FObjectStoreDispatchGuard(FObjectStore& InStore) noexcept;

	/**
	 * Motivation: Ensures the callback exclusion is released when the guard leaves scope.
	 * Responsibilities: Release the reservation only when this instance acquired it.
	 */
	~FObjectStoreDispatchGuard() noexcept;

	/**
	 * Motivation: Prevents two guards from releasing one dispatch reservation.
	 * Responsibilities: Reject copy construction so each reservation has one owner.
	 */
	FObjectStoreDispatchGuard(const FObjectStoreDispatchGuard&) = delete;

	/**
	 * Motivation: Prevents replacing this guard's unique dispatch reservation via assignment.
	 * Responsibilities: Reject copy assignment so each reservation has one owner.
	 */
	FObjectStoreDispatchGuard& operator=(const FObjectStoreDispatchGuard&) = delete;

	/**
	 * Motivation: Lets a caller branch on whether callback dispatch may proceed under this guard.
	 * Responsibilities: Report true only when this instance holds the reservation.
	 */
	bool IsAcquired() const noexcept { return ObjectStore != nullptr; }

private:
	/** Motivation: Identifies the store reservation released at scope exit, or null after rejection. */
	FObjectStore* ObjectStore{nullptr};
};

} // namespace MicroWorld::Engine
