#pragma once

#include <cstddef>
#include <type_traits>

namespace MicroWorld::Core
{

/**
 * Motivation: Lets a callee observe a caller-owned contiguous sequence without extending its
 *   lifetime, allocating, or borrowing the element array's ownership.
 * Responsibilities: Carry a pointer and a count that together satisfy the view invariant, and
 *   never mutate or release the observed storage.
 * Example:
 *   int Values[3] = {1, 2, 3};
 *   TSpan<int> View(Values, 3);
 *   for (int& Value : View) { Value += 1; }
 */
template<typename ElementType>
class TSpan final
{
public:
	/**
	 * Motivation: Gives a span the one valid empty shape to hold when no storage is available.
	 * Responsibilities: Produce a null-pointer, zero-count view that satisfies IsValid.
	 */
	constexpr TSpan() noexcept = default;

	/**
	 * Motivation: Lets a caller attach a span to InCount live caller-owned elements beginning at InData.
	 * Responsibilities: Hold the supplied pointer and count without copying; InData may be null only
	 *   when InCount is zero, and the caller keeps every element alive and pointer-stable.
	 */
	constexpr TSpan(ElementType* const InData, const std::size_t InCount) noexcept : DataPointer(InData), ElementCount(InCount) {}

	/**
	 * Motivation: Lets a caller wrap a caller-owned C array without spelling its length.
	 * Responsibilities: Bind every element of the array without changing its mutability or ownership.
	 */
	template<std::size_t Count>
	constexpr TSpan(ElementType (&Elements)[Count]) noexcept : DataPointer(Elements), ElementCount(Count)
	{
	}

	/**
	 * Motivation: Lets a compatible span, including a mutable view to const, cross the type boundary.
	 * Responsibilities: Copy the pointer and count without changing the observed ownership.
	 */
	template<typename OtherElementType, typename std::enable_if<std::is_convertible<OtherElementType (*)[], ElementType (*)[]>::value, int>::type = 0>
	constexpr TSpan(const TSpan<OtherElementType>& InOther) noexcept : DataPointer(InOther.Data()), ElementCount(InOther.Size())
	{
	}

	/**
	 * Motivation: Lets a caller obtain the start address to hand to byte-level APIs.
	 * Responsibilities: Return the caller-owned start address, which may be null only for a valid empty view.
	 */
	constexpr ElementType* Data() const noexcept { return DataPointer; }

	/**
	 * Motivation: Lets a caller bound a loop or size a copy without recomputing the count.
	 * Responsibilities: Report the bounded number of elements described by this view.
	 */
	constexpr std::size_t Size() const noexcept { return ElementCount; }

	/**
	 * Motivation: Lets a caller skip work without dereferencing caller-owned storage.
	 * Responsibilities: Report true when the view bounds zero elements.
	 */
	constexpr bool IsEmpty() const noexcept { return ElementCount == 0; }

	/**
	 * Motivation: Lets a caller prove null and count satisfy the view invariant before access.
	 * Responsibilities: Confirm the stored pointer is non-null unless the count is zero.
	 */
	constexpr bool IsValid() const noexcept { return DataPointer != nullptr || ElementCount == 0; }

	/**
	 * Motivation: Lets a caller index one viewed element by position.
	 * Responsibilities: Return the element at InIndex, which the caller must keep below Size of a valid view.
	 */
	constexpr ElementType& operator[](const std::size_t InIndex) const noexcept { return DataPointer[InIndex]; }

	/**
	 * Motivation: Lets a range-for traverse a valid non-empty view in order.
	 * Responsibilities: Return the start address; a non-empty view must be valid and its storage alive.
	 */
	constexpr ElementType* begin() const noexcept { return DataPointer; }

	/**
	 * Motivation: Lets a range-for terminate after the last viewed element.
	 * Responsibilities: Return the past-the-end address of the bounded sequence.
	 */
	constexpr ElementType* end() const noexcept { return ElementCount == 0 ? DataPointer : DataPointer + ElementCount; }

private:
	/** Motivation: Observes the first caller-owned element and never releases or reallocates it. */
	ElementType* DataPointer{nullptr};

	/** Motivation: Bounds indexing and iteration independently of any sentinel value. */
	std::size_t ElementCount{0};
};

} // namespace MicroWorld::Core
