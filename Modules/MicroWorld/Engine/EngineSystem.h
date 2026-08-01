#pragma once

#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Engine/EngineResult.h>

#include <cstddef>

namespace MicroWorld::Engine
{

/**
 * Motivation: Lets a caller-owned network host be driven by the engine as a Core::IPlaySystem without the engine naming
 *   or including the host's concrete type.
 * Responsibilities: Forward lifecycle turns to Start/Stop and frame turns to PumpReceive/PumpSend, discarding each result
 *   just as the engine already discards timer and collector step results; the host must outlive the adapter, which must
 *   outlive the TEngine it binds.
 * Example:
 *   THostPlaySystem<FTransportHost> System(TransportHost);
 *   Engine.Bind(System);
 */
template<typename THost>
class THostPlaySystem final : public Core::IPlaySystem
{
public:
	/**
	 * Motivation: Binds this adapter to one externally owned network host for its lifetime.
	 * Responsibilities: Store the host reference without taking ownership.
	 */
	explicit THostPlaySystem(THost& InHost) noexcept : Host(InHost) {}

	/**
	 * Motivation: Opens the bound host session at the engine's canonical play-start time.
	 * Responsibilities: Forward to Host.Start and discard its result.
	 */
	void BeginPlay(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.Start(InNowMilliseconds); }

	/**
	 * Motivation: Forwards the frame's inbound step to the bound host.
	 * Responsibilities: Call Host.PumpReceive and discard its result.
	 */
	void PreAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.PumpReceive(InNowMilliseconds); }

	/**
	 * Motivation: Forwards the frame's outbound step to the bound host.
	 * Responsibilities: Call Host.PumpSend and discard its result.
	 */
	void PostAdvance(const Core::TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.PumpSend(InNowMilliseconds); }

	/**
	 * Motivation: Closes the bound host session after the engine world has ended.
	 * Responsibilities: Forward to Host.Stop.
	 */
	void EndPlay() noexcept override { Host.Stop(); }

private:
	/** Motivation: The externally owned network host this adapter drives; never owned here. */
	THost& Host;
};

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
