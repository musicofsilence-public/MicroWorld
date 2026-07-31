#pragma once

#include <MicroWorld/Core/Containers/RawSlot.h>
#include <MicroWorld/Core/RuntimeResult.h>

#include <cstddef>
#include <type_traits>
#include <utility>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives a fixed-capacity owner a contiguous, insertion-ordered sequence whose
 *   capacity is set at compile time, so it never allocates on the heap.
 * Responsibilities: Construct and destroy only the elements actually added, in stable insertion
 *   order, and report capacity exhaustion instead of growing.
 * Example:
 *   TStaticVector<int, 4> Values;
 *   Values.Add(7);
 *   for (int& Value : Values) { Value += 1; }
 */
template<typename ElementType, std::size_t MaxElements>
class TStaticVector final
{
public:
	/**
	 * Motivation: Lets an owner declare an empty vector without paying for element construction.
	 * Responsibilities: Produce zero live elements over the reserved fixed storage.
	 */
	TStaticVector() noexcept = default;

	/**
	 * Motivation: Ensures no constructed element outlives the vector that owns its storage.
	 * Responsibilities: Destroy every live element exactly once in reverse insertion order.
	 */
	~TStaticVector() noexcept { Clear(); }

	/**
	 * Motivation: Prevents an implicit copy from bypassing explicit per-element construction.
	 * Responsibilities: Reject copy construction so element lifetimes stay deliberate.
	 */
	TStaticVector(const TStaticVector&) = delete;

	/**
	 * Motivation: Prevents assignment from obscuring element lifetime and failure behavior.
	 * Responsibilities: Reject copy assignment so per-element construction stays explicit.
	 */
	TStaticVector& operator=(const TStaticVector&) = delete;

	/**
	 * Motivation: Keeps the fixed storage address stable for pointers and iterators.
	 * Responsibilities: Reject move construction so slot addresses never relocate.
	 */
	TStaticVector(TStaticVector&&) = delete;

	/**
	 * Motivation: Keeps the fixed storage address stable for pointers and iterators.
	 * Responsibilities: Reject move assignment so slot addresses never relocate.
	 */
	TStaticVector& operator=(TStaticVector&&) = delete;

	/**
	 * Motivation: Lets a caller append one element by copy when room remains.
	 * Responsibilities: Copy-construct one element at the end or report capacity exhaustion before mutating state.
	 */
	ERuntimeResult Add(const ElementType& InElement) noexcept
	{
		static_assert(std::is_nothrow_copy_constructible<ElementType>::value, "TStaticVector elements must be nothrow copy constructible");
		return Emplace(InElement);
	}

	/**
	 * Motivation: Lets a caller append one element by move when room remains.
	 * Responsibilities: Move-construct one element at the end or report capacity exhaustion before mutating state.
	 */
	ERuntimeResult Add(ElementType&& InElement) noexcept
	{
		static_assert(std::is_nothrow_move_constructible<ElementType>::value, "TStaticVector elements must be nothrow move constructible");
		return Emplace(std::move(InElement));
	}

	/**
	 * Motivation: Lets a caller construct one element in place at the end with forwarded arguments.
	 * Responsibilities: Construct before the count advances and report capacity exhaustion without partial mutation.
	 */
	template<typename... ArgumentTypes>
	ERuntimeResult Emplace(ArgumentTypes&&... Arguments) noexcept
	{
		static_assert(
			std::is_nothrow_constructible<ElementType, ArgumentTypes...>::value,
			"TStaticVector elements must be nothrow constructible from these arguments");
		if (IsFull())
		{
			return ERuntimeResult::CapacityExceeded;
		}

		void* const ElementStorage = static_cast<void*>(&Storage[ElementCount]);
		RawStorage::ConstructAt<ElementType>(ElementStorage, std::forward<ArgumentTypes>(Arguments)...);
		++ElementCount;
		return ERuntimeResult::Success;
	}

	/**
	 * Motivation: Lets an owner return a full vector to its empty state deterministically.
	 * Responsibilities: Destroy every live element in reverse insertion order and reset the count to zero.
	 */
	void Clear() noexcept
	{
		static_assert(std::is_nothrow_destructible<ElementType>::value, "TStaticVector elements must be nothrow destructible");
		while (ElementCount > 0)
		{
			--ElementCount;
			RawStorage::DestroyAt(ElementAt(ElementCount));
		}
	}

	/**
	 * Motivation: Exposes mutable storage to a bulk byte-level consumer.
	 * Responsibilities: Return the first live element address, or null when the vector is empty.
	 */
	ElementType* Data() noexcept { return ElementCount == 0 ? nullptr : ElementAt(0); }

	/**
	 * Motivation: Exposes read-only storage to a bulk byte-level consumer.
	 * Responsibilities: Return the first live element address, or null when the vector is empty.
	 */
	const ElementType* Data() const noexcept { return ElementCount == 0 ? nullptr : ElementAt(0); }

	/**
	 * Motivation: Lets a caller report how many live elements the sequence holds.
	 * Responsibilities: Return the count of elements with active lifetimes.
	 */
	std::size_t Size() const noexcept { return ElementCount; }

	/**
	 * Motivation: Lets a caller test a request against the fixed limit without magic numbers.
	 * Responsibilities: Report the compile-time bound that additions can never exceed.
	 */
	static constexpr std::size_t Capacity() noexcept { return MaxElements; }

	/**
	 * Motivation: Lets a caller skip work without inspecting the storage.
	 * Responsibilities: Report true when no element lifetime is active.
	 */
	bool IsEmpty() const noexcept { return ElementCount == 0; }

	/**
	 * Motivation: Lets a caller avoid a known capacity failure before attempting an addition.
	 * Responsibilities: Report true when every slot holds an active element.
	 */
	bool IsFull() const noexcept { return ElementCount == MaxElements; }

	/**
	 * Motivation: Lets a caller reach one mutable element by position.
	 * Responsibilities: Return the element at InIndex, which the caller must keep below Size.
	 */
	ElementType& operator[](const std::size_t InIndex) noexcept { return *ElementAt(InIndex); }

	/**
	 * Motivation: Lets a caller reach one read-only element by position.
	 * Responsibilities: Return the element at InIndex, which the caller must keep below Size.
	 */
	const ElementType& operator[](const std::size_t InIndex) const noexcept { return *ElementAt(InIndex); }

	/**
	 * Motivation: Lets a range-for traverse live elements in insertion order.
	 * Responsibilities: Return the start address for mutable iteration.
	 */
	ElementType* begin() noexcept { return Data(); }

	/**
	 * Motivation: Lets a range-for terminate after the last live element.
	 * Responsibilities: Return the mutable past-the-end address, or null when empty.
	 */
	ElementType* end() noexcept
	{
		ElementType* const FirstElement = Data();
		return FirstElement == nullptr ? nullptr : FirstElement + ElementCount;
	}

	/**
	 * Motivation: Lets a const range-for traverse live elements in insertion order.
	 * Responsibilities: Return the start address for read-only iteration.
	 */
	const ElementType* begin() const noexcept { return Data(); }

	/**
	 * Motivation: Lets a const range-for terminate after the last live element.
	 * Responsibilities: Return the read-only past-the-end address, or null when empty.
	 */
	const ElementType* end() const noexcept
	{
		const ElementType* const FirstElement = Data();
		return FirstElement == nullptr ? nullptr : FirstElement + ElementCount;
	}

	/**
	 * Motivation: Lets an explicit read-only loop start in insertion order.
	 * Responsibilities: Return the read-only start address for the live elements.
	 */
	const ElementType* cbegin() const noexcept { return begin(); }

	/**
	 * Motivation: Lets an explicit read-only loop terminate after the last live element.
	 * Responsibilities: Return the read-only past-the-end address, or null when empty.
	 */
	const ElementType* cend() const noexcept { return end(); }

private:
	/**
	 * Motivation: Gives every slot enough size and alignment without starting an element lifetime.
	 * Responsibilities: Hold exactly one element's worth of raw bytes and match the element alignment.
	 * Example:
	 *   FStorageSlot Slot;
	 *   static_assert(alignof(FStorageSlot) >= alignof(ElementType));
	 */
	struct alignas(ElementType) FStorageSlot
	{
		/**
		 * Motivation: Raw bytes for exactly one element; no ValueType lifetime begins here.
		 * Responsibilities: Reserve one element's worth of storage without starting a ValueType lifetime.
		 */
		unsigned char Bytes[sizeof(ElementType)];
	};

	static_assert(sizeof(FStorageSlot) == sizeof(ElementType), "TStaticVector requires storage slots with no inter-element padding");

	/**
	 * Motivation: Lets the vector reach the live element after placement construction begins its lifetime.
	 * Responsibilities: Resolve a laundered pointer to the element in the requested slot.
	 */
	ElementType* ElementAt(const std::size_t InIndex) noexcept { return RawStorage::LaunderedPointer<ElementType>(&Storage[InIndex]); }

	/**
	 * Motivation: Lets a const vector reach the live element after placement construction begins its lifetime.
	 * Responsibilities: Resolve a laundered const pointer to the element in the requested slot.
	 */
	const ElementType* ElementAt(const std::size_t InIndex) const noexcept { return RawStorage::LaunderedPointer<ElementType>(&Storage[InIndex]); }

	/** Motivation: Reserves a compile-time-bounded set of slots without constructing or allocating elements. */
	// C++ forbids zero-length arrays; one dummy slot keeps a zero-capacity
	// (MaxElements == 0) instantiation well-formed.
	FStorageSlot Storage[MaxElements == 0 ? 1 : MaxElements];

	/** Motivation: Bounds every access and identifies exactly which element lifetimes are active. */
	std::size_t ElementCount{0};
};

} // namespace MicroWorld::Core
