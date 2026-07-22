#pragma once

#include <MicroWorld/Memory/MemoryResource.h>

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

template<typename T>
class TUniquePtr;

template<typename T>
struct TUniquePointerResult;

/**
 * Constructs one exclusively owned value in caller-selected storage.
 *
 * @tparam T Complete value type whose construction and destruction cannot throw.
 * @tparam TArguments Constructor argument types forwarded only after allocation.
 * @param Resource Resource that must outlive the returned pointer.
 * @param Arguments Arguments forwarded to T's constructor.
 * @return Typed allocation outcome and exclusive owner on success.
 */
template<typename T, typename... TArguments>
TUniquePointerResult<T> MakeUnique(IMemoryResource& Resource, TArguments&&... Arguments) noexcept;

/**
 * Owns one value and returns its exact allocation to its originating resource.
 *
 * The resource must outlive this pointer. Ownership can only move as a complete
 * pointer/deleter contract; raw release and cross-resource reset are excluded.
 */
template<typename T>
class TUniquePtr final
{
	static_assert(!std::is_array<T>::value, "TUniquePtr owns one non-array value.");
	static_assert(std::is_nothrow_destructible<T>::value, "TUniquePtr requires noexcept destruction.");

private:
	/** Retains the resource identity and exact block behind standard unique ownership. */
	struct FResourceDeleter final
	{
		/** Creates an inert deleter for an empty pointer. */
		FResourceDeleter() noexcept = default;

		/** Binds one successful allocation to the resource that produced it. */
		FResourceDeleter(IMemoryResource& InResource, const FMemoryBlock InAllocation) noexcept : Resource(&InResource), Allocation(InAllocation) {}

		/** Destroys the value once before returning its unchanged allocation. */
		void operator()(T* const Value) noexcept
		{
			if (Value == nullptr)
			{
				return;
			}

			Value->~T();
			if (Resource != nullptr)
			{
				static_cast<void>(Resource->Deallocate(Allocation));
			}
		}

		/** Identifies the resource that must receive Allocation. */
		IMemoryResource* Resource{nullptr};

		/** Preserves the exact block returned for this value. */
		FMemoryBlock Allocation{};
	};

	/** Uses the standard-library exclusive-owner state machine with a resource-aware deleter. */
	using FStandardUniquePtr = std::unique_ptr<T, FResourceDeleter>;

public:
	/** Creates an empty owner without selecting or touching a resource. */
	TUniquePtr() noexcept = default;

	/** Preserves exclusive ownership by rejecting copies. */
	TUniquePtr(const TUniquePtr&) = delete;

	/** Preserves exclusive ownership by rejecting copy assignment. */
	TUniquePtr& operator=(const TUniquePtr&) = delete;

	/** Transfers the complete value/resource/block contract from another owner. */
	TUniquePtr(TUniquePtr&& Other) noexcept = default;

	/** Releases any current value, then transfers another complete ownership contract. */
	TUniquePtr& operator=(TUniquePtr&& Other) noexcept = default;

	/** Destroys the owned value and returns its exact block when ownership remains. */
	~TUniquePtr() noexcept = default;

	/** Observes the owned value without changing its lifetime. */
	T* Get() const noexcept { return Pointer.get(); }

	/** Reports whether this handle currently owns a value. */
	bool IsValid() const noexcept { return Pointer != nullptr; }

	/** Destroys the owned value and returns its exact block to its resource. */
	void Reset() noexcept { Pointer.reset(); }

private:
	/** Adopts only a factory-validated value and its exact allocation contract. */
	TUniquePtr(T* const Value, IMemoryResource& Resource, const FMemoryBlock Allocation) noexcept
		: Pointer(Value, FResourceDeleter(Resource, Allocation))
	{
	}

	/**
	 * Lets the factory create the only non-empty owner without exposing raw adoption.
	 */
	template<typename TObject, typename... TObjectArguments>
	friend TUniquePointerResult<TObject> MakeUnique(IMemoryResource&, TObjectArguments&&...) noexcept;

	/** Holds the value and invokes the resource-aware deleter at most once. */
	FStandardUniquePtr Pointer{};
};

/** Couples an explicit allocation result with the exclusive owner it created. */
template<typename T>
struct TUniquePointerResult
{
	/** Distinguishes successful construction from the resource's exact failure. */
	EMemoryResult Result{EMemoryResult::OutOfMemory};

	/** Owns the constructed value only when Result is Success. */
	TUniquePtr<T> Pointer{};
};

template<typename T, typename... TArguments>
TUniquePointerResult<T> MakeUnique(IMemoryResource& Resource, TArguments&&... Arguments) noexcept
{
	static_assert(!std::is_array<T>::value, "MakeUnique constructs one non-array value.");
	static_assert(std::is_nothrow_constructible<T, TArguments...>::value, "MakeUnique requires noexcept construction.");
	static_assert(std::is_nothrow_destructible<T>::value, "MakeUnique requires noexcept destruction.");

	FMemoryBlock Allocation{};
	const EMemoryResult AllocationResult = Resource.TryAllocate(sizeof(T), alignof(T), Allocation);
	if (AllocationResult != EMemoryResult::Success)
	{
		TUniquePointerResult<T> FailedResult{};
		FailedResult.Result = AllocationResult;
		return FailedResult;
	}

	T* const Value = ::new (Allocation.Address) T(std::forward<TArguments>(Arguments)...);

	TUniquePointerResult<T> SuccessfulResult{};
	SuccessfulResult.Result = EMemoryResult::Success;
	SuccessfulResult.Pointer = TUniquePtr<T>(Value, Resource, Allocation);
	return SuccessfulResult;
}

} // namespace MicroWorld
