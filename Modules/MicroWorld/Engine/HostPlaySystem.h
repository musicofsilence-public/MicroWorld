#pragma once

#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Core/Time.h>

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

} // namespace MicroWorld::Engine
