#pragma once

#include <MicroWorld/Core/IO/TransportDevice.h>

namespace MicroWorld::Transport::Device
{

/**
 * Core owns this type now; this declaration exists only so existing Transport and Platform code keeps compiling.
 * It disappears with the rest of the moved Transport headers.
 *
 * Motivation: Preserves the former Transport receive-result name while callers complete the move to Core ownership.
 * Responsibilities: Alias Core's receive result without declaring another byte-count carrier or behavior.
 */
using FReceiveResult = Core::FReceiveResult;

/**
 * Motivation: Extends Core's non-blocking transport contract with the existing staged-transmit pump so Transport and Platform
 *   devices remain usable as Core play systems without duplicating packet operations.
 * Responsibilities: Inherit Core's packet transport and lifecycle contract, map the pre-advance turn to one bounded transmit
 *   pump, and own no scheduler, clock, thread, retry policy, peer identity, session, or protocol behavior.
 * Example:
 *   IDevice& Device = GetDevice();
 *   if (Device.TrySend(To, Packet) == ETransportResult::Success) { Sent(); }
 */
class IDevice : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Lets staged devices advance one bounded unit of pending outbound work so a packet accepted before physical
	 *   transmission can drain across pumps.
	 * Responsibilities: Default to a no-op so autonomous devices keep the send/receive-only contract, and let staged devices
	 *   override it to make bounded progress.
	 */
	virtual void AdvanceTransmit() noexcept {}

	/**
	 * Motivation: Maps the existing staged-transmit pump to Core's pre-advance turn because both represent the device's one
	 *   bounded per-frame opportunity to progress queued physical transmission.
	 * Responsibilities: Advance at most one bounded unit of outbound work by forwarding to AdvanceTransmit.
	 */
	void PreAdvance(Core::TimePointMilliseconds) noexcept override { AdvanceTransmit(); }

	/**
	 * Motivation: Satisfies Core's post-advance turn without imposing a second pump on devices whose existing outbound turn
	 *   is already mapped to pre-advance.
	 * Responsibilities: Perform no work by default so existing device implementations keep their current frame behavior.
	 */
	void PostAdvance(Core::TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Gives every concrete device one stable virtual destructor anchored out of line.
	 * Responsibilities: Release no interface-owned resource and allow destruction through the interface base.
	 */
	virtual ~IDevice() noexcept;
};

} // namespace MicroWorld::Transport::Device
