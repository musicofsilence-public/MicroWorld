#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every bounded delegate operation one result vocabulary that does not borrow
 *   unrelated lifecycle errors.
 * Responsibilities: Distinguish success from capacity, callable-fit, handle, and dispatch conflicts.
 * Example:
 *   EDelegateResult Result = Delegate.Execute();
 *   if (Result != EDelegateResult::Success) { Recover(); }
 */
enum class EDelegateResult : std::uint8_t
{
	/** Motivation: Confirms that the requested delegate operation completed. */
	Success,

	/** Motivation: Reports that no reusable multicast slot remains. */
	CapacityExceeded,

	/** Motivation: Rejects a callable whose object representation exceeds the declared inline capacity. */
	CallableTooLarge,

	/** Motivation: Rejects a callable whose alignment exceeds the delegate's inline storage guarantee. */
	CallableAlignmentUnsupported,

	/** Motivation: Rejects an unbound delegate or a structurally invalid handle. */
	InvalidHandle,

	/** Motivation: Rejects a handle whose binding was removed or whose slot generation has changed. */
	StaleHandle,

	/** Motivation: Prevents mutation or nested dispatch from changing an active broadcast iteration. */
	BroadcastLocked,
};

/**
 * Motivation: Lets a caller carry one multicast binding identity without exposing storage or
 *   extending the callable's lifetime.
 * Responsibilities: Pair a slot index with a generation and never mutate on its own.
 * Example:
 *   FDelegateHandle Handle;
 *   if (Handle.IsValid()) { Delegate.Remove(Handle); }
 */
struct FDelegateHandle final
{
	/** Motivation: Reserves the maximum index as an invalid sentinel independent of delegate capacity. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Motivation: Selects the fixed slot while preserving an explicit invalid sentinel. */
	std::uint16_t Index{InvalidIndex};

	/** Motivation: Distinguishes successive bindings that occupy the same slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning delegate.
	 * Responsibilities: Report true only when the index and generation together look like a live binding.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }

	/**
	 * Motivation: Lets containers compare two handles by complete stable identity.
	 * Responsibilities: Return true only when both index and generation match.
	 */
	friend constexpr bool operator==(const FDelegateHandle Left, const FDelegateHandle Right) noexcept
	{
		return Left.Index == Right.Index && Left.Generation == Right.Generation;
	}

	/**
	 * Motivation: Lets a caller tell two handles apart by stable identity.
	 * Responsibilities: Return true whenever the slot or generation identity differs.
	 */
	friend constexpr bool operator!=(const FDelegateHandle Left, const FDelegateHandle Right) noexcept { return !(Left == Right); }
};

/**
 * Motivation: Lets an owner hold one callable entirely inside fixed inline storage, so binding
 *   never heap-allocates on a constrained target.
 * Responsibilities: Erase the concrete callable behind one supported void signature and keep
 *   construction, invocation, movement, and destruction non-throwing.
 * Example:
 *   TDelegate<void(), 32> Delegate;
 *   Delegate.Bind([]() noexcept {});
 *   Delegate.Execute();
 */
template<typename Signature, std::size_t InlineCallableBytes>
class TDelegate;

/**
 * Motivation: Specializes the inline callable erasure for the supported void signature family.
 * Responsibilities: Bind, invoke, move, and destroy one concrete callable without allocating.
 * Example:
 *   TDelegate<void(int), 16> Delegate;
 *   Delegate.Bind([](int) noexcept {});
 *   Delegate.Execute(7);
 */
template<std::size_t InlineCallableBytes, typename... ArgumentTypes>
class TDelegate<void(ArgumentTypes...), InlineCallableBytes> final
{
	static_assert(InlineCallableBytes > 0, "A delegate must reserve at least one inline callable byte.");

public:
	/**
	 * Motivation: Lets an owner declare a delegate with no callable bound yet.
	 * Responsibilities: Produce an unbound delegate without constructing a callable or allocating storage.
	 */
	TDelegate() noexcept = default;

	/**
	 * Motivation: Ensures no bound callable outlives the delegate that owns its storage.
	 * Responsibilities: Destroy the currently bound callable exactly once.
	 */
	~TDelegate() noexcept { Reset(); }

	/**
	 * Motivation: Prevents copying from duplicating ownership of one inline callable lifetime.
	 * Responsibilities: Reject copy construction so the inline callable stays uniquely owned.
	 */
	TDelegate(const TDelegate&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating ownership of one inline callable lifetime.
	 * Responsibilities: Reject copy assignment so the inline callable stays uniquely owned.
	 */
	TDelegate& operator=(const TDelegate&) = delete;

	/**
	 * Motivation: Lets an owner transfer a bound callable into this delegate in one non-throwing move.
	 * Responsibilities: Move the callable and erasure operations into this delegate and leave the source unbound.
	 */
	TDelegate(TDelegate&& Other) noexcept { MoveFrom(Other); }

	/**
	 * Motivation: Lets an owner replace this delegate's callable with another owner's callable.
	 * Responsibilities: Release the current binding, then transfer the source callable and leave the source unbound.
	 */
	TDelegate& operator=(TDelegate&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		Reset();
		MoveFrom(Other);
		return *this;
	}

	/**
	 * Motivation: Lets a caller replace the current binding when the callable fits the inline layout.
	 * Responsibilities: Reject unsupported size or alignment before either callable is constructed or the
	 *   current binding is changed, and on success install the erased operations.
	 */
	template<typename CallableType>
	EDelegateResult Bind(CallableType&& InCallable) noexcept
	{
		using FStoredCallable = typename std::decay<CallableType>::type;

		static_assert(
			std::is_nothrow_constructible<FStoredCallable, CallableType&&>::value,
			"A delegate callable must be nothrow constructible from the supplied value.");
		static_assert(std::is_nothrow_move_constructible<FStoredCallable>::value, "A delegate callable must be nothrow move constructible.");
		static_assert(std::is_nothrow_destructible<FStoredCallable>::value, "A delegate callable must be nothrow destructible.");
		static_assert(
			std::is_nothrow_invocable_r<void, FStoredCallable&, ArgumentTypes...>::value,
			"A delegate callable must be nothrow invocable with the declared signature.");

		if constexpr (sizeof(FStoredCallable) > InlineCallableBytes)
		{
			return EDelegateResult::CallableTooLarge;
		}
		else if constexpr (alignof(FStoredCallable) > InlineStorageAlignment)
		{
			return EDelegateResult::CallableAlignmentUnsupported;
		}
		else
		{
			Reset();
			RawStorage::ConstructAt<FStoredCallable>(StorageAddress(), std::forward<CallableType>(InCallable));
			InstallErasedOperations<FStoredCallable>();
			return EDelegateResult::Success;
		}
	}

	/**
	 * Motivation: Lets an owner release the current callable without destroying the delegate.
	 * Responsibilities: Destroy the current callable if any and restore the unbound state.
	 */
	void Reset() noexcept
	{
		if (Operations.Destroy == nullptr)
		{
			return;
		}

		Operations.Destroy(StorageAddress());
		ClearFunctions();
	}

	/**
	 * Motivation: Lets a caller guard an Execute behind a single cheap check.
	 * Responsibilities: Report whether invocation currently has a live callable target.
	 */
	bool IsBound() const noexcept { return Operations.Invoke != nullptr; }

	/**
	 * Motivation: Lets a caller fire the bound callable once with forwarded arguments.
	 * Responsibilities: Invoke the bound callable once or report that no target is present.
	 */
	EDelegateResult Execute(ArgumentTypes... Arguments) noexcept
	{
		if (Operations.Invoke == nullptr)
		{
			return EDelegateResult::InvalidHandle;
		}

		Operations.Invoke(StorageAddress(), std::forward<ArgumentTypes>(Arguments)...);
		return EDelegateResult::Success;
	}

private:
	/** Motivation: Gives all supported inline callables a portable fundamental alignment guarantee. */
	static constexpr std::size_t InlineStorageAlignment = alignof(std::max_align_t);

	/** Motivation: Names the erased invocation signature dispatched to the live callable object. */
	using FInvokeFunction = void (*)(void*, ArgumentTypes...) noexcept;

	/** Motivation: Names the erased move signature that transfers one callable between storage blocks. */
	using FMoveFunction = void (*)(void*, void*) noexcept;

	/** Motivation: Names the erased destroy signature that ends one callable without its concrete type. */
	using FDestroyFunction = void (*)(void*) noexcept;

	/**
	 * Motivation: Bundles the invoke, move, and destroy operations for one erased callable.
	 * Responsibilities: Hold three fixed function pointers that keep every binding inline and allocation-free.
	 * Example:
	 *   FErasedCallableOperations Ops;
	 *   Ops.Invoke = &Invoke<FStoredCallable>;
	 */
	struct FErasedCallableOperations
	{
		FInvokeFunction Invoke{nullptr};
		FMoveFunction MoveConstruct{nullptr};
		FDestroyFunction Destroy{nullptr};
	};

	/**
	 * Motivation: Lets the lifetime helpers reach the live callable after placement construction.
	 * Responsibilities: Resolve a laundered pointer to the concrete callable in the given storage.
	 */
	template<typename CallableType>
	static CallableType* CallableAt(void* const InStorage) noexcept
	{
		return RawStorage::LaunderedPointer<CallableType>(InStorage);
	}

	/**
	 * Motivation: Lets the erased invoke entry reach one concrete callable.
	 * Responsibilities: Forward the arguments with their declared single-cast categories.
	 */
	template<typename CallableType>
	static void Invoke(void* const InStorage, ArgumentTypes... Arguments) noexcept
	{
		(*CallableAt<CallableType>(InStorage))(std::forward<ArgumentTypes>(Arguments)...);
	}

	/**
	 * Motivation: Lets move construction transfer one erased callable between storage blocks.
	 * Responsibilities: Move-construct the callable in destination storage and end its source lifetime.
	 */
	template<typename CallableType>
	static void Move(void* const InDestinationStorage, void* const InSourceStorage) noexcept
	{
		CallableType* const SourceCallable = CallableAt<CallableType>(InSourceStorage);
		RawStorage::ConstructAt<CallableType>(InDestinationStorage, std::move(*SourceCallable));
		RawStorage::DestroyAt(SourceCallable);
	}

	/**
	 * Motivation: Lets Reset end one concrete callable held by this delegate.
	 * Responsibilities: Run the callable's destructor once through its laundered pointer.
	 */
	template<typename CallableType>
	static void Destroy(void* const InStorage) noexcept
	{
		RawStorage::DestroyAt(CallableAt<CallableType>(InStorage));
	}

	/**
	 * Motivation: Lets Bind point the erased operations at the concrete callable's invoke/move/destroy.
	 * Responsibilities: Assign the three operation pointers for the newly bound callable type.
	 */
	template<typename StoredCallable>
	void InstallErasedOperations() noexcept
	{
		Operations.Invoke = &Invoke<StoredCallable>;
		Operations.MoveConstruct = &Move<StoredCallable>;
		Operations.Destroy = &Destroy<StoredCallable>;
	}

	/**
	 * Motivation: Gives the lifetime helpers the raw address of the inline storage.
	 * Responsibilities: Expose the erased storage address only to this delegate's operations.
	 */
	void* StorageAddress() noexcept { return static_cast<void*>(&InlineStorage); }

	/**
	 * Motivation: Lets move construction transfer the callable without duplicating ownership.
	 * Responsibilities: Copy the erasure operations, move the callable into this storage, and clear the source.
	 */
	void MoveFrom(TDelegate& InOther) noexcept
	{
		if (!InOther.IsBound())
		{
			return;
		}

		Operations = InOther.Operations;
		Operations.MoveConstruct(StorageAddress(), InOther.StorageAddress());
		InOther.ClearFunctions();
	}

	/**
	 * Motivation: Lets Reset leave a clean unbound state after the callable is gone.
	 * Responsibilities: Reset the erasure operations to their inert defaults.
	 */
	void ClearFunctions() noexcept { Operations = {}; }

	/**
	 * Motivation: Retains one callable inline without beginning any concrete object lifetime by default.
	 * Responsibilities: Hold one callable's worth of aligned bytes without starting a concrete object lifetime.
	 */
	alignas(InlineStorageAlignment) unsigned char InlineStorage[InlineCallableBytes];

	/** Motivation: Selects the concrete invoke/move/destroy operations for the currently bound callable. */
	FErasedCallableOperations Operations;
};

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
