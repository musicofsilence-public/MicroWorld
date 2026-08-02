#include "TestSupport.h"
#include "MemoryTestHelpers.h"

#include <array>
#include <cstddef>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Inspect a fresh positive-capacity vector and attempt to add to a zero-capacity vector.
 * Responsibilities: The empty vector exposes safe boundary state; the zero-capacity vector atomically rejects its first
 *   element and preserves zero size.
 */
MW_TEST_CASE(StaticVectorHandlesEmptyAndZeroCapacityBoundaries)
{
	// Arrange
	TStaticVector<int, 3> EmptyVector;
	TStaticVector<int, 0> ZeroCapacityVector;
	const bool bEmptyVectorIsEmpty = EmptyVector.IsEmpty();
	const bool bEmptyVectorIsFull = EmptyVector.IsFull();
	const bool bEmptyVectorHasSpace = !bEmptyVectorIsFull;
	int* const EmptyData = EmptyVector.Data();
	int* const EmptyBegin = EmptyVector.begin();
	int* const EmptyEnd = EmptyVector.end();
	const std::size_t EmptyCapacity = EmptyVector.Capacity();

	// Act
	const ERuntimeResult AddResult = ZeroCapacityVector.Emplace(7);
	const bool bZeroVectorIsEmpty = ZeroCapacityVector.IsEmpty();
	const bool bZeroVectorIsFull = ZeroCapacityVector.IsFull();
	const std::size_t ZeroSize = ZeroCapacityVector.Size();
	const std::size_t ZeroCapacity = ZeroCapacityVector.Capacity();
	int* const ZeroData = ZeroCapacityVector.Data();
	int* const NullIntPointer = nullptr;

	// Assert
	MW_EXPECT_TRUE(Test, bEmptyVectorIsEmpty, "Fresh positive-capacity vector should be empty");
	MW_EXPECT_TRUE(Test, bEmptyVectorHasSpace, "Fresh positive-capacity vector should not be full");
	MW_EXPECT_EQ(Test, NullIntPointer, EmptyData, "Empty vector should expose null data");
	MW_EXPECT_EQ(Test, NullIntPointer, EmptyBegin, "Empty vector iteration should begin at null");
	MW_EXPECT_EQ(Test, NullIntPointer, EmptyEnd, "Empty vector iteration should end at null");
	MW_EXPECT_EQ(Test, std::size_t{3}, EmptyCapacity, "Vector should expose its exact compile-time capacity");
	MW_EXPECT_EQ(Test, ERuntimeResult::CapacityExceeded, AddResult, "Zero-capacity vector should reject its first element");
	MW_EXPECT_TRUE(Test, bZeroVectorIsEmpty, "Rejected zero-capacity add should preserve empty state");
	MW_EXPECT_TRUE(Test, bZeroVectorIsFull, "Zero-capacity vector should report its capacity is full");
	MW_EXPECT_EQ(Test, std::size_t{0}, ZeroSize, "Rejected zero-capacity add should preserve size zero");
	MW_EXPECT_EQ(Test, std::size_t{0}, ZeroCapacity, "Zero-capacity vector should report capacity zero");
	MW_EXPECT_EQ(Test, NullIntPointer, ZeroData, "Zero-capacity vector should expose null data");
}

/**
 * Motivation: Emplace three tracked elements to capacity, attempt a capacity-plus-one element, iterate, then
 *   clear.
 * Responsibilities: The rejected element never constructs; iteration preserves insertion order; clear destroys elements
 *   in reverse order.
 */
MW_TEST_CASE(StaticVectorPreservesIterationAndRejectsCapacityPlusOneBeforeConstruction)
{
	// Arrange
	FVectorLifetimeState Lifetime;
	TStaticVector<FVectorTrackedValue, 3> Vector;

	// Act
	const ERuntimeResult FirstResult = Vector.Emplace(Lifetime, 1);
	const ERuntimeResult SecondResult = Vector.Emplace(Lifetime, 2);
	const ERuntimeResult ThirdResult = Vector.Emplace(Lifetime, 3);
	const ERuntimeResult ExcessResult = Vector.Emplace(Lifetime, 4);

	std::array<int, 3> IterationOrder{};
	std::size_t IterationCount = 0;
	for (const FVectorTrackedValue& Value : Vector)
	{
		IterationOrder[IterationCount] = Value.GetIdentity();
		++IterationCount;
	}
	const std::size_t SizeAtCapacity = Vector.Size();
	const bool bFullAtCapacity = Vector.IsFull();
	const std::size_t ConstructionCountAtCapacity = Lifetime.ConstructionCount;
	const int FirstIterationIdentity = IterationOrder[0];
	const int SecondIterationIdentity = IterationOrder[1];
	const int ThirdIterationIdentity = IterationOrder[2];

	// Assert
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstResult, "First vector element should construct");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, SecondResult, "Second vector element should construct");
	MW_EXPECT_EQ(Test, ERuntimeResult::Success, ThirdResult, "Element at exact capacity should construct");
	MW_EXPECT_EQ(Test, ERuntimeResult::CapacityExceeded, ExcessResult, "Capacity-plus-one element should be rejected");
	MW_EXPECT_EQ(Test, std::size_t{3}, SizeAtCapacity, "Capacity rejection should preserve exact vector size");
	MW_EXPECT_TRUE(Test, bFullAtCapacity, "Vector should report full at exact capacity");
	MW_EXPECT_EQ(Test, std::size_t{3}, ConstructionCountAtCapacity, "Rejected excess element should never construct");
	MW_EXPECT_EQ(Test, std::size_t{3}, IterationCount, "Iteration should visit every live element once");
	MW_EXPECT_EQ(Test, 1, FirstIterationIdentity, "Iteration should visit the first inserted element first");
	MW_EXPECT_EQ(Test, 2, SecondIterationIdentity, "Iteration should preserve the second insertion position");
	MW_EXPECT_EQ(Test, 3, ThirdIterationIdentity, "Iteration should visit the capacity element last");

	// Act
	Vector.Clear();
	const std::size_t SizeAfterClear = Vector.Size();
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const int FirstDestroyedIdentity = Lifetime.DestructionOrder[0];
	const int SecondDestroyedIdentity = Lifetime.DestructionOrder[1];
	const int ThirdDestroyedIdentity = Lifetime.DestructionOrder[2];

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{0}, SizeAfterClear, "Clear should restore vector size zero");
	MW_EXPECT_EQ(Test, std::size_t{3}, DestructionCount, "Clear should destroy every live element exactly once");
	MW_EXPECT_EQ(Test, 3, FirstDestroyedIdentity, "Clear should destroy the latest element first");
	MW_EXPECT_EQ(Test, 2, SecondDestroyedIdentity, "Clear should continue in reverse insertion order");
	MW_EXPECT_EQ(Test, 1, ThirdDestroyedIdentity, "Clear should destroy the earliest element last");
}

/**
 * Motivation: Emplace two tracked elements into a scoped vector and let it exit scope.
 * Responsibilities: Scope exit destroys exactly the two live elements in reverse insertion order.
 */
MW_TEST_CASE(StaticVectorScopeExitDestroysOnlyLiveElements)
{
	// Arrange
	FVectorLifetimeState Lifetime;

	// Act
	{
		TStaticVector<FVectorTrackedValue, 3> Vector;
		const ERuntimeResult FirstResult = Vector.Emplace(Lifetime, 5);
		const ERuntimeResult SecondResult = Vector.Emplace(Lifetime, 6);

		// Assert
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, FirstResult, "First scoped vector element should construct");
		MW_EXPECT_EQ(Test, ERuntimeResult::Success, SecondResult, "Second scoped vector element should construct");
	}

	// Act
	const std::size_t ConstructionCount = Lifetime.ConstructionCount;
	const std::size_t DestructionCount = Lifetime.DestructionCount;
	const int FirstDestroyedIdentity = Lifetime.DestructionOrder[0];
	const int SecondDestroyedIdentity = Lifetime.DestructionOrder[1];

	// Assert
	MW_EXPECT_EQ(Test, std::size_t{2}, ConstructionCount, "Scoped vector should construct only added elements");
	MW_EXPECT_EQ(Test, std::size_t{2}, DestructionCount, "Scoped vector should destroy every live element exactly once");
	MW_EXPECT_EQ(Test, 6, FirstDestroyedIdentity, "Scope exit should destroy latest live element first");
	MW_EXPECT_EQ(Test, 5, SecondDestroyedIdentity, "Scope exit should destroy earliest live element last");
}

/**
 * Motivation: Construct default and explicit null-zero-count spans, plus a non-empty null span.
 * Responsibilities: Null spans are valid only at the empty boundary; the non-empty null span is invalid but retains its
 *   caller-provided count.
 */
MW_TEST_CASE(SpanDistinguishesValidEmptyNullFromInvalidNonEmptyNull)
{
	// Arrange
	TSpan<int> DefaultSpan;
	TSpan<int> ExplicitEmptySpan(nullptr, 0);

	// Act
	TSpan<int> InvalidSpan(nullptr, 1);

	const bool bDefaultValid = DefaultSpan.IsValid();
	const bool bDefaultEmpty = DefaultSpan.IsEmpty();
	int* const DefaultData = DefaultSpan.Data();
	const bool bExplicitEmptyValid = ExplicitEmptySpan.IsValid();
	const bool bExplicitEmpty = ExplicitEmptySpan.IsEmpty();
	int* const ExplicitEmptyBegin = ExplicitEmptySpan.begin();
	int* const ExplicitEmptyEnd = ExplicitEmptySpan.end();
	const bool bInvalidSpanValid = InvalidSpan.IsValid();
	const bool bInvalidSpanEmpty = InvalidSpan.IsEmpty();
	const bool bInvalidSpanRejected = !bInvalidSpanValid;
	const bool bInvalidSpanNonEmpty = !bInvalidSpanEmpty;
	const std::size_t InvalidSpanSize = InvalidSpan.Size();
	int* const NullIntPointer = nullptr;

	// Assert
	MW_EXPECT_TRUE(Test, bDefaultValid, "Default null span should be a valid empty view");
	MW_EXPECT_TRUE(Test, bDefaultEmpty, "Default span should report empty");
	MW_EXPECT_EQ(Test, NullIntPointer, DefaultData, "Default span should expose null data");
	MW_EXPECT_TRUE(Test, bExplicitEmptyValid, "Explicit null zero-count span should be valid");
	MW_EXPECT_TRUE(Test, bExplicitEmpty, "Explicit null zero-count span should report empty");
	MW_EXPECT_EQ(Test, NullIntPointer, ExplicitEmptyBegin, "Empty null span should begin at null");
	MW_EXPECT_EQ(Test, NullIntPointer, ExplicitEmptyEnd, "Empty null span should end at null");
	MW_EXPECT_TRUE(Test, bInvalidSpanRejected, "Non-empty null span should report invalid");
	MW_EXPECT_TRUE(Test, bInvalidSpanNonEmpty, "Non-empty null span should preserve its non-empty count");
	MW_EXPECT_EQ(Test, std::size_t{1}, InvalidSpanSize, "Invalid span should retain the caller-provided count");
}

/**
 * Motivation: Build mutable and const array spans including a const view converted from the mutable span, mutate
 *   through it, and iterate each.
 * Responsibilities: The spans preserve array extent, the mutable write updates the caller array, and iteration visits
 *   each element in order including the.
 */
MW_TEST_CASE(SpanProvidesMutableAndConstArrayViewsInOrder)
{
	// Arrange
	int MutableElements[]{2, 4, 6};
	const int ConstElements[]{1, 3, 5};

	// Act
	TSpan<int> MutableSpan(MutableElements);
	TSpan<const int> ConstFromMutable(MutableSpan);
	TSpan<const int> ConstSpan(ConstElements);

	MutableSpan[1] = 8;
	int MutableSum = 0;
	for (int& Element : MutableSpan)
	{
		MutableSum += Element;
	}
	int ConstFromMutableSum = 0;
	for (const int Element : ConstFromMutable)
	{
		ConstFromMutableSum += Element;
	}
	int ConstSum = 0;
	for (const int Element : ConstSpan)
	{
		ConstSum += Element;
	}

	const bool bMutableValid = MutableSpan.IsValid();
	const bool bConstFromMutableValid = ConstFromMutable.IsValid();
	const bool bConstValid = ConstSpan.IsValid();
	const std::size_t MutableSize = MutableSpan.Size();
	const std::size_t ConstSize = ConstSpan.Size();
	const int MutatedArrayValue = MutableElements[1];
	const bool bConstConversionSharesData = ConstFromMutable.Data() == MutableSpan.Data();

	// Assert
	MW_EXPECT_TRUE(Test, bMutableValid, "Mutable array span should be valid");
	MW_EXPECT_TRUE(Test, bConstFromMutableValid, "Const view converted from mutable span should be valid");
	MW_EXPECT_TRUE(Test, bConstValid, "Const array span should be valid");
	MW_EXPECT_EQ(Test, std::size_t{3}, MutableSize, "Mutable array span should preserve array extent");
	MW_EXPECT_EQ(Test, std::size_t{3}, ConstSize, "Const array span should preserve array extent");
	MW_EXPECT_EQ(Test, 8, MutatedArrayValue, "Mutable span write should update caller-owned array");
	MW_EXPECT_TRUE(Test, bConstConversionSharesData, "Const conversion should observe the same caller-owned storage");
	MW_EXPECT_EQ(Test, 16, MutableSum, "Mutable span iteration should visit each element in order");
	MW_EXPECT_EQ(Test, 16, ConstFromMutableSum, "Converted const span should observe the mutable update");
	MW_EXPECT_EQ(Test, 9, ConstSum, "Const span iteration should visit each const element in order");
}

} // namespace
