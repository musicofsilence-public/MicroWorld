#pragma once

#include <MicroWorld/Engine/EngineResult.h>
#include <MicroWorld/Time.h>

#include <cstddef>

namespace MicroWorld
{

/**
 * The frame-facing network seam the engine advances each tick, named after UE5's
 * UNetDriver: TickDispatch processes inbound traffic at frame start; TickFlush
 * sends queued outbound traffic at frame end.
 *
 * TEngineHost holds only this interface, so microworld-engine never depends on
 * microworld-net; the concrete network host is bound by the caller through
 * TNetHostFrame. A null frame simply leaves both slots inert.
 */
class INetworkFrame
{
public:
	/** Defaulted virtual so a derived frame adapter destructs through this interface. */
	virtual ~INetworkFrame() noexcept = default;

	/** Processes inbound traffic for one frame: drains the driver, dispatches messages, ages peers. */
	virtual void TickDispatch(TimePointMilliseconds NowMilliseconds) noexcept = 0;

	/** Sends outbound traffic for one frame: flushes the queue and emits due heartbeats. */
	virtual void TickFlush(TimePointMilliseconds NowMilliseconds) noexcept = 0;
};

/**
 * Adapts one caller-owned network host to INetworkFrame by forwarding the two
 * frame steps to its PumpReceive/PumpSend, discarding the transport result exactly
 * as the engine already discards its timer and collector step results.
 *
 * TNet is deduced at the call site, so the engine binds a network host without
 * naming its concrete type or including its package. The host must outlive this
 * adapter, and the adapter must outlive the TEngineHost it is bound to.
 */
template<typename TNet>
class TNetHostFrame final : public INetworkFrame
{
public:
	/** Binds this adapter to one externally owned network host for its lifetime. */
	explicit TNetHostFrame(TNet& InHost) noexcept : Host(InHost) {}

	/** Forwards the frame's inbound step to the bound host. */
	void TickDispatch(const TimePointMilliseconds NowMilliseconds) noexcept override { (void)Host.PumpReceive(NowMilliseconds); }

	/** Forwards the frame's outbound step to the bound host. */
	void TickFlush(const TimePointMilliseconds NowMilliseconds) noexcept override { (void)Host.PumpSend(NowMilliseconds); }

private:
	/** The externally owned network host this adapter drives; never owned here. */
	TNet& Host;
};

/**
 * Pumps several caller-owned network frames as one bound INetworkFrame (roadmap D3): dispatch
 * runs in add-order (a net frame first delivers its inbound traffic before a router dispatches
 * it to handlers), while flush runs in reverse add-order (the router queues its outbound traffic
 * before the net frame sends it). This is how a message channel binding composes a TNetHostFrame
 * and a TMessageRouter behind the one INetworkFrame slot TEngineHost drives.
 *
 * The set only stores pointers to caller-owned frames, so it never allocates and never owns their
 * lifetime; every added frame must outlive this set.
 */
template<std::size_t MaxFrames>
class TNetworkFrameSet final : public INetworkFrame
{
public:
	/** Creates a set with no frames added. */
	TNetworkFrameSet() noexcept = default;

	/** Virtual destructor via the base; the set owns no frame, so there is nothing else to release. */
	~TNetworkFrameSet() noexcept override = default;

	// TEngineHost holds this set by INetworkFrame&, and each added frame is captured by raw
	// pointer; relocating the set would dangle both references, so copy and move are deleted
	// (the same fixed-identity rule TMessageRouter and TMessageChannelBinding already follow).
	TNetworkFrameSet(const TNetworkFrameSet&) = delete;
	TNetworkFrameSet& operator=(const TNetworkFrameSet&) = delete;
	TNetworkFrameSet(TNetworkFrameSet&&) = delete;
	TNetworkFrameSet& operator=(TNetworkFrameSet&&) = delete;

	/**
	 * Adds one caller-owned frame; the order of Add calls becomes the TickDispatch order.
	 * Rejects a frame already present as Duplicate (matched by pointer identity) and a full
	 * set as CapacityExceeded, leaving the set unchanged in both cases.
	 */
	EEngineResult Add(INetworkFrame& Frame) noexcept
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			if (Frames[Index] == &Frame)
			{
				return EEngineResult::Duplicate;
			}
		}
		if (Count == MaxFrames)
		{
			return EEngineResult::CapacityExceeded;
		}

		Frames[Count] = &Frame;
		++Count;
		return EEngineResult::Success;
	}

	/** Dispatches every added frame's inbound step in add-order. An empty set does nothing. */
	void TickDispatch(const TimePointMilliseconds NowMilliseconds) noexcept override
	{
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			Frames[Index]->TickDispatch(NowMilliseconds);
		}
	}

	/** Flushes every added frame's outbound step in reverse add-order. An empty set does nothing. */
	void TickFlush(const TimePointMilliseconds NowMilliseconds) noexcept override
	{
		for (std::size_t Index = Count; Index > 0; --Index)
		{
			Frames[Index - 1]->TickFlush(NowMilliseconds);
		}
	}

	/** Reports how many frames have been added so far. */
	std::size_t FrameCount() const noexcept { return Count; }

private:
	/** Caller-owned frames in add-order; never owned here. */
	INetworkFrame* Frames[MaxFrames == 0 ? 1 : MaxFrames]{};

	/** Number of occupied entries at the front of Frames. */
	std::size_t Count{0};
};

} // namespace MicroWorld
