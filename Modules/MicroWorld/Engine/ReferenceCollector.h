#pragma once

#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectPtr.h>

namespace MicroWorld::Engine
{

class FGarbageCollector;
class FObjectStore;

/**
 * Motivation: Presents descriptor-visible handles to the active non-recursive mark traversal so traced references reach
 *   the collector through one narrow type.
 * Responsibilities: Mark and enqueue same-store referenced handles during the active mark phase without exposing a raw
 *   public bypass.
 * Example:
 *   Collector.VisitReferences(RefCollector);
 *   RefCollector.AddReferencedObject(Child);
 */
class FReferenceCollector final
{
public:
	/**
	 * Motivation: Marks one typed traced reference while preserving its generation identity.
	 * Responsibilities: Enqueue the reference's handle only when it belongs to the expected store.
	 */
	template<typename T>
	void AddReferencedObject(const TObjectPtr<T> InObject) noexcept
	{
		if (ExpectedStore != nullptr && InObject.BelongsTo(*ExpectedStore))
		{
			AddReferencedHandle(InObject.Handle());
		}
	}

private:
	friend class FGarbageCollector;

	/**
	 * Motivation: Marks one validated same-store identity without exposing a raw public bypass.
	 * Responsibilities: Forward the handle to the active collector's discovery path.
	 */
	void AddReferencedHandle(FObjectHandle InHandle) noexcept;

	/**
	 * Motivation: Restricts discovery to one active visitor and its owning object store.
	 * Responsibilities: Bind the collector and expected store for the visitor's lifetime.
	 */
	FReferenceCollector(FGarbageCollector& InGarbageCollector, FObjectStore& InStore) noexcept
		: Collector(&InGarbageCollector), ExpectedStore(&InStore)
	{
	}

	/** Motivation: Identifies the collector that owns mark state and worklist capacity. */
	FGarbageCollector* Collector{nullptr};

	/** Motivation: Prevents same-valued handles from another object store entering this graph. */
	FObjectStore* ExpectedStore{nullptr};
};

} // namespace MicroWorld::Engine
