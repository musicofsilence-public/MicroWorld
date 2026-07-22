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
void* AllocateAligned(const std::size_t Size, const std::size_t Alignment) noexcept
{
#if defined(_WIN32)
	return _aligned_malloc(Size, Alignment);
#else
	const std::size_t RoundedSize = ((Size + Alignment - 1) / Alignment) * Alignment;
	return std::aligned_alloc(Alignment, RoundedSize);
#endif
}

/** Releases one block returned by AllocateAligned on the active host runtime. */
void FreeAligned(void* const Allocation) noexcept
{
#if defined(_WIN32)
	_aligned_free(Allocation);
#else
	std::free(Allocation);
#endif
}

} // namespace

void* operator new(const std::size_t Size)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = std::malloc(Size))
	{
		return Allocation;
	}
	std::abort();
}

void* operator new[](const std::size_t Size)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = std::malloc(Size))
	{
		return Allocation;
	}
	std::abort();
}

void* operator new(const std::size_t Size, const std::align_val_t Alignment)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = AllocateAligned(Size, static_cast<std::size_t>(Alignment)))
	{
		return Allocation;
	}
	std::abort();
}

void* operator new[](const std::size_t Size, const std::align_val_t Alignment)
{
	++MicroWorld::Tests::GlobalAllocationCount;
	if (void* const Allocation = AllocateAligned(Size, static_cast<std::size_t>(Alignment)))
	{
		return Allocation;
	}
	std::abort();
}

void operator delete(void* const Allocation) noexcept
{
	std::free(Allocation);
}

void operator delete[](void* const Allocation) noexcept
{
	std::free(Allocation);
}

void operator delete(void* const Allocation, const std::size_t) noexcept
{
	std::free(Allocation);
}

void operator delete[](void* const Allocation, const std::size_t) noexcept
{
	std::free(Allocation);
}

void operator delete(void* const Allocation, const std::align_val_t) noexcept
{
	FreeAligned(Allocation);
}

void operator delete[](void* const Allocation, const std::align_val_t) noexcept
{
	FreeAligned(Allocation);
}

void operator delete(void* const Allocation, const std::size_t, const std::align_val_t) noexcept
{
	FreeAligned(Allocation);
}

void operator delete[](void* const Allocation, const std::size_t, const std::align_val_t) noexcept
{
	FreeAligned(Allocation);
}
