#pragma once

#include <MicroWorld/Core/Delegates/Delegate.h>
#include <MicroWorld/Core/Time.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace MicroWorld::Core
{

/**
 * Motivation: Gives every bounded timer operation one result vocabulary that does not borrow unrelated
 *   lifecycle errors.
 * Responsibilities: Distinguish success from capacity, callback, handle, mode, dispatch, and time-rollback failures.
 * Example:
 *   if (Manager.Cancel(Handle) == ETimerResult::StaleHandle) { IgnoreStale(); }
 */
enum class ETimerResult : std::uint8_t
{
	/** Motivation: Confirms that the requested timer operation completed. */
	Success,

	/** Motivation: Reports that no reusable timer slot remains, including zero capacity and retired generations. */
	CapacityExceeded,

	/** Motivation: Rejects an unbound delegate before any slot is consumed or callback ownership moves. */
	InvalidCallback,

	/** Motivation: Rejects a default, sentinel, or out-of-range handle before consulting slot state. */
	InvalidHandle,

	/** Motivation: Rejects a handle whose slot is free, retired, removed, expired, or holds another generation. */
	StaleHandle,

	/** Motivation: Rejects a timer mode that is neither OneShot nor Looping. */
	InvalidMode,

	/** Motivation: Prevents Schedule, Cancel, and nested Advance from mutating an active dispatch. */
	DispatchLocked,

	/** Motivation: Prevents unsigned time arithmetic from accepting a rolled-back caller clock. */
	NonMonotonicTime,
};

/**
 * Motivation: Selects one timer schedule shape independently of its bound callback.
 * Responsibilities: Distinguish the unscheduled default from one-shot and looping schedules.
 * Example:
 *   ETimerMode Mode = ETimerMode::Looping;
 */
enum class ETimerMode : std::uint8_t
{
	/** Motivation: Rejects scheduling so an uninitialized mode never silently becomes OneShot or Looping. */
	None,

	/** Motivation: Fires once and removes the timer so its handle becomes stale. */
	OneShot,

	/** Motivation: Reschedules from the accepted NowMilliseconds after each fire and stays in insertion order. */
	Looping,
};

/**
 * Motivation: Lets a caller carry one live timer identity without exposing storage or extending the callback's lifetime.
 * Responsibilities: Pair a slot index with a generation and never mutate on its own; a handle is local to the
 *   manager that issued it and must not be carried between managers.
 * Example:
 *   FTimerHandle Handle;
 *   if (Handle.IsValid()) { Manager.Cancel(Handle); }
 */
struct FTimerHandle final
{
	/** Motivation: Reserves the maximum index as the invalid sentinel independent of manager capacity. */
	static constexpr std::uint16_t InvalidIndex = std::numeric_limits<std::uint16_t>::max();

	/** Motivation: Selects the fixed slot while preserving an explicit invalid sentinel. */
	std::uint16_t Index{InvalidIndex};

	/** Motivation: Distinguishes successive schedules that occupy the same slot. */
	std::uint32_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning manager.
	 * Responsibilities: Report true only when the index and generation together look like a live timer.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex && Generation != 0; }

	/**
	 * Motivation: Lets containers compare two handles by complete stable identity.
	 * Responsibilities: Return true only when both index and generation match.
	 */
	friend constexpr bool operator==(const FTimerHandle InLeft, const FTimerHandle InRight) noexcept
	{
		return InLeft.Index == InRight.Index && InLeft.Generation == InRight.Generation;
	}

	/**
	 * Motivation: Lets a caller tell two handles apart by stable identity.
	 * Responsibilities: Return true whenever the slot or generation identity differs.
	 */
	friend constexpr bool operator!=(const FTimerHandle InLeft, const FTimerHandle InRight) noexcept { return !(InLeft == InRight); }
};

/**
 * Motivation: Confirms that one more live generation can be published without wrapping.
 * Responsibilities: Report whether the generation is below its maximum, so a manager can retire a slot before
 *   wrapping would make an old handle valid again.
 */
constexpr bool CanAdvanceTimerGeneration(const std::uint32_t InCurrentGeneration) noexcept
{
	return InCurrentGeneration < std::numeric_limits<std::uint32_t>::max();
}

/**
 * Motivation: Owns a bounded set of caller-scheduled timers and dispatches them deterministically.
 * Responsibilities: Store the last accepted caller time, fire each due timer in stable insertion order, and never
 *   read a hidden clock, accept a rolled-back clock, or let mutation disturb an active dispatch.
 * Example:
 *   TTimerManager<8, 16> Manager(Now);
 *   Manager.Schedule(std::move(Callback), 100, ETimerMode::OneShot, Handle);
 *   Manager.Advance(Now + 100);
 */
template<std::size_t MaxTimers, std::size_t InlineTimerCallbackBytes>
class TTimerManager final
{
	static_assert(MaxTimers < FTimerHandle::InvalidIndex, "A timer manager capacity must fit below the reserved handle index.");
	static_assert(InlineTimerCallbackBytes > 0, "A timer manager must reserve inline callback storage for its delegates.");

public:
	/**
	 * Motivation: Gives the manager one caller-owned clock baseline for every later operation.
	 * Responsibilities: Store the caller's initial time as LastAcceptedNowMilliseconds.
	 */
	explicit TTimerManager(const TimePointMilliseconds InInitialNow) noexcept : LastAcceptedNowMilliseconds{InInitialNow} {}

	/**
	 * Motivation: Ensures no bound callback outlives the manager that owns its slot.
	 * Responsibilities: Destroy every bound callback without invoking any of them; each FTimerSlot's TDelegate
	 *   member destroys its callable exactly once, so no explicit Reset loop is needed.
	 */
	~TTimerManager() noexcept = default;

	/**
	 * Motivation: Prevents copying from duplicating uniquely owned callbacks and slot identity.
	 * Responsibilities: Reject copy construction so the manager stays the single owner of its slots.
	 */
	TTimerManager(const TTimerManager&) = delete;

	/**
	 * Motivation: Prevents copy assignment from duplicating uniquely owned callback and slot identity.
	 * Responsibilities: Reject copy assignment so the manager stays the single owner of its slots.
	 */
	TTimerManager& operator=(const TTimerManager&) = delete;

	/**
	 * Motivation: Keeps the manager at one deliberately simple application-owned lifetime and identity.
	 * Responsibilities: Reject move construction so handles, which are plain {index, generation} pairs local to one
	 *   issuing manager, are never carried across a relocation.
	 */
	TTimerManager(TTimerManager&&) = delete;

	/**
	 * Motivation: Prevents move assignment for the same application-owned lifetime and identity reason as the deleted move ctor.
	 * Responsibilities: Reject move assignment so handles stay local to one issuing manager.
	 */
	TTimerManager& operator=(TTimerManager&&) = delete;

	/**
	 * Motivation: Lets a caller arm one bound delegate using a single duration as first delay and repeat period.
	 * Responsibilities: Reject mutation during dispatch, an invalid mode, and an unbound callback, then move the
	 *   delegate into a reusable slot and publish a fresh generation-checked handle; on failure clear OutHandle and
	 *   leave InCallback bound.
	 */
	ETimerResult Schedule(
		TDelegate<void(), InlineTimerCallbackBytes>&& InCallback,
		const DurationMilliseconds InDelayAndPeriodMilliseconds,
		const ETimerMode InMode,
		FTimerHandle& OutHandle) noexcept
	{
		OutHandle = {};
		if (bDispatchActive)
		{
			return ETimerResult::DispatchLocked;
		}
		// Explicit allowlist so neither ETimerMode::None nor any arbitrary cast value
		// (e.g. static_cast<ETimerMode>(3)) can silently become a valid schedule shape.
		if (InMode != ETimerMode::OneShot && InMode != ETimerMode::Looping)
		{
			return ETimerResult::InvalidMode;
		}
		if (!InCallback.IsBound())
		{
			return ETimerResult::InvalidCallback;
		}

		FTimerSlot* const AvailableSlot = FindAvailableSlot();
		if (AvailableSlot == nullptr)
		{
			return ETimerResult::CapacityExceeded;
		}

		const std::size_t SlotIndex = static_cast<std::size_t>(AvailableSlot - Slots);
		const DurationMilliseconds PeriodMilliseconds = (InMode == ETimerMode::Looping) ? InDelayAndPeriodMilliseconds : DurationMilliseconds{0};
		AvailableSlot->Arm(
			std::move(InCallback), SaturatingAdd(LastAcceptedNowMilliseconds, InDelayAndPeriodMilliseconds), PeriodMilliseconds, InMode);

		const FTimerHandle PublishedHandle{static_cast<std::uint16_t>(SlotIndex), AvailableSlot->Generation};
		InsertionOrder[ActiveTimerCount] = PublishedHandle;
		++ActiveTimerCount;
		OutHandle = PublishedHandle;
		return ETimerResult::Success;
	}

	/**
	 * Motivation: Lets a caller remove one timer by its current generation-checked handle.
	 * Responsibilities: Reject mutation during dispatch and remove exactly the identified timer.
	 */
	ETimerResult Cancel(const FTimerHandle InHandle) noexcept
	{
		if (bDispatchActive)
		{
			return ETimerResult::DispatchLocked;
		}
		if (!InHandle.IsValid() || static_cast<std::size_t>(InHandle.Index) >= MaxTimers)
		{
			return ETimerResult::InvalidHandle;
		}

		FTimerSlot& Slot = Slots[InHandle.Index];
		if (!Slot.bActive || Slot.Generation != InHandle.Generation)
		{
			return ETimerResult::StaleHandle;
		}

		CancelActiveSlot(Slot, InHandle);
		return ETimerResult::Success;
	}

	/**
	 * Motivation: Lets a dispatcher fire each timer due at the caller-supplied time in stable insertion order.
	 * Responsibilities: Reject reentrant dispatch and a rolled-back clock transactionally, then fire the snapshotted
	 *   due timers and compact completed one-shots in one stable pass so dispatch stays O(active + removed) total.
	 */
	ETimerResult Advance(const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (bDispatchActive)
		{
			return ETimerResult::DispatchLocked;
		}
		if (InNowMilliseconds < LastAcceptedNowMilliseconds)
		{
			return ETimerResult::NonMonotonicTime;
		}
		LastAcceptedNowMilliseconds = InNowMilliseconds;

		const std::size_t SnapshotCount = SnapshotActiveTimers();

		bDispatchActive = true;
		for (std::size_t SnapshotIndex = 0; SnapshotIndex < SnapshotCount; ++SnapshotIndex)
		{
			FireAndRescheduleSlot(DispatchSnapshot[SnapshotIndex], InNowMilliseconds);
		}
		bDispatchActive = false;

		CompactInsertionOrder();
		return ETimerResult::Success;
	}

	/**
	 * Motivation: Lets a caller report how many timers the next successful Advance may visit.
	 * Responsibilities: Return the exact count of active timers.
	 */
	std::size_t TimerCount() const noexcept { return ActiveTimerCount; }

	/**
	 * Motivation: Lets a caller test capacity against the fixed limit without magic numbers.
	 * Responsibilities: Report the compile-time upper bound on live timers and dispatch work.
	 */
	static constexpr std::size_t Capacity() noexcept { return MaxTimers; }

private:
	/**
	 * Motivation: Owns one reusable inline callback plus its schedule and identity state.
	 * Responsibilities: Hold the callback, deadline, period, mode, and generation-checked identity for one timer slot.
	 * Example:
	 *   FTimerSlot Slot;
	 *   Slot.Arm(std::move(Callback), 100, 100, ETimerMode::Looping);
	 */
	struct FTimerSlot final
	{
		/** Motivation: Owns the callable only while this slot is active. */
		TDelegate<void(), InlineTimerCallbackBytes> Callback;

		/** Motivation: Stores the absolute time at which this timer next becomes due. */
		TimePointMilliseconds DeadlineMilliseconds{0};

		/** Motivation: Stores the looping repeat period; zero marks one-shot or zero-period looping. */
		DurationMilliseconds PeriodMilliseconds{0};

		/** Motivation: Guards nonzero-period looping timers against refiring at the same accepted NowMilliseconds. */
		TimePointMilliseconds LastFiredMilliseconds{0};

		/** Motivation: Distinguishes successive schedules that occupy this slot. */
		std::uint32_t Generation{1};

		/** Motivation: Records the schedule shape that owns this slot's removal or reschedule behavior. */
		ETimerMode Mode{ETimerMode::None};

		/** Motivation: Distinguishes a live timer from reusable unoccupied slot state. */
		bool bActive{false};

		/** Motivation: Permanently removes this slot once its generation space is exhausted. */
		bool bRetired{false};

		/**
		 * Motivation: Populates every schedule field for a freshly claimed slot.
		 * Responsibilities: Set callback, deadline, period, mode, and active flag, leaving generation and retirement
		 *   identity untouched so slot reuse stays generation-checked.
		 */
		void Arm(
			TDelegate<void(), InlineTimerCallbackBytes>&& InCallback,
			const TimePointMilliseconds InFirstDeadlineMilliseconds,
			const DurationMilliseconds InPeriodMilliseconds,
			const ETimerMode InMode) noexcept
		{
			Callback = std::move(InCallback);
			DeadlineMilliseconds = InFirstDeadlineMilliseconds;
			PeriodMilliseconds = InPeriodMilliseconds;
			LastFiredMilliseconds = TimePointMilliseconds{0};
			Mode = InMode;
			bActive = true;
		}
	};

	/**
	 * Motivation: Lets Schedule locate the next slot without disturbing insertion order.
	 * Responsibilities: Return the lowest unoccupied, unretired slot, or null when none remains.
	 */
	FTimerSlot* FindAvailableSlot() noexcept
	{
		for (std::size_t SlotIndex = 0; SlotIndex < MaxTimers; ++SlotIndex)
		{
			FTimerSlot& Slot = Slots[SlotIndex];
			if (!Slot.bActive && !Slot.bRetired)
			{
				return &Slot;
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Lets Cancel tear down one timer outside dispatch.
	 * Responsibilities: Reset the callback, retire or advance the slot identity, remove its insertion-order entry,
	 *   and drop the count; Advance clears completed one-shots in place and leaves them to the compaction pass.
	 */
	void CancelActiveSlot(FTimerSlot& InSlot, const FTimerHandle InHandle) noexcept
	{
		InSlot.Callback.Reset();
		InSlot.bActive = false;
		AdvanceGenerationOrRetire(InSlot);
		RemoveInsertionOrderAt(InHandle);
		--ActiveTimerCount;
	}

	/**
	 * Motivation: Keeps a reused slot from matching an old handle as generations approach wrap.
	 * Responsibilities: Advance the generation, or permanently retire the slot before it can wrap.
	 */
	static void AdvanceGenerationOrRetire(FTimerSlot& InSlot) noexcept
	{
		if (!CanAdvanceTimerGeneration(InSlot.Generation))
		{
			InSlot.bRetired = true;
			return;
		}
		++InSlot.Generation;
	}

	/**
	 * Motivation: Freezes the timers active at Advance entry so dispatch visits a stable set.
	 * Responsibilities: Copy the active insertion-order entries into the snapshot and return the count that bounds
	 *   the dispatch loop, so a callback that schedules or cancels cannot change the visited set.
	 */
	std::size_t SnapshotActiveTimers() noexcept
	{
		const std::size_t SnapshotCount = ActiveTimerCount;
		for (std::size_t SnapshotIndex = 0; SnapshotIndex < SnapshotCount; ++SnapshotIndex)
		{
			DispatchSnapshot[SnapshotIndex] = InsertionOrder[SnapshotIndex];
		}
		return SnapshotCount;
	}

	/**
	 * Motivation: Fires one snapshotted timer if it is still live and due.
	 * Responsibilities: Skip a stale, inactive, not-yet-due, or already-fired-this-instant slot without firing,
	 *   then retire a completed one-shot in place or reschedule a looping timer.
	 */
	void FireAndRescheduleSlot(const FTimerHandle InHandle, const TimePointMilliseconds InNowMilliseconds) noexcept
	{
		if (static_cast<std::size_t>(InHandle.Index) >= MaxTimers)
		{
			return;
		}

		FTimerSlot& Slot = Slots[InHandle.Index];
		if (!Slot.bActive || Slot.Generation != InHandle.Generation)
		{
			return;
		}
		if (InNowMilliseconds < Slot.DeadlineMilliseconds)
		{
			return;
		}
		// Guards a nonzero-period looping timer against refiring when NowMilliseconds has not advanced
		// past the previously accepted timestamp, including after deadline saturation.
		if (Slot.PeriodMilliseconds != 0 && Slot.LastFiredMilliseconds == InNowMilliseconds)
		{
			return;
		}

		// Dispatch is locked for the whole Advance, so no caller Schedule, Cancel, or nested
		// Advance can cancel this slot, replace its callback, or reschedule it while the
		// active bound callback executes. Execute is noexcept and the bound callable is live.
		(void)Slot.Callback.Execute();

		if (Slot.Mode == ETimerMode::OneShot)
		{
			// Clear and retire the slot in place; the live insertion-order entry is dropped
			// by the single post-dispatch compaction pass so no per-fired shift is needed.
			Slot.Callback.Reset();
			Slot.bActive = false;
			AdvanceGenerationOrRetire(Slot);
		}
		else
		{
			if (Slot.PeriodMilliseconds != 0)
			{
				Slot.LastFiredMilliseconds = InNowMilliseconds;
			}
			Slot.DeadlineMilliseconds = SaturatingAdd(InNowMilliseconds, Slot.PeriodMilliseconds);
		}
	}

	/**
	 * Motivation: Drops every insertion-order entry whose slot is no longer active in one stable pass.
	 * Responsibilities: Remove the cleared one-shots after dispatch while preserving the relative order of survivors,
	 *   never consulting callback state or shifting a survivor past another.
	 */
	void CompactInsertionOrder() noexcept
	{
		std::size_t WriteIndex = 0;
		for (std::size_t ReadIndex = 0; ReadIndex < ActiveTimerCount; ++ReadIndex)
		{
			const FTimerHandle Handle = InsertionOrder[ReadIndex];
			if (static_cast<std::size_t>(Handle.Index) >= MaxTimers)
			{
				continue;
			}
			const FTimerSlot& Slot = Slots[Handle.Index];
			if (Slot.bActive && Slot.Generation == Handle.Generation)
			{
				InsertionOrder[WriteIndex] = Handle;
				++WriteIndex;
			}
		}
		for (std::size_t ClearIndex = WriteIndex; ClearIndex < ActiveTimerCount; ++ClearIndex)
		{
			InsertionOrder[ClearIndex] = {};
		}
		ActiveTimerCount = WriteIndex;
	}

	/**
	 * Motivation: Lets Cancel close the gap left by a removed timer in insertion order.
	 * Responsibilities: Shift later entries down without changing any remaining slot identity.
	 */
	void RemoveInsertionOrderAt(const FTimerHandle InRemovedHandle) noexcept
	{
		std::size_t OrderIndex = ActiveTimerCount;
		for (std::size_t SearchIndex = 0; SearchIndex < ActiveTimerCount; ++SearchIndex)
		{
			if (InsertionOrder[SearchIndex] == InRemovedHandle)
			{
				OrderIndex = SearchIndex;
				break;
			}
		}
		if (OrderIndex == ActiveTimerCount)
		{
			return;
		}
		for (std::size_t ShiftIndex = OrderIndex; ShiftIndex + 1U < ActiveTimerCount; ++ShiftIndex)
		{
			InsertionOrder[ShiftIndex] = InsertionOrder[ShiftIndex + 1U];
		}
		InsertionOrder[ActiveTimerCount - 1U] = {};
	}

	/**
	 * Motivation: Lets scheduling add to the time without wrapping into an early deadline.
	 * Responsibilities: Return InBase plus InAddend, saturating at the TimePointMilliseconds maximum.
	 */
	static constexpr TimePointMilliseconds SaturatingAdd(const TimePointMilliseconds InBase, const DurationMilliseconds InAddend) noexcept
	{
		const TimePointMilliseconds MaximumTime = std::numeric_limits<TimePointMilliseconds>::max();
		return (MaximumTime - InBase < static_cast<TimePointMilliseconds>(InAddend)) ? MaximumTime
																					 : InBase + static_cast<TimePointMilliseconds>(InAddend);
	}

	/** Motivation: Owns all bounded callback storage independently of insertion order. */
	// C++ forbids zero-length arrays; the "== 0 ? 1" guard on these three arrays
	// keeps a zero-capacity (MaxTimers == 0) manager well-formed.
	FTimerSlot Slots[MaxTimers == 0 ? 1 : MaxTimers];

	/** Motivation: Preserves deterministic insertion order while slots are removed and reused. */
	FTimerHandle InsertionOrder[MaxTimers == 0 ? 1 : MaxTimers];

	/** Motivation: Snapshots the timers active at Advance entry so dispatch visits each at most once. */
	FTimerHandle DispatchSnapshot[MaxTimers == 0 ? 1 : MaxTimers];

	/** Motivation: Stores the last accepted caller time so scheduling never reads a hidden clock. */
	TimePointMilliseconds LastAcceptedNowMilliseconds{0};

	/** Motivation: Bounds insertion-order traversal and makes current timer count observable. */
	std::size_t ActiveTimerCount{0};

	/** Motivation: Rejects Schedule, Cancel, and nested Advance while dispatch iteration is active. */
	bool bDispatchActive{false};
};

} // namespace MicroWorld::Core
