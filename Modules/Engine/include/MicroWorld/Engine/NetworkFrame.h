#pragma once

#include <MicroWorld/Time.h>

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

} // namespace MicroWorld
