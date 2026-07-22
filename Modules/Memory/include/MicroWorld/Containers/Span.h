#pragma once

#include <cstddef>
#include <type_traits>

namespace MicroWorld
{

/** Observes a caller-owned contiguous sequence without extending its lifetime or allocating. */
template<typename ElementType>
class TSpan final
{
public:
	/** Creates the valid empty null view used when no storage is available. */
	constexpr TSpan() noexcept = default;

	/**
	 * Observes `Count` elements beginning at `Data`.
	 * `Data` may be null only when `Count` is zero, and the caller keeps all elements alive and pointer-stable.
	 */
	constexpr TSpan(ElementType* const Data, const std::size_t Count) noexcept : DataPointer(Data), ElementCount(Count) {}

	/** Observes every element in a caller-owned C array without changing its mutability. */
	template<std::size_t Count>
	constexpr TSpan(ElementType (&Elements)[Count]) noexcept : DataPointer(Elements), ElementCount(Count)
	{
	}

	/** Converts a compatible view, including mutable-to-const, without changing ownership. */
	template<typename OtherElementType, typename std::enable_if<std::is_convertible<OtherElementType (*)[], ElementType (*)[]>::value, int>::type = 0>
	constexpr TSpan(const TSpan<OtherElementType>& Other) noexcept : DataPointer(Other.Data()), ElementCount(Other.Size())
	{
	}

	/** Returns the caller-owned start address, which may be null only for an empty valid view. */
	constexpr ElementType* Data() const noexcept { return DataPointer; }

	/** Reports the bounded number of elements described by this view. */
	constexpr std::size_t Size() const noexcept { return ElementCount; }

	/** Distinguishes an empty view without dereferencing caller-owned storage. */
	constexpr bool IsEmpty() const noexcept { return ElementCount == 0; }

	/** Reports whether null and count satisfy the view invariant before access or iteration. */
	constexpr bool IsValid() const noexcept { return DataPointer != nullptr || ElementCount == 0; }

	/** Accesses a viewed element; the caller must first ensure the view is valid and the index is below `Size()`. */
	constexpr ElementType& operator[](const std::size_t Index) const noexcept { return DataPointer[Index]; }

	/** Begins iteration; a non-empty view must be valid and its storage must remain alive. */
	constexpr ElementType* begin() const noexcept { return DataPointer; }

	/** Ends iteration; a non-empty view must be valid and its storage must remain alive. */
	constexpr ElementType* end() const noexcept { return ElementCount == 0 ? DataPointer : DataPointer + ElementCount; }

private:
	/** Observes the first caller-owned element and never releases or reallocates it. */
	ElementType* DataPointer{nullptr};

	/** Bounds indexing and iteration independently of any sentinel value. */
	std::size_t ElementCount{0};
};

} // namespace MicroWorld
