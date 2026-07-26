#pragma once

#include <MicroWorld/EngineSystem.h>
#include <MicroWorld/Engine/EngineResult.h>

#include <cstddef>

namespace MicroWorld
{

/**
 * Adapts one caller-owned network host to IEngineSystem by forwarding lifecycle
 * turns to Start/Stop and frame turns to PumpReceive/PumpSend,
 * discarding each
 * result exactly as the engine already discards timer and collector step results.
 *
 * TNet is deduced at the call site, so the
 * engine binds a network host without
 * naming its concrete type or including its package. The host must outlive this
 * adapter, and the adapter
 * must outlive the TEngine it is bound to.
 */
template<typename TNet>
class TNetHostSystem final : public IEngineSystem
{
public:
	/** Binds this adapter to one externally owned network host for its lifetime. */
	explicit TNetHostSystem(TNet& InHost) noexcept : Host(InHost) {}

	/** Opens the bound host session at the engine's canonical play-start time. */
	void BeginPlay(const TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.Start(InNowMilliseconds); }

	/** Forwards the frame's inbound step to the bound host. */
	void PreAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.PumpReceive(InNowMilliseconds); }

	/** Forwards the frame's outbound step to the bound host. */
	void PostAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.PumpSend(InNowMilliseconds); }

	/** Closes the bound host session after the engine world has ended. */
	void EndPlay() noexcept override { Host.Stop(); }

private:
	/** The externally owned network host this adapter drives; never owned here. */
	TNet& Host;
};

/**
 * Pumps several caller-owned systems as one bound IEngineSystem (roadmap D3):
 * lifecycle start and inbound dispatch run in add-order, while
 * lifecycle end
 * and outbound flush run in reverse add-order. This lets a net host deliver
 * inbound traffic before a router handles it, then lets
 * the router queue
 * outbound traffic before the net host sends it.
 *
 * The set only stores pointers to caller-owned systems, so it never
 * allocates
 * and never owns their lifetime; every added system must outlive this set.
 */
template<std::size_t MaxFrames>
class TEngineSystemSet final : public IEngineSystem
{
public:
	/** Creates a set with no frames added. */
	TEngineSystemSet() noexcept = default;

	/** Virtual destructor via the base; the set owns no frame, so there is nothing else to release. */
	~TEngineSystemSet() noexcept override = default;

	// TEngine holds this set by IEngineSystem&, and each added frame is captured by raw
	// pointer; relocating the set would dangle both references, so copy and move are deleted
	// (the same fixed-identity rule TMessageRouter and TMessageChannelBinding already follow).
	TEngineSystemSet(const TEngineSystemSet&) = delete;
	TEngineSystemSet& operator=(const TEngineSystemSet&) = delete;
	TEngineSystemSet(TEngineSystemSet&&) = delete;
	TEngineSystemSet& operator=(TEngineSystemSet&&) = delete;

	/**
	 * Adds one caller-owned system; the order of Add calls becomes the BeginPlay
	 * and PreAdvance order. Rejects a system already present as
	 * Duplicate (matched
	 * by pointer identity) and a full
	 * set as CapacityExceeded, leaving the set unchanged in both cases.
	 */
	EEngineResult Add(IEngineSystem& InFrame) noexcept
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

	/** Starts every added system in add-order. An empty set does nothing. */
	void BeginPlay(const TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			Frames[Index]->BeginPlay(InNowMilliseconds);
		}
	}

	/** Dispatches every added system's inbound step in add-order. An empty set does nothing. */
	void PreAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			Frames[Index]->PreAdvance(InNowMilliseconds);
		}
	}

	/** Flushes every added system's outbound step in reverse add-order. An empty set does nothing. */
	void PostAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = Count; Index > 0; --Index)
		{
			Frames[Index - 1]->PostAdvance(InNowMilliseconds);
		}
	}

	/** Ends every added system in reverse add-order. An empty set does nothing. */
	void EndPlay() noexcept override
	{
		for (std::size_t Index = Count; Index > 0; --Index)
		{
			Frames[Index - 1]->EndPlay();
		}
	}

	/** Reports how many frames have been added so far. */
	std::size_t FrameCount() const noexcept { return Count; }

private:
	/** Caller-owned systems in add-order; never owned here. */
	IEngineSystem* Frames[MaxFrames == 0 ? 1 : MaxFrames]{};

	/** Number of occupied entries at the front of Frames. */
	std::size_t Count{0};
};

} // namespace MicroWorld
