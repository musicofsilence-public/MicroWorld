#pragma once

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Time.h>

#include <cstddef>

namespace MicroWorld
{

/**
 * The contract for a system the engine ticks either side of the world's advance:
 * TEngine::Tick step 1 gives a bound system its pre-advance turn and step 7
 * gives it its post-advance turn, so a system can pump work in and out without
 * the engine naming what that work is.
 *
 * TEngine holds only this interface, so microworld-engine never depends on
 * microworld-net; a concrete system (a network host, a message router, a reliable
 * channel) is bound by the caller through TNetHostSystem, and a null system
 * leaves both turns inert.
 */
class IEngineSystem
{
public:
	/** Defaulted virtual so a derived system adapter destructs through this interface. */
	virtual ~IEngineSystem() noexcept = default;

	/** Step 1 turn: a bound system does its pre-advance work (for a net host, drain inbound traffic, dispatch messages, age peers). */
	virtual void PreAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;

	/** Step 7 turn: a bound system does its post-advance work (for a net host, flush the queue and emit due heartbeats). */
	virtual void PostAdvance(TimePointMilliseconds InNowMilliseconds) noexcept = 0;
};

/**
 * Adapts one caller-owned network host to IEngineSystem by forwarding the two
 * frame steps to its PumpReceive/PumpSend, discarding the transport result exactly
 * as the engine already discards its timer and collector step results.
 *
 * TNet is deduced at the call site, so the engine binds a network host without
 * naming its concrete type or including its package. The host must outlive this
 * adapter, and the adapter must outlive the TEngine it is bound to.
 */
template<typename TNet>
class TNetHostSystem final : public IEngineSystem
{
public:
	/** Binds this adapter to one externally owned network host for its lifetime. */
	explicit TNetHostSystem(TNet& InHost) noexcept : Host(InHost) {}

	/** Forwards the frame's inbound step to the bound host. */
	void PreAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.PumpReceive(InNowMilliseconds); }

	/** Forwards the frame's outbound step to the bound host. */
	void PostAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override { (void)Host.PumpSend(InNowMilliseconds); }

private:
	/** The externally owned network host this adapter drives; never owned here. */
	TNet& Host;
};

/**
 * Pumps several caller-owned network frames as one bound IEngineSystem (roadmap D3): dispatch
 * runs in add-order (a net frame first delivers its inbound traffic before a router dispatches
 * it to handlers), while flush runs in reverse add-order (the router queues its outbound traffic
 * before the net frame sends it). This is how a message channel binding composes a TNetHostSystem
 * and a TMessageRouter behind the one IEngineSystem slot TEngine drives.
 *
 * The set only stores pointers to caller-owned frames, so it never allocates and never owns their
 * lifetime; every added frame must outlive this set.
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
	 * Adds one caller-owned frame; the order of Add calls becomes the PreAdvance order.
	 * Rejects a frame already present as Duplicate (matched by pointer identity) and a full
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

	/** Dispatches every added frame's inbound step in add-order. An empty set does nothing. */
	void PreAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			Frames[Index]->PreAdvance(InNowMilliseconds);
		}
	}

	/** Flushes every added frame's outbound step in reverse add-order. An empty set does nothing. */
	void PostAdvance(const TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		for (std::size_t Index = Count; Index > 0; --Index)
		{
			Frames[Index - 1]->PostAdvance(InNowMilliseconds);
		}
	}

	/** Reports how many frames have been added so far. */
	std::size_t FrameCount() const noexcept { return Count; }

private:
	/** Caller-owned frames in add-order; never owned here. */
	IEngineSystem* Frames[MaxFrames == 0 ? 1 : MaxFrames]{};

	/** Number of occupied entries at the front of Frames. */
	std::size_t Count{0};
};

} // namespace MicroWorld
