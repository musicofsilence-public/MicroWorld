#pragma once

#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>
#include <MicroWorld/Engine/EngineResult.h>

#include <cstddef>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets several caller-owned systems be pumped together as one bound IPlaySystem so, for example, a transport
 *   host can deliver inbound traffic before a router handles it and queue outbound traffic before the transport sends it.
 * Responsibilities: Run lifecycle start and inbound dispatch in add-order and lifecycle end and outbound flush in reverse
 *   add-order, store only caller-owned pointers so it never allocates or owns a lifetime, and require every added system
 *   to outlive this set.
 * Example:
 *   TPlaySystemSet<4> Set;
 *   (void)Set.Add(Transport); (void)Set.Add(Router);
 *   Engine.Bind(Set);
 */
template<std::size_t MaxFrames>
class TPlaySystemSet final : public Core::IPlaySystem
{
public:
	/**
	 * Motivation: Lets a caller create a set with no frames added.
	 * Responsibilities: Produce an empty set that does nothing on every turn.
	 */
	TPlaySystemSet() noexcept = default;

	/**
	 * Motivation: Ensures destruction through the base interface releases no frame, since the set owns none.
	 * Responsibilities: Default-destroy without touching any added system.
	 */
	~TPlaySystemSet() noexcept override = default;

	// TEngine holds this set by IPlaySystem&, and each added frame is captured by raw
	// pointer; relocating the set would dangle both references, so copy and move are deleted
	// (the same fixed-identity rule every other engine-held system already follows).
	/**
	 * Motivation: Prevents relocating this set from dangling the IPlaySystem& an engine holds and the raw pointers it captures.
	 * Responsibilities: Reject copy construction so the set keeps a fixed identity.
	 */
	TPlaySystemSet(const TPlaySystemSet&) = delete;

	/**
	 * Motivation: Prevents relocating this set from dangling the IPlaySystem& an engine holds and the raw pointers it captures.
	 * Responsibilities: Reject copy assignment so the set keeps a fixed identity.
	 */
	TPlaySystemSet& operator=(const TPlaySystemSet&) = delete;

	/**
	 * Motivation: Prevents relocating this set from dangling the IPlaySystem& an engine holds and the raw pointers it captures.
	 * Responsibilities: Reject move construction so the set keeps a fixed identity.
	 */
	TPlaySystemSet(TPlaySystemSet&&) = delete;

	/**
	 * Motivation: Prevents relocating this set from dangling the IPlaySystem& an engine holds and the raw pointers it captures.
	 * Responsibilities: Reject move assignment so the set keeps a fixed identity.
	 */
	TPlaySystemSet& operator=(TPlaySystemSet&&) = delete;

	/**
	 * Motivation: Lets a caller add systems in the order they should start and dispatch, so Add order becomes BeginPlay and
	 *   PreAdvance order.
	 * Responsibilities: Reject a system already present (by pointer identity) as Duplicate and a full set as
	 *   CapacityExceeded, leaving the set unchanged in both cases.
	 */
	EEngineResult Add(Core::IPlaySystem& InFrame) noexcept
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			if (Frames[Index] == &InFrame)
			{
				return EEngineResult::Duplicate;
			}
		}
		if (Count == MaxFrames)
		{
			return EEngineResult::CapacityExceeded;
		}

		Frames[Count] = &InFrame;
		++Count;
		return EEngineResult::Success;
	}

	/**
	 * Motivation: Starts every added system in add-order during the engine's BeginPlay.
	 * Responsibilities: Forward BeginPlay to each frame in order; an empty set does nothing.
	 */
	void BeginPlay(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			Frames[Index]->BeginPlay(InNowMilliseconds);
		}
	}

	/**
	 * Motivation: Dispatches every added system's inbound step in add-order during each frame.
	 * Responsibilities: Forward PreAdvance to each frame in order; an empty set does nothing.
	 */
	void PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			Frames[Index]->PreAdvance(InNowMilliseconds);
		}
	}

	/**
	 * Motivation: Flushes every added system's outbound step in reverse add-order so a router queues before a transport sends.
	 * Responsibilities: Forward PostAdvance to each frame in reverse order; an empty set does nothing.
	 */
	void PostAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = Count; Index > 0; --Index)
		{
			Frames[Index - 1]->PostAdvance(InNowMilliseconds);
		}
	}

	/**
	 * Motivation: Ends every added system in reverse add-order so cleanup mirrors startup.
	 * Responsibilities: Forward EndPlay to each frame in reverse order; an empty set does nothing.
	 */
	void EndPlay() noexcept override
	{
		for (std::size_t Index = Count; Index > 0; --Index)
		{
			Frames[Index - 1]->EndPlay();
		}
	}

	/**
	 * Motivation: Lets a caller report how many frames have been added so far.
	 * Responsibilities: Return the current frame count.
	 */
	std::size_t FrameCount() const noexcept { return Count; }

private:
	/** Motivation: Caller-owned systems in add-order; never owned here. */
	Core::IPlaySystem* Frames[MaxFrames == 0 ? 1 : MaxFrames]{};

	/** Motivation: Number of occupied entries at the front of Frames. */
	std::size_t Count{0};
};

} // namespace MicroWorld::Engine
