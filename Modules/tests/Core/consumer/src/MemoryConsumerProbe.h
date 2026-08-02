#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/Containers/StaticVector.h>
#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/Memory/FixedArena.h>
#include <MicroWorld/Core/Memory/TSharedPointerDefinitions.h>
#include <MicroWorld/Core/Memory/UniquePtr.h>
#include <MicroWorld/Core/TickFunction.h>
#include <MicroWorld/Core/Version.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

static_assert(__cplusplus >= 201703L);
static_assert(MicroWorld::Version.Major == 0);
static_assert(MicroWorld::Version.Minor == 4);
static_assert(MicroWorld::Version.Patch == 0);
static_assert(std::is_nothrow_destructible_v<MicroWorld::Core::TUniquePtr<std::uint32_t>>);

namespace MicroWorldConsumer
{

/**
 * Motivation: Stable exit codes for the Core memory public-API probe; 0 reports full success.
 * Responsibilities: Name each distinct memory-API failure so the probe reports the exact broken step.
 * Example:
 *   EMemoryConsumerExitCode Code = EMemoryConsumerExitCode::Success;
 */
enum class EMemoryConsumerExitCode : int
{
	Success = 0,						 ///< Motivation: Reports the probe observed every memory API succeeding.
	UniquePointerConstructionFailed = 1, ///< Motivation: Names a unique-pointer construction that did not return a live value.
	SharedPointerConstructionFailed = 2, ///< Motivation: Names a shared-pointer construction that did not return a live value.
	WeakAcquireFailed = 3,				 ///< Motivation: Names a weak-pointer acquire that did not observe a live referent.
	StaticVectorAddFailed = 4,			 ///< Motivation: Names a static-vector add that did not accept both values.
	DelegateBindFailed = 5,				 ///< Motivation: Names a delegate bind that did not return success.
	MulticastAddOrBroadcastFailed = 6,	 ///< Motivation: Names a multicast add or broadcast that did not deliver.
	ProbeOutcomeMismatch = 7,			 ///< Motivation: Names a final outcome check that did not match the expected state.
};

/** Motivation: Fixed arena byte capacity exercised by the probe. */
inline constexpr std::size_t ProbeArenaBytes = 256;

/** Motivation: Distinct u32 values the probe threads through each Core container type. */
inline constexpr std::uint32_t UniqueProbeValue = 11U;
inline constexpr std::uint32_t SharedProbeValue = 13U;
inline constexpr std::uint32_t FirstStaticVectorValue = 17U;
inline constexpr std::uint32_t SecondStaticVectorValue = 19U;

/** Motivation: Bounded element count and inline callable bytes used by the probe's containers and delegates. */
inline constexpr std::size_t StaticVectorCapacity = 2;
inline constexpr std::size_t DelegateInlineBytes = 32;
inline constexpr std::size_t MulticastHandlerCount = 1;

} // namespace MicroWorldConsumer

/**
 * Motivation: Exercises representative Core memory public APIs without platform dependencies.
 * Responsibilities: Construct, use, and reclaim each memory container and report the first failure code.
 */
inline int RunMemoryConsumerProbe() noexcept
{
	using namespace MicroWorld::Core;
	using MicroWorldConsumer::EMemoryConsumerExitCode;

	MicroWorld::Core::FTickFunction CoreArchiveProbe({true, true, 0});
	CoreArchiveProbe.BeginPlay(0);
	const MicroWorld::Core::FTickDecision TickDecision = CoreArchiveProbe.Advance(0);
	CoreArchiveProbe.EndPlay();

	MicroWorld::Core::TFixedArena<MicroWorldConsumer::ProbeArenaBytes, alignof(std::max_align_t)> Arena;
	{
		const TUniquePointerResult<std::uint32_t> UniqueResult =
			MicroWorld::Core::MakeUnique<std::uint32_t>(Arena, MicroWorldConsumer::UniqueProbeValue);
		const bool bUniqueAccepted = UniqueResult.Result == MicroWorld::Core::EMemoryResult::Success && UniqueResult.Pointer.IsValid();
		const bool bUniqueValueIntact = bUniqueAccepted && *UniqueResult.Pointer.Get() == MicroWorldConsumer::UniqueProbeValue;
		if (!bUniqueValueIntact)
		{
			return static_cast<int>(EMemoryConsumerExitCode::UniquePointerConstructionFailed);
		}
	}
	{
		const TSharedPointerResult<std::uint32_t, ESharedPointerMode::SingleThreaded> SharedResult =
			MicroWorld::Core::MakeShared<std::uint32_t>(Arena, MicroWorldConsumer::SharedProbeValue);
		const bool bSharedAccepted = SharedResult.Result == MicroWorld::Core::ESharedPointerResult::Success && SharedResult.Pointer.IsValid();
		if (!bSharedAccepted)
		{
			return static_cast<int>(EMemoryConsumerExitCode::SharedPointerConstructionFailed);
		}
		const TWeakPointerResult<std::uint32_t, ESharedPointerMode::SingleThreaded> WeakResult = SharedResult.Pointer.TryAcquireWeak();
		const bool bWeakLive = WeakResult.Result == MicroWorld::Core::ESharedPointerResult::Success && !WeakResult.Pointer.IsExpired();
		if (!bWeakLive)
		{
			return static_cast<int>(EMemoryConsumerExitCode::WeakAcquireFailed);
		}
	}

	MicroWorld::Core::TStaticVector<std::uint32_t, MicroWorldConsumer::StaticVectorCapacity> Values;
	const bool bBothAddsAccepted = Values.Add(MicroWorldConsumer::FirstStaticVectorValue) == MicroWorld::Core::ERuntimeResult::Success
		&& Values.Add(MicroWorldConsumer::SecondStaticVectorValue) == MicroWorld::Core::ERuntimeResult::Success;
	if (!bBothAddsAccepted)
	{
		return static_cast<int>(EMemoryConsumerExitCode::StaticVectorAddFailed);
	}
	const MicroWorld::Core::TSpan<const std::uint32_t> ValuesView(Values.Data(), Values.Size());

	std::uint32_t DelegateTotal = 0;
	MicroWorld::Core::TDelegate<void(std::uint32_t), MicroWorldConsumer::DelegateInlineBytes> Binding;
	const EDelegateResult BindResult = Binding.Bind([&DelegateTotal](const std::uint32_t Value) noexcept { DelegateTotal += Value; });
	if (BindResult != MicroWorld::Core::EDelegateResult::Success)
	{
		return static_cast<int>(EMemoryConsumerExitCode::DelegateBindFailed);
	}
	MicroWorld::Core::TMulticastDelegate<void(std::uint32_t), MicroWorldConsumer::MulticastHandlerCount, MicroWorldConsumer::DelegateInlineBytes>
		Multicast;
	MicroWorld::Core::FDelegateHandle Handle;
	const EDelegateResult AddResult = Multicast.Add(std::move(Binding), Handle);
	const EDelegateResult BroadcastResult = Multicast.Broadcast(ValuesView[0]);
	const bool bMulticastDelivered =
		AddResult == MicroWorld::Core::EDelegateResult::Success && BroadcastResult == MicroWorld::Core::EDelegateResult::Success;
	if (!bMulticastDelivered)
	{
		return static_cast<int>(EMemoryConsumerExitCode::MulticastAddOrBroadcastFailed);
	}

	const bool bTickSucceeded = TickDecision.Result == MicroWorld::Core::ERuntimeResult::Success && TickDecision.bShouldTick;
	const bool bDelegateAccumulated = DelegateTotal == MicroWorldConsumer::FirstStaticVectorValue;
	const bool bStaticVectorSecondValueIntact = ValuesView[1] == MicroWorldConsumer::SecondStaticVectorValue;
	const bool bArenaReclaimed = Arena.UsedBytes() == 0;
	const bool bProbeSucceeded = bTickSucceeded && bDelegateAccumulated && bStaticVectorSecondValueIntact && bArenaReclaimed;
	return bProbeSucceeded ? static_cast<int>(EMemoryConsumerExitCode::Success) : static_cast<int>(EMemoryConsumerExitCode::ProbeOutcomeMismatch);
}
