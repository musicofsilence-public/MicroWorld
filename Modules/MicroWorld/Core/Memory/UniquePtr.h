#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/Memory/MemoryBlock.h>
#include <MicroWorld/Core/Memory/MemoryResource.h>

#include <memory>
#include <type_traits>

namespace MicroWorld::Core
{

template<typename ValueType>
struct TUniquePointerResult;

template<typename ValueType, typename... ConstructorArgumentTypes>
TUniquePointerResult<ValueType> MakeUnique(IMemoryResource&, ConstructorArgumentTypes&&...) noexcept;

/**
 * Motivation: Lets an owner hold one value and return its exact allocation to its originating resource.
 * Responsibilities: Move only as a complete pointer and deleter contract, with raw release and cross-resource
 *   reset excluded, and require the resource outlive this pointer.
 * Example:
 *   auto Result = MakeUnique<int>(Arena, 7);
 *   TUniquePtr<int> Owner = std::move(Result.Pointer);
 */
template<typename ValueType>
class TUniquePtr final
{
	static_assert(!std::is_array<ValueType>::value, "TUniquePtr owns one non-array value.");
	static_assert(std::is_nothrow_destructible<ValueType>::value, "TUniquePtr requires noexcept destruction.");

private:
	/**
	 * Motivation: Retains the resource identity and exact block behind standard unique ownership.
	 * Responsibilities: Destroy one value once and return its unchanged allocation to its resource.
	 * Example:
	 *   FResourceDeleter Deleter{Arena, Block};
	 *   Deleter(Value);
	 */
	struct FResourceDeleter final
	{
		/**
		 * Motivation: Lets an empty unique pointer keep an inert deleter.
		 * Responsibilities: Produce a deleter bound to no resource and no allocation.
		 */
		FResourceDeleter() noexcept = default;

		/**
		 * Motivation: Binds one successful allocation to the resource that produced it.
		 * Responsibilities: Store the resource pointer and exact block for later deallocation.
		 */
		FResourceDeleter(IMemoryResource& InResource, const FMemoryBlock InAllocation) noexcept : Resource(&InResource), Allocation(InAllocation) {}

		/**
		 * Motivation: Lets the standard deleter return the value's allocation at the end of ownership.
		 * Responsibilities: Destroy the value once before returning its unchanged allocation.
		 */
		void operator()(ValueType* const InValue) noexcept
		{
			if (InValue == nullptr)
			{
				return;
			}

			RawStorage::DestroyAt(InValue);
			if (Resource != nullptr)
			{
				static_cast<void>(Resource->Deallocate(Allocation));
			}
		}

		/** Motivation: Identifies the resource that must receive Allocation. */
		IMemoryResource* Resource{nullptr};

		/** Motivation: Preserves the exact block returned for this value. */
		FMemoryBlock Allocation{};
	};

	/** Motivation: Uses the standard-library exclusive-owner state machine with a resource-aware deleter. */
	using FStandardUniquePtr = std::unique_ptr<ValueType, FResourceDeleter>;

public:
	/**
	 * Motivation: Lets an owner declare an empty unique pointer before any resource is chosen.
	 * Responsibilities: Produce an owner holding no value and touching no resource.
	 */
	TUniquePtr() noexcept = default;

	/**
	 * Motivation: Preserves exclusive ownership by rejecting copies.
	 * Responsibilities: Reject copy construction so the value and its allocation stay uniquely owned.
	 */
	TUniquePtr(const TUniquePtr&) = delete;

	/**
	 * Motivation: Preserves exclusive ownership by rejecting copy assignment.
	 * Responsibilities: Reject copy assignment so the value and its allocation stay uniquely owned.
	 */
	TUniquePtr& operator=(const TUniquePtr&) = delete;

	/**
	 * Motivation: Lets an owner transfer the complete value/resource/block contract from another owner.
	 * Responsibilities: Move the value, resource, and block together without splitting them.
	 */
	TUniquePtr(TUniquePtr&& Other) noexcept = default;

	/**
	 * Motivation: Lets an owner replace its value with another complete ownership contract.
	 * Responsibilities: Release any current value, then adopt the source value, resource, and block.
	 */
	TUniquePtr& operator=(TUniquePtr&& Other) noexcept = default;

	/**
	 * Motivation: Ensures the owned value is destroyed and its block returned at end of scope.
	 * Responsibilities: Destroy the owned value and return its exact block when ownership remains.
	 */
	~TUniquePtr() noexcept = default;

	/**
	 * Motivation: Lets a caller observe the owned value without changing its lifetime.
	 * Responsibilities: Return the value pointer, or null when no value is owned.
	 */
	ValueType* Get() const noexcept { return Pointer.get(); }

	/**
	 * Motivation: Lets a caller guard dereference behind one cheap check.
	 * Responsibilities: Report whether this handle currently owns a value.
	 */
	bool IsValid() const noexcept { return Pointer != nullptr; }

	/**
	 * Motivation: Lets an owner release its value deterministically.
	 * Responsibilities: Destroy the owned value and return its exact block to its resource.
	 */
	void Reset() noexcept { Pointer.reset(); }

private:
	/**
	 * Motivation: Lets the factory adopt only a validated value and its exact allocation contract.
	 * Responsibilities: Bind the value pointer and resource-aware deleter without re-validating.
	 */
	TUniquePtr(ValueType* const InValue, IMemoryResource& InResource, const FMemoryBlock InAllocation) noexcept
		: Pointer(InValue, FResourceDeleter(InResource, InAllocation))
	{
	}

	/**
	 * Motivation: Lets the factory create the only non-empty owner without exposing raw adoption.
	 * Responsibilities: Grant the factory access to the private adoption constructor.
	 */
	template<typename FactoryValueType, typename... FactoryConstructorArgumentTypes>
	friend TUniquePointerResult<FactoryValueType> MakeUnique(IMemoryResource&, FactoryConstructorArgumentTypes&&...) noexcept;

	/** Motivation: Holds the value and invokes the resource-aware deleter at most once. */
	FStandardUniquePtr Pointer{};
};

} // namespace MicroWorld::Core
