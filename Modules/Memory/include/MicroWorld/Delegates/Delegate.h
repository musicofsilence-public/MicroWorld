#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

/** Reports every bounded delegate operation without borrowing unrelated lifecycle errors. */
enum class EDelegateResult : std::uint8_t
{
	/** Confirms that the requested delegate operation completed. */
	Success,

	/** Reports that no reusable multicast slot remains. */
	CapacityExceeded,

	/** Rejects a callable whose object representation exceeds the declared inline capacity. */
	CallableTooLarge,

	/** Rejects a callable whose alignment exceeds the delegate's inline storage guarantee. */
	CallableAlignmentUnsupported,

	/** Rejects an unbound delegate or a structurally invalid handle. */
	InvalidHandle,

	/** Rejects a handle whose binding was removed or whose slot generation has changed. */
	StaleHandle,

	/** Prevents mutation or nested dispatch from changing an active broadcast iteration. */
	BroadcastLocked,
};

/** Identifies one multicast binding without exposing storage or extending callable lifetime. */
struct FDelegateHandle final
{
	/** Reserves the maximum index as an invalid sentinel independent of delegate capacity. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Selects the fixed slot while preserving an explicit invalid sentinel. */
	std::uint16_t Index{InvalidIndex};

	/** Distinguishes successive bindings that occupy the same slot. */
	std::uint32_t Generation{0};

	/** Reports whether the value can identify a binding before consulting its owning delegate. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }

	/** Compares the complete stable binding identity. */
	friend constexpr bool operator==(const FDelegateHandle Left, const FDelegateHandle Right) noexcept
	{
		return Left.Index == Right.Index && Left.Generation == Right.Generation;
	}

	/** Distinguishes handles whose slot or generation identity differs. */
	friend constexpr bool operator!=(const FDelegateHandle Left, const FDelegateHandle Right) noexcept { return !(Left == Right); }
};

/**
 * Owns one callable entirely inside fixed inline storage.
 *
 * Only `void` signatures are supported in this release. Callable construction,
 * invocation, movement, and destruction must all be non-throwing.
 */
template<typename Signature, std::size_t InlineCallableBytes>
class TDelegate;

/** Specializes fixed inline callable erasure for the supported `void` signature family. */
template<std::size_t InlineCallableBytes, typename... ArgumentTypes>
class TDelegate<void(ArgumentTypes...), InlineCallableBytes> final
{
	static_assert(InlineCallableBytes > 0, "A delegate must reserve at least one inline callable byte.");

public:
	/** Creates an unbound delegate without constructing a callable or allocating storage. */
	TDelegate() noexcept = default;

	/** Destroys the currently bound callable exactly once. */
	~TDelegate() noexcept { Reset(); }

	/** Prevents copying from duplicating ownership of one inline callable lifetime. */
	TDelegate(const TDelegate&) = delete;

	/** Prevents copy assignment from duplicating ownership of one inline callable lifetime. */
	TDelegate& operator=(const TDelegate&) = delete;

	/** Transfers a bound callable into this delegate and leaves the source unbound. */
	TDelegate(TDelegate&& Other) noexcept { MoveFrom(Other); }

	/** Replaces this callable with the source callable and leaves the source unbound. */
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
	 * Replaces the current binding when the callable fits the declared inline layout.
	 *
	 * Unsupported size or alignment is reported before either callable is
	 * constructed or the current binding is changed.
	 */
	template<typename CallableType>
	EDelegateResult Bind(CallableType&& Callable) noexcept
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
			::new (StorageAddress()) FStoredCallable(std::forward<CallableType>(Callable));
			InvokeFunction = &Invoke<FStoredCallable>;
			MoveFunction = &Move<FStoredCallable>;
			DestroyFunction = &Destroy<FStoredCallable>;
			return EDelegateResult::Success;
		}
	}

	/** Destroys the current callable, if any, and restores the unbound state. */
	void Reset() noexcept
	{
		if (DestroyFunction == nullptr)
		{
			return;
		}

		DestroyFunction(StorageAddress());
		ClearFunctions();
	}

	/** Reports whether invocation currently has a live callable target. */
	bool IsBound() const noexcept { return InvokeFunction != nullptr; }

	/** Invokes the bound callable once or reports that no target is present. */
	EDelegateResult Execute(ArgumentTypes... Arguments) noexcept
	{
		if (InvokeFunction == nullptr)
		{
			return EDelegateResult::InvalidHandle;
		}

		InvokeFunction(StorageAddress(), std::forward<ArgumentTypes>(Arguments)...);
		return EDelegateResult::Success;
	}

private:
	/** Gives all supported inline callables a portable fundamental alignment guarantee. */
	static constexpr std::size_t InlineStorageAlignment = alignof(std::max_align_t);

	/** Dispatches the erased invocation to the live callable object. */
	using FInvokeFunction = void (*)(void*, ArgumentTypes...) noexcept;

	/** Transfers one erased callable lifetime between delegate storage blocks. */
	using FMoveFunction = void (*)(void*, void*) noexcept;

	/** Ends one erased callable lifetime without knowing its concrete type. */
	using FDestroyFunction = void (*)(void*) noexcept;

	/** Resolves the live callable after placement construction established its concrete type. */
	template<typename CallableType>
	static CallableType* CallableAt(void* const Storage) noexcept
	{
		return std::launder(reinterpret_cast<CallableType*>(Storage));
	}

	/** Invokes one concrete callable while preserving the declared single-cast argument categories. */
	template<typename CallableType>
	static void Invoke(void* const Storage, ArgumentTypes... Arguments) noexcept
	{
		(*CallableAt<CallableType>(Storage))(std::forward<ArgumentTypes>(Arguments)...);
	}

	/** Move-constructs one callable in destination storage and ends its source lifetime. */
	template<typename CallableType>
	static void Move(void* const DestinationStorage, void* const SourceStorage) noexcept
	{
		CallableType* const SourceCallable = CallableAt<CallableType>(SourceStorage);
		::new (DestinationStorage) CallableType(std::move(*SourceCallable));
		SourceCallable->~CallableType();
	}

	/** Destroys one concrete callable held by this delegate. */
	template<typename CallableType>
	static void Destroy(void* const Storage) noexcept
	{
		CallableAt<CallableType>(Storage)->~CallableType();
	}

	/** Exposes erased storage only to this delegate's lifetime operations. */
	void* StorageAddress() noexcept { return static_cast<void*>(&InlineStorage); }

	/** Transfers the live callable and erasure operations without duplicating ownership. */
	void MoveFrom(TDelegate& Other) noexcept
	{
		if (!Other.IsBound())
		{
			return;
		}

		InvokeFunction = Other.InvokeFunction;
		MoveFunction = Other.MoveFunction;
		DestroyFunction = Other.DestroyFunction;
		MoveFunction(StorageAddress(), Other.StorageAddress());
		Other.ClearFunctions();
	}

	/** Clears erased operations only after the associated callable lifetime has ended or moved. */
	void ClearFunctions() noexcept
	{
		InvokeFunction = nullptr;
		MoveFunction = nullptr;
		DestroyFunction = nullptr;
	}

	/** Retains one callable inline without beginning any concrete object lifetime by default. */
	typename std::aligned_storage<InlineCallableBytes, InlineStorageAlignment>::type InlineStorage;

	/** Selects the concrete invocation operation for the currently bound callable. */
	FInvokeFunction InvokeFunction{nullptr};

	/** Selects the concrete move operation that transfers the currently bound callable. */
	FMoveFunction MoveFunction{nullptr};

	/** Selects the concrete destruction operation that owns the currently bound callable lifetime. */
	FDestroyFunction DestroyFunction{nullptr};
};

/**
 * Owns a fixed number of insertion-ordered `void` delegate bindings.
 *
 * Active broadcast rejects mutation and nested broadcast so every binding
 * present at dispatch start executes at most once in stable insertion order.
 */
template<typename Signature, std::size_t MaxBindings, std::size_t InlineCallableBytes>
class TMulticastDelegate;

/** Specializes bounded multicast dispatch for the supported `void` signature family. */
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
	/** Creates an empty delegate with reusable generation-one slots and no active broadcast. */
	TMulticastDelegate() noexcept = default;

	/** Prevents copying fixed slots and their uniquely owned inline callables. */
	TMulticastDelegate(const TMulticastDelegate&) = delete;

	/** Prevents copy assignment from duplicating binding identities and callable ownership. */
	TMulticastDelegate& operator=(const TMulticastDelegate&) = delete;

	/** Keeps this multicast object's slot addresses and handle ownership stable. */
	TMulticastDelegate(TMulticastDelegate&&) = delete;

	/** Keeps this multicast object's slot addresses and handle ownership stable. */
	TMulticastDelegate& operator=(TMulticastDelegate&&) = delete;

	/**
	 * Transfers one bound delegate into the next insertion position.
	 *
	 * Failure leaves `Binding` bound and clears `OutHandle`.
	 */
	EDelegateResult Add(TDelegate<void(ArgumentTypes...), InlineCallableBytes>&& Binding, FDelegateHandle& OutHandle) noexcept
	{
		OutHandle = {};
		if (bBroadcastActive)
		{
			return EDelegateResult::BroadcastLocked;
		}
		if (!Binding.IsBound())
		{
			return EDelegateResult::InvalidHandle;
		}

		FBindingSlot* const AvailableSlot = FindAvailableSlot();
		if (AvailableSlot == nullptr)
		{
			return EDelegateResult::CapacityExceeded;
		}

		const std::size_t SlotIndex = static_cast<std::size_t>(AvailableSlot - BindingSlots);
		AvailableSlot->Binding = std::move(Binding);
		AvailableSlot->bOccupied = true;

		const FDelegateHandle AddedHandle{
			static_cast<std::uint16_t>(SlotIndex),
			AvailableSlot->Generation,
		};
		BroadcastOrder[ActiveBindingCount] = AddedHandle;
		++ActiveBindingCount;
		OutHandle = AddedHandle;
		return EDelegateResult::Success;
	}

	/** Removes exactly the binding identified by a current generation-checked handle. */
	EDelegateResult Remove(const FDelegateHandle Handle) noexcept
	{
		if (bBroadcastActive)
		{
			return EDelegateResult::BroadcastLocked;
		}
		if (!Handle.IsValid() || Handle.Index >= MaxBindings)
		{
			return EDelegateResult::InvalidHandle;
		}

		FBindingSlot& Slot = BindingSlots[Handle.Index];
		if (!Slot.bOccupied || Slot.Generation != Handle.Generation)
		{
			return EDelegateResult::StaleHandle;
		}

		const std::size_t OrderIndex = FindOrderIndex(Handle);
		if (OrderIndex == ActiveBindingCount)
		{
			return EDelegateResult::StaleHandle;
		}

		Slot.Binding.Reset();
		Slot.bOccupied = false;
		AdvanceGenerationOrRetire(Slot);
		RemoveOrderAt(OrderIndex);
		--ActiveBindingCount;
		return EDelegateResult::Success;
	}

	/**
	 * Invokes the bindings present at broadcast start once in insertion order.
	 *
	 * Value arguments are copied independently for each binding; references
	 * continue to refer to the caller's object.
	 */
	EDelegateResult Broadcast(ArgumentTypes... Arguments) noexcept
	{
		if (bBroadcastActive)
		{
			return EDelegateResult::BroadcastLocked;
		}

		bBroadcastActive = true;
		const std::size_t InitialBindingCount = ActiveBindingCount;
		for (std::size_t OrderIndex = 0; OrderIndex < InitialBindingCount; ++OrderIndex)
		{
			const FDelegateHandle Handle = BroadcastOrder[OrderIndex];
			FBindingSlot& Slot = BindingSlots[Handle.Index];
			const EDelegateResult ExecuteResult = Slot.Binding.Execute(Arguments...);
			if (ExecuteResult != EDelegateResult::Success)
			{
				bBroadcastActive = false;
				return ExecuteResult;
			}
		}

		bBroadcastActive = false;
		return EDelegateResult::Success;
	}

	/** Reports the exact number of bindings that the next successful broadcast will visit. */
	std::size_t BindingCount() const noexcept { return ActiveBindingCount; }

	/** Reports the compile-time upper bound on live bindings and broadcast work. */
	static constexpr std::size_t Capacity() noexcept { return MaxBindings; }

private:
	/** Owns one reusable inline binding and the identity state guarding slot reuse. */
	struct FBindingSlot final
	{
		/** Owns the callable only while this slot is occupied. */
		TDelegate<void(ArgumentTypes...), InlineCallableBytes> Binding;

		/** Changes after removal so an old handle cannot identify a later binding. */
		std::uint32_t Generation{1};

		/** Distinguishes a live binding from reusable unconstructed slot state. */
		bool bOccupied{false};

		/** Permanently removes a slot whose generation can no longer advance safely. */
		bool bRetired{false};
	};

	/** Finds the lowest reusable slot while insertion order remains separately recorded. */
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

	/** Finds one live handle in the insertion-order table without trusting slot state alone. */
	std::size_t FindOrderIndex(const FDelegateHandle Handle) const noexcept
	{
		for (std::size_t OrderIndex = 0; OrderIndex < ActiveBindingCount; ++OrderIndex)
		{
			if (BroadcastOrder[OrderIndex] == Handle)
			{
				return OrderIndex;
			}
		}
		return ActiveBindingCount;
	}

	/** Advances a reusable slot identity or retires it before generation wrap can cause ABA. */
	static void AdvanceGenerationOrRetire(FBindingSlot& Slot) noexcept
	{
		if (Slot.Generation == std::numeric_limits<std::uint32_t>::max())
		{
			Slot.bRetired = true;
			return;
		}
		++Slot.Generation;
	}

	/** Compacts insertion order after removal without changing any remaining slot identity. */
	void RemoveOrderAt(const std::size_t RemovedOrderIndex) noexcept
	{
		for (std::size_t OrderIndex = RemovedOrderIndex; OrderIndex + 1U < ActiveBindingCount; ++OrderIndex)
		{
			BroadcastOrder[OrderIndex] = BroadcastOrder[OrderIndex + 1U];
		}
		BroadcastOrder[ActiveBindingCount - 1U] = {};
	}

	/** Owns all bounded callable storage independently of insertion order. */
	FBindingSlot BindingSlots[MaxBindings == 0 ? 1 : MaxBindings];

	/** Preserves deterministic insertion order while slots are removed and reused. */
	FDelegateHandle BroadcastOrder[MaxBindings == 0 ? 1 : MaxBindings];

	/** Bounds order-table traversal and makes current registration count observable. */
	std::size_t ActiveBindingCount{0};

	/** Rejects mutation and reentrant dispatch while broadcast iteration is active. */
	bool bBroadcastActive{false};
};

} // namespace MicroWorld
