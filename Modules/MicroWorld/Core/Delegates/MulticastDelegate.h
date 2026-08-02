#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Delegates/DelegateHandle.h>
#include <MicroWorld/Core/Delegates/DelegateResult.h>
#include <MicroWorld/Core/Delegates/Delegate.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace MicroWorld::Core
{

/**
 * Motivation: Lets an owner keep a fixed number of insertion-ordered void delegate bindings and
 *   broadcast to all of them without heap allocation.
 * Responsibilities: Hold the bindings in stable insertion order and let active broadcast reject
 *   mutation and nested broadcast so every binding present at dispatch start runs at most once.
 * Example:
 *   TMulticastDelegate<void(), 4, 16> Bus;
 *   Bus.Add(std::move(A), Handle);
 *   Bus.Broadcast();
 */
template<typename Signature, std::size_t MaxBindings, std::size_t InlineCallableBytes>
class TMulticastDelegate;

/**
 * Motivation: Specializes bounded multicast dispatch for the supported void signature family.
 * Responsibilities: Add, remove, and broadcast insertion-ordered bindings, rejecting mutation and
 *   reentrant dispatch while a broadcast is active.
 * Example:
 *   TMulticastDelegate<void(int), 8, 16> Bus;
 *   Bus.Broadcast(7);
 */
template<std::size_t MaxBindings, std::size_t InlineCallableBytes, typename... ArgumentTypes>
class TMulticastDelegate<void(ArgumentTypes...), MaxBindings, InlineCallableBytes> final
{
	static_assert(MaxBindings < FDelegateHandle::InvalidIndex, "A multicast delegate capacity must fit below the reserved handle index.");
	static_assert(
		((!std::is_rvalue_reference<ArgumentTypes>::value) && ...), "A multicast delegate cannot safely repeat an rvalue-reference argument.");
	static_assert(
		((std::is_lvalue_reference<ArgumentTypes>::value
		  || std::is_nothrow_copy_constructible<typename std::remove_reference<ArgumentTypes>::type>::value)
		 && ...),
		"Every multicast value argument must be nothrow copy constructible for noexcept delivery to each binding.");

public:
	/**
	 * Motivation: Lets an owner declare an empty multicast delegate with no bindings.
	 * Responsibilities: Produce reusable generation-one slots and no active broadcast.
	 */
	TMulticastDelegate() noexcept = default;

	/**
	 * Motivation: Prevents copying fixed slots and their uniquely owned inline callables.
	 * Responsibilities: Reject copy construction so each inline callable stays uniquely owned.
	 */
	TMulticastDelegate(const TMulticastDelegate&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating binding identities and callable ownership.
	 * Responsibilities: Reject copy assignment so binding identities stay unique.
	 */
	TMulticastDelegate& operator=(const TMulticastDelegate&) = delete;

	/**
	 * Motivation: Keeps this multicast object's slot addresses and handle ownership stable.
	 * Responsibilities: Reject move construction so registered slot addresses never relocate.
	 */
	TMulticastDelegate(TMulticastDelegate&&) = delete;

	/**
	 * Motivation: Keeps this multicast object's slot addresses and handle ownership stable.
	 * Responsibilities: Reject move assignment so registered slot addresses never relocate.
	 */
	TMulticastDelegate& operator=(TMulticastDelegate&&) = delete;

	/**
	 * Motivation: Lets a caller register one bound delegate at the next insertion position.
	 * Responsibilities: Reject mutation during broadcast and an unbound binding, then move InBinding
	 *   into a reusable slot and publish a generation-checked handle; on failure leave InBinding bound
	 *   and clear OutHandle.
	 */
	EDelegateResult Add(TDelegate<void(ArgumentTypes...), InlineCallableBytes>&& InBinding, FDelegateHandle& OutHandle) noexcept
	{
		OutHandle = {};
		if (bBroadcastActive)
		{
			return EDelegateResult::BroadcastLocked;
		}
		if (!InBinding.IsBound())
		{
			return EDelegateResult::InvalidHandle;
		}
		FBindingSlot* const AvailableSlot = FindAvailableSlot();
		if (AvailableSlot == nullptr)
		{
			return EDelegateResult::CapacityExceeded;
		}
		const std::size_t SlotIndex = static_cast<std::size_t>(AvailableSlot - BindingSlots);
		const FDelegateHandle AddedHandle = OccupySlot(SlotIndex, std::move(InBinding));
		RecordBroadcastOrder(AddedHandle);
		OutHandle = AddedHandle;
		return EDelegateResult::Success;
	}

	/**
	 * Motivation: Lets a caller unregister one binding by its current generation-checked handle.
	 * Responsibilities: Reject mutation during broadcast and remove exactly the identified binding.
	 */
	EDelegateResult Remove(const FDelegateHandle InHandle) noexcept
	{
		if (bBroadcastActive)
		{
			return EDelegateResult::BroadcastLocked;
		}
		std::size_t OrderIndex = 0;
		const EDelegateResult ValidationResult = ValidateLiveHandle(InHandle, OrderIndex);
		if (ValidationResult != EDelegateResult::Success)
		{
			return ValidationResult;
		}
		RetireSlotAndCompactOrder(InHandle.Index, OrderIndex);
		return EDelegateResult::Success;
	}

	/**
	 * Motivation: Lets a caller fire every binding present at broadcast start once, in order.
	 * Responsibilities: Reject reentrant broadcast, then invoke the snapshotted bindings in insertion
	 *   order, copying value arguments per binding while references refer to the caller's object.
	 */
	EDelegateResult Broadcast(ArgumentTypes... Arguments) noexcept
	{
		if (bBroadcastActive)
		{
			return EDelegateResult::BroadcastLocked;
		}
		const FScopedBroadcastGuard BroadcastGuard{bBroadcastActive};
		const std::size_t InitialBindingCount = ActiveBindingCount;
		for (std::size_t OrderIndex = 0; OrderIndex < InitialBindingCount; ++OrderIndex)
		{
			const FDelegateHandle Handle = BroadcastOrder[OrderIndex];
			FBindingSlot& Slot = BindingSlots[Handle.Index];
			const EDelegateResult ExecuteResult = Slot.Binding.Execute(Arguments...);
			if (ExecuteResult != EDelegateResult::Success)
			{
				return ExecuteResult;
			}
		}
		return EDelegateResult::Success;
	}

	/**
	 * Motivation: Lets a caller report how many bindings the next successful broadcast visits.
	 * Responsibilities: Return the exact count of live bindings.
	 */
	std::size_t BindingCount() const noexcept { return ActiveBindingCount; }

	/**
	 * Motivation: Lets a caller test capacity against the fixed limit without magic numbers.
	 * Responsibilities: Report the compile-time upper bound on live bindings and broadcast work.
	 */
	static constexpr std::size_t Capacity() noexcept { return MaxBindings; }

private:
	/**
	 * Motivation: Holds one reusable inline binding and the identity state that guards slot reuse.
	 * Responsibilities: Own the callable while occupied and carry the generation and retirement flags
	 *   that make a reused slot reject stale handles.
	 * Example:
	 *   FBindingSlot Slot;
	 *   Slot.Generation = 2;
	 */
	struct FBindingSlot final
	{
		/** Motivation: Owns the callable only while this slot is occupied. */
		TDelegate<void(ArgumentTypes...), InlineCallableBytes> Binding;

		/** Motivation: Changes after removal so an old handle cannot identify a later binding. */
		std::uint32_t Generation{1};

		/** Motivation: Distinguishes a live binding from reusable unconstructed slot state. */
		bool bOccupied{false};

		/** Motivation: Permanently removes a slot whose generation can no longer advance safely. */
		bool bRetired{false};
	};

	/**
	 * Motivation: Gives Broadcast a flag it can set for one dispatch and reliably clear on exit.
	 * Responsibilities: Set the flag on construction and clear it on every exit path, including exceptions.
	 * Example:
	 *   bool Active = false;
	 *   FScopedBroadcastGuard Guard{Active};
	 */
	struct FScopedBroadcastGuard final
	{
		explicit FScopedBroadcastGuard(bool& InFlag) noexcept : ActiveFlag(InFlag) { ActiveFlag = true; }
		~FScopedBroadcastGuard() noexcept { ActiveFlag = false; }

		/**
		 * Motivation: Prevents a second guard instance from racing one flag.
		 * Responsibilities: Reject copy construction so exactly one scope owns the flag reset.
		 */
		FScopedBroadcastGuard(const FScopedBroadcastGuard&) = delete;

		/**
		 * Motivation: Prevents reassigning the flag one guard resets on destruction.
		 * Responsibilities: Reject copy assignment so each guard keeps one bound flag.
		 */
		FScopedBroadcastGuard& operator=(const FScopedBroadcastGuard&) = delete;

		/** Motivation: Names the broadcast-active flag this guard sets and clears. */
		bool& ActiveFlag;
	};

	/**
	 * Motivation: Lets Add locate the next slot without disturbing the separately recorded order.
	 * Responsibilities: Return the lowest unoccupied, unretired slot, or null when none remains.
	 */
	FBindingSlot* FindAvailableSlot() noexcept
	{
		for (std::size_t SlotIndex = 0; SlotIndex < MaxBindings; ++SlotIndex)
		{
			FBindingSlot& Slot = BindingSlots[SlotIndex];
			if (!Slot.bOccupied && !Slot.bRetired)
			{
				return &Slot;
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Lets Add claim one reusable slot for a new binding.
	 * Responsibilities: Move the bound delegate into the slot, mark it occupied, and return its handle.
	 */
	FDelegateHandle OccupySlot(const std::size_t InSlotIndex, TDelegate<void(ArgumentTypes...), InlineCallableBytes>&& InBinding) noexcept
	{
		FBindingSlot& Slot = BindingSlots[InSlotIndex];
		Slot.Binding = std::move(InBinding);
		Slot.bOccupied = true;
		return FDelegateHandle{
			static_cast<std::uint16_t>(InSlotIndex),
			Slot.Generation,
		};
	}

	/**
	 * Motivation: Lets Add record where each binding sits for the next broadcast.
	 * Responsibilities: Append the handle to the insertion-order table and advance the binding count.
	 */
	void RecordBroadcastOrder(const FDelegateHandle InHandle) noexcept
	{
		BroadcastOrder[ActiveBindingCount] = InHandle;
		++ActiveBindingCount;
	}

	/**
	 * Motivation: Lets validation locate one handle in insertion order without trusting slot state alone.
	 * Responsibilities: Return the handle's order index, or the active count when it is not present.
	 */
	std::size_t FindOrderIndex(const FDelegateHandle InHandle) const noexcept
	{
		for (std::size_t OrderIndex = 0; OrderIndex < ActiveBindingCount; ++OrderIndex)
		{
			if (BroadcastOrder[OrderIndex] == InHandle)
			{
				return OrderIndex;
			}
		}
		return ActiveBindingCount;
	}

	/**
	 * Motivation: Lets Remove trust one handle as identifying a current live binding.
	 * Responsibilities: Reject invalid or stale handles and report the binding's insertion-order index.
	 */
	EDelegateResult ValidateLiveHandle(const FDelegateHandle InHandle, std::size_t& OutOrderIndex) const noexcept
	{
		OutOrderIndex = ActiveBindingCount;
		if (!InHandle.IsValid() || InHandle.Index >= MaxBindings)
		{
			return EDelegateResult::InvalidHandle;
		}
		const FBindingSlot& Slot = BindingSlots[InHandle.Index];
		if (!Slot.bOccupied || Slot.Generation != InHandle.Generation)
		{
			return EDelegateResult::StaleHandle;
		}
		const std::size_t OrderIndex = FindOrderIndex(InHandle);
		if (OrderIndex == ActiveBindingCount)
		{
			return EDelegateResult::StaleHandle;
		}
		OutOrderIndex = OrderIndex;
		return EDelegateResult::Success;
	}

	/**
	 * Motivation: Lets Remove tear down one binding without leaving the order table inconsistent.
	 * Responsibilities: Reset the binding, advance its slot identity, compact insertion order, and drop the count.
	 */
	void RetireSlotAndCompactOrder(const std::size_t InSlotIndex, const std::size_t InOrderIndex) noexcept
	{
		FBindingSlot& Slot = BindingSlots[InSlotIndex];
		Slot.Binding.Reset();
		Slot.bOccupied = false;
		AdvanceGenerationOrRetire(Slot);
		RemoveOrderAt(InOrderIndex);
		--ActiveBindingCount;
	}

	/**
	 * Motivation: Keeps a reused slot from matching an old handle as generations approach wrap.
	 * Responsibilities: Advance the generation, or permanently retire the slot before it can wrap.
	 */
	static void AdvanceGenerationOrRetire(FBindingSlot& InSlot) noexcept
	{
		if (InSlot.Generation == std::numeric_limits<std::uint32_t>::max())
		{
			InSlot.bRetired = true;
			return;
		}
		++InSlot.Generation;
	}

	/**
	 * Motivation: Lets Remove close the gap left by a removed binding in insertion order.
	 * Responsibilities: Shift later entries down without changing any remaining slot identity.
	 */
	void RemoveOrderAt(const std::size_t InRemovedOrderIndex) noexcept
	{
		for (std::size_t OrderIndex = InRemovedOrderIndex; OrderIndex + 1U < ActiveBindingCount; ++OrderIndex)
		{
			BroadcastOrder[OrderIndex] = BroadcastOrder[OrderIndex + 1U];
		}
		BroadcastOrder[ActiveBindingCount - 1U] = {};
	}

	/** Motivation: Owns all bounded callable storage independently of insertion order. */
	// C++ forbids zero-length arrays; one dummy slot keeps a zero-binding
	// (MaxBindings == 0) instantiation well-formed.
	FBindingSlot BindingSlots[MaxBindings == 0 ? 1 : MaxBindings];

	/** Motivation: Preserves deterministic insertion order while slots are removed and reused. */
	FDelegateHandle BroadcastOrder[MaxBindings == 0 ? 1 : MaxBindings];

	/** Motivation: Bounds order-table traversal and makes current registration count observable. */
	std::size_t ActiveBindingCount{0};

	/** Motivation: Rejects mutation and reentrant dispatch while broadcast iteration is active. */
	bool bBroadcastActive{false};
};

} // namespace MicroWorld::Core
