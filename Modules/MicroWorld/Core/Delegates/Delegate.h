#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Delegates/DelegateResult.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace MicroWorld::Core
{

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

} // namespace MicroWorld::Core
