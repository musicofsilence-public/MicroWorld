#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Containers/StaticVector.h>
#include <MicroWorld/Delegates/Delegate.h>
#include <MicroWorld/Memory/FixedArena.h>
#include <MicroWorld/Memory/SharedPtr.h>
#include <MicroWorld/Memory/UniquePtr.h>
#include <MicroWorld/TickFunction.h>
#include <MicroWorld/Version.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 2);
static_assert(MicroWorld::Version.Patch == 0);
static_assert(std::is_nothrow_destructible_v<MicroWorld::TUniquePtr<std::uint32_t>>);

/** Exercises representative Core+Memory public APIs without platform dependencies. */
inline int RunMemoryConsumerProbe() noexcept
{
	MicroWorld::FTickFunction CoreArchiveProbe({true, true, 0});
	CoreArchiveProbe.BeginPlay(0);
	const MicroWorld::FTickDecision TickDecision = CoreArchiveProbe.Advance(0);
	CoreArchiveProbe.EndPlay();

	MicroWorld::TFixedArena<256, alignof(std::max_align_t)> Arena;
	{
		auto UniqueResult = MicroWorld::MakeUnique<std::uint32_t>(Arena, 11U);
		if (UniqueResult.Result != MicroWorld::EMemoryResult::Success || !UniqueResult.Pointer.IsValid() || *UniqueResult.Pointer.Get() != 11U)
		{
			return 1;
		}
	}
	{
		auto SharedResult = MicroWorld::MakeShared<std::uint32_t>(Arena, 13U);
		if (SharedResult.Result != MicroWorld::ESharedPointerResult::Success || !SharedResult.Pointer.IsValid())
		{
			return 2;
		}
		auto WeakResult = SharedResult.Pointer.TryAcquireWeak();
		if (WeakResult.Result != MicroWorld::ESharedPointerResult::Success || WeakResult.Pointer.IsExpired())
		{
			return 3;
		}
	}

	MicroWorld::TStaticVector<std::uint32_t, 2> Values;
	if (Values.Add(17U) != MicroWorld::ERuntimeResult::Success || Values.Add(19U) != MicroWorld::ERuntimeResult::Success)
	{
		return 4;
	}
	const MicroWorld::TSpan<const std::uint32_t> ValuesView(Values.Data(), Values.Size());

	std::uint32_t DelegateTotal = 0;
	MicroWorld::TDelegate<void(std::uint32_t), 32> Binding;
	if (Binding.Bind([&DelegateTotal](const std::uint32_t Value) noexcept { DelegateTotal += Value; }) != MicroWorld::EDelegateResult::Success)
	{
		return 5;
	}
	MicroWorld::TMulticastDelegate<void(std::uint32_t), 1, 32> Multicast;
	MicroWorld::FDelegateHandle Handle;
	if (Multicast.Add(std::move(Binding), Handle) != MicroWorld::EDelegateResult::Success
		|| Multicast.Broadcast(ValuesView[0]) != MicroWorld::EDelegateResult::Success)
	{
		return 6;
	}

	return TickDecision.Result == MicroWorld::ERuntimeResult::Success && TickDecision.bShouldTick && DelegateTotal == 17U && ValuesView[1] == 19U
			&& Arena.UsedBytes() == 0
		? 0
		: 7;
}
