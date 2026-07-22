#pragma once

#include <MicroWorld/Time.h>

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace MicroWorld
{

/** Owns a contiguous, insertion-ordered sequence whose capacity is fixed at compile time. */
template<typename ElementType, std::size_t MaxElements>
class TStaticVector final
{
public:
	/** Creates an empty vector without constructing elements or allocating storage. */
	TStaticVector() noexcept = default;

	/** Destroys only the elements that were successfully added. */
	~TStaticVector() noexcept { Clear(); }

	/** Prevents an implicit copy from bypassing explicit element construction. */
	TStaticVector(const TStaticVector&) = delete;

	/** Prevents assignment from obscuring element lifetime and failure behavior. */
	TStaticVector& operator=(const TStaticVector&) = delete;

	/** Keeps the fixed storage address stable for pointers and iterators. */
	TStaticVector(TStaticVector&&) = delete;

	/** Keeps the fixed storage address stable for pointers and iterators. */
	TStaticVector& operator=(TStaticVector&&) = delete;

	/** Copy-constructs one element at the end or reports that fixed capacity is exhausted. */
	ERuntimeResult Add(const ElementType& Element) noexcept
	{
		static_assert(std::is_nothrow_copy_constructible<ElementType>::value, "TStaticVector elements must be nothrow copy constructible");
		return Emplace(Element);
	}

	/** Move-constructs one element at the end or reports that fixed capacity is exhausted. */
	ERuntimeResult Add(ElementType&& Element) noexcept
	{
		static_assert(std::is_nothrow_move_constructible<ElementType>::value, "TStaticVector elements must be nothrow move constructible");
		return Emplace(std::move(Element));
	}

	/** Constructs one element in place or reports capacity before changing the sequence. */
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
		::new (ElementStorage) ElementType(std::forward<ArgumentTypes>(Arguments)...);
		++ElementCount;
		return ERuntimeResult::Success;
	}

	/** Destroys all live elements in reverse insertion order and restores empty state. */
	void Clear() noexcept
	{
		static_assert(std::is_nothrow_destructible<ElementType>::value, "TStaticVector elements must be nothrow destructible");
		while (ElementCount > 0)
		{
			--ElementCount;
			ElementAt(ElementCount)->~ElementType();
		}
	}

	/** Returns the first live element address, or null when the vector is empty. */
	ElementType* Data() noexcept { return ElementCount == 0 ? nullptr : ElementAt(0); }

	/** Returns the first live element address, or null when the vector is empty. */
	const ElementType* Data() const noexcept { return ElementCount == 0 ? nullptr : ElementAt(0); }

	/** Reports how many elements currently have active lifetimes. */
	std::size_t Size() const noexcept { return ElementCount; }

	/** Reports the compile-time bound that additions can never exceed. */
	static constexpr std::size_t Capacity() noexcept { return MaxElements; }

	/** Distinguishes an empty sequence without inspecting its storage. */
	bool IsEmpty() const noexcept { return ElementCount == 0; }

	/** Lets callers avoid a known capacity failure before attempting an addition. */
	bool IsFull() const noexcept { return ElementCount == MaxElements; }

	/** Accesses a live element; the caller must provide an index below `Size()`. */
	ElementType& operator[](const std::size_t Index) noexcept { return *ElementAt(Index); }

	/** Accesses a live element; the caller must provide an index below `Size()`. */
	const ElementType& operator[](const std::size_t Index) const noexcept { return *ElementAt(Index); }

	/** Begins deterministic mutable iteration in insertion order. */
	ElementType* begin() noexcept { return Data(); }

	/** Ends deterministic mutable iteration after the last live element. */
	ElementType* end() noexcept
	{
		ElementType* const FirstElement = Data();
		return FirstElement == nullptr ? nullptr : FirstElement + ElementCount;
	}

	/** Begins deterministic read-only iteration in insertion order. */
	const ElementType* begin() const noexcept { return Data(); }

	/** Ends deterministic read-only iteration after the last live element. */
	const ElementType* end() const noexcept
	{
		const ElementType* const FirstElement = Data();
		return FirstElement == nullptr ? nullptr : FirstElement + ElementCount;
	}

	/** Begins explicit read-only iteration in insertion order. */
	const ElementType* cbegin() const noexcept { return begin(); }

	/** Ends explicit read-only iteration after the last live element. */
	const ElementType* cend() const noexcept { return end(); }

private:
	/** Gives every slot enough size and alignment without starting an element lifetime. */
	using FStorageSlot = typename std::aligned_storage<sizeof(ElementType), alignof(ElementType)>::type;

	static_assert(sizeof(FStorageSlot) == sizeof(ElementType), "TStaticVector requires storage slots with no inter-element padding");

	/** Resolves a live element after placement construction has begun its lifetime. */
	ElementType* ElementAt(const std::size_t Index) noexcept { return std::launder(reinterpret_cast<ElementType*>(&Storage[Index])); }

	/** Resolves a live element after placement construction has begun its lifetime. */
	const ElementType* ElementAt(const std::size_t Index) const noexcept
	{
		return std::launder(reinterpret_cast<const ElementType*>(&Storage[Index]));
	}

	/** Reserves a compile-time-bounded set of slots without constructing or allocating elements. */
	FStorageSlot Storage[MaxElements == 0 ? 1 : MaxElements];

	/** Bounds every access and identifies exactly which element lifetimes are active. */
	std::size_t ElementCount{0};
};

} // namespace MicroWorld
