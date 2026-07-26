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
static_assert(MicroWorld::Version.Minor == 3);
static_assert(MicroWorld::Version.Patch == 0);
static_assert(std::is_nothrow_destructible_v<MicroWorld::TUniquePtr<std::uint32_t>>);

namespace MicroWorldConsumer
{

/** Stable exit codes for the Core memory public-API probe; 0 reports full success. */
enum class EMemoryConsumerExitCode : int
{
	Success = 0,
	UniquePointerConstructionFailed = 1,
	SharedPointerConstructionFailed = 2,
	WeakAcquireFailed = 3,
	StaticVectorAddFailed = 4,
	DelegateBindFailed = 5,
	MulticastAddOrBroadcastFailed = 6,
	ProbeOutcomeMismatch = 7,
};

/** Fixed arena byte capacity exercised by the probe. */
inline constexpr std::size_t ProbeArenaBytes = 256;

/** Distinct u32 values the probe threads through each Core container type. */
inline constexpr std::uint32_t UniqueProbeValue = 11U;
inline constexpr std::uint32_t SharedProbeValue = 13U;
inline constexpr std::uint32_t FirstStaticVectorValue = 17U;
inline constexpr std::uint32_t SecondStaticVectorValue = 19U;

/** Bounded element count and inline callable bytes used by the probe's containers and delegates. */
inline constexpr std::size_t StaticVectorCapacity = 2;
inline constexpr std::size_t DelegateInlineBytes = 32;
inline constexpr std::size_t MulticastHandlerCount = 1;

} // namespace MicroWorldConsumer

/** Exercises representative Core memory public APIs without platform dependencies. */
inline int RunMemoryConsumerProbe() noexcept
{
	using namespace MicroWorld;
	using MicroWorldConsumer::EMemoryConsumerExitCode;

	MicroWorld::FTickFunction CoreArchiveProbe({true, true, 0});
	CoreArchiveProbe.BeginPlay(0);
	const MicroWorld::FTickDecision TickDecision = CoreArchiveProbe.Advance(0);
	CoreArchiveProbe.EndPlay();

	MicroWorld::TFixedArena<MicroWorldConsumer::ProbeArenaBytes, alignof(std::max_align_t)> Arena;
	{
		const TUniquePointerResult<std::uint32_t> UniqueResult = MicroWorld::MakeUnique<std::uint32_t>(Arena, MicroWorldConsumer::UniqueProbeValue);
		const bool bUniqueAccepted = UniqueResult.Result == MicroWorld::EMemoryResult::Success && UniqueResult.Pointer.IsValid();
		const bool bUniqueValueIntact = bUniqueAccepted && *UniqueResult.Pointer.Get() == MicroWorldConsumer::UniqueProbeValue;
		if (!bUniqueValueIntact)
		{
			return static_cast<int>(EMemoryConsumerExitCode::UniquePointerConstructionFailed);
		}
	}
	{
		const TSharedPointerResult<std::uint32_t, ESharedPointerMode::SingleThreaded> SharedResult =
			MicroWorld::MakeShared<std::uint32_t>(Arena, MicroWorldConsumer::SharedProbeValue);
		const bool bSharedAccepted = SharedResult.Result == MicroWorld::ESharedPointerResult::Success && SharedResult.Pointer.IsValid();
		if (!bSharedAccepted)
		{
			return static_cast<int>(EMemoryConsumerExitCode::SharedPointerConstructionFailed);
		}
		const TWeakPointerResult<std::uint32_t, ESharedPointerMode::SingleThreaded> WeakResult = SharedResult.Pointer.TryAcquireWeak();
		const bool bWeakLive = WeakResult.Result == MicroWorld::ESharedPointerResult::Success && !WeakResult.Pointer.IsExpired();
		if (!bWeakLive)
		{
			return static_cast<int>(EMemoryConsumerExitCode::WeakAcquireFailed);
		}
	}

	MicroWorld::TStaticVector<std::uint32_t, MicroWorldConsumer::StaticVectorCapacity> Values;
	const bool bBothAddsAccepted = Values.Add(MicroWorldConsumer::FirstStaticVectorValue) == MicroWorld::ERuntimeResult::Success
		&& Values.Add(MicroWorldConsumer::SecondStaticVectorValue) == MicroWorld::ERuntimeResult::Success;
	if (!bBothAddsAccepted)
	{
		return static_cast<int>(EMemoryConsumerExitCode::StaticVectorAddFailed);
	}
	const MicroWorld::TSpan<const std::uint32_t> ValuesView(Values.Data(), Values.Size());

	std::uint32_t DelegateTotal = 0;
	MicroWorld::TDelegate<void(std::uint32_t), MicroWorldConsumer::DelegateInlineBytes> Binding;
	const EDelegateResult BindResult = Binding.Bind([&DelegateTotal](const std::uint32_t Value) noexcept { DelegateTotal += Value; });
	if (BindResult != MicroWorld::EDelegateResult::Success)
	{
		return static_cast<int>(EMemoryConsumerExitCode::DelegateBindFailed);
	}
	MicroWorld::TMulticastDelegate<void(std::uint32_t), MicroWorldConsumer::MulticastHandlerCount, MicroWorldConsumer::DelegateInlineBytes> Multicast;
	MicroWorld::FDelegateHandle Handle;
	const EDelegateResult AddResult = Multicast.Add(std::move(Binding), Handle);
	const EDelegateResult BroadcastResult = Multicast.Broadcast(ValuesView[0]);
	const bool bMulticastDelivered = AddResult == MicroWorld::EDelegateResult::Success && BroadcastResult == MicroWorld::EDelegateResult::Success;
	if (!bMulticastDelivered)
	{
		return static_cast<int>(EMemoryConsumerExitCode::MulticastAddOrBroadcastFailed);
	}

	const bool bTickSucceeded = TickDecision.Result == MicroWorld::ERuntimeResult::Success && TickDecision.bShouldTick;
	const bool bDelegateAccumulated = DelegateTotal == MicroWorldConsumer::FirstStaticVectorValue;
	const bool bStaticVectorSecondValueIntact = ValuesView[1] == MicroWorldConsumer::SecondStaticVectorValue;
	const bool bArenaReclaimed = Arena.UsedBytes() == 0;
	const bool bProbeSucceeded = bTickSucceeded && bDelegateAccumulated && bStaticVectorSecondValueIntact && bArenaReclaimed;
	return bProbeSucceeded ? static_cast<int>(EMemoryConsumerExitCode::Success) : static_cast<int>(EMemoryConsumerExitCode::ProbeOutcomeMismatch);
}
