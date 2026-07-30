#include "EngineAllocationCounters.h"

#include <cstdint>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace MicroWorld::Tests
{

/** Counts process-wide scalar and array allocation calls after test setup. */
std::uint32_t GlobalAllocationCount{0};

} // namespace MicroWorld::Tests

namespace
{

/** Allocates one block with the requested C++17 over-alignment. */
void* AllocateAligned(const std::size_t InSize, const std::size_t InAlignment) noexcept
{
#if defined(_WIN32)
	return _aligned_malloc(InSize, InAlignment);
#else
	const std::size_t RoundedSize = ((InSize + InAlignment - 1) / InAlignment) * InAlignment;
	return std::aligned_alloc(InAlignment, RoundedSize);
#endif
}

/** Releases one block returned by AllocateAligned on the active host runtime. */
void FreeAligned(void* const InAllocation) noexcept
{
#if defined(_WIN32)
	_aligned_free(InAllocation);
#else
	std::free(InAllocation);
#endif
}

} // namespace

void* operator new(const std::size_t InSize)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = std::malloc(InSize))
	{
		return Allocation;
	}
	std::abort();
}

void* operator new[](const std::size_t InSize)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = std::malloc(InSize))
	{
		return Allocation;
	}
	std::abort();
}

void* operator new(const std::size_t InSize, const std::align_val_t InAlignment)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = AllocateAligned(InSize, static_cast<std::size_t>(InAlignment)))
	{
		return Allocation;
	}
	std::abort();
}

void* operator new[](const std::size_t InSize, const std::align_val_t InAlignment)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = AllocateAligned(InSize, static_cast<std::size_t>(InAlignment)))
	{
		return Allocation;
	}
	std::abort();
}

void operator delete(void* const InAllocation) noexcept
{
	std::free(InAllocation);
}

void operator delete[](void* const InAllocation) noexcept
{
	std::free(InAllocation);
}

void operator delete(void* const InAllocation, const std::size_t) noexcept
{
	std::free(InAllocation);
}

void operator delete[](void* const InAllocation, const std::size_t) noexcept
{
	std::free(InAllocation);
}

void operator delete(void* const InAllocation, const std::align_val_t) noexcept
{
	FreeAligned(InAllocation);
}

void operator delete[](void* const InAllocation, const std::align_val_t) noexcept
{
	FreeAligned(InAllocation);
}

void operator delete(void* const InAllocation, const std::size_t, const std::align_val_t) noexcept
{
	FreeAligned(InAllocation);
}

void operator delete[](void* const InAllocation, const std::size_t, const std::align_val_t) noexcept
{
	FreeAligned(InAllocation);
}
