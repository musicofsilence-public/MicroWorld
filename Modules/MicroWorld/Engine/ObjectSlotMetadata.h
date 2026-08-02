#pragma once

#include <MicroWorld/Engine/ObjectHandle.h>
#include <MicroWorld/Engine/ObjectSlotState.h>

namespace MicroWorld::Engine
{

struct FClassDescriptor;
class UObject;

/**
 * Motivation: Holds lifecycle metadata in storage supplied and owned by the application so the store keeps no
 *   bookkeeping of its own.
 * Responsibilities: Carry the slot's generation, descriptor, object pointer, state, and collector mark for one slot.
 * Example:
 *   FObjectSlotMetadata Slot;
 *   Slot.State = EObjectSlotState::Live;
 */
struct FObjectSlotMetadata
{
	/** Motivation: Names this slot's current identity without ever wrapping it; it advances the moment an object dies, so a stale handle and a weak
	 * reference both stop matching immediately rather than waiting for the slot to be reused. */
	ObjectGeneration Generation{0};

	/** Motivation: Selects tracing, ancestry, layout, and exact destruction for the active object. */
	const FClassDescriptor* Descriptor{nullptr};

	/** Motivation: Points at the active UObject base within the non-moving slot. */
	UObject* Object{nullptr};

	/** Motivation: Prevents unpublished, pending, vacant, and retired storage from resolving. */
	EObjectSlotState State{EObjectSlotState::Vacant};

	/** Motivation: Stores one collector mark without allocating a side table. */
	bool bMarked{false};
};

} // namespace MicroWorld::Engine
