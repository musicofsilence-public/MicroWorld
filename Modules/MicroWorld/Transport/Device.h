#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>

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
 * Motivation: Bounds one non-blocking addressed byte transport behind one reference-held interface so a caller can poll
 *   without blocking and distinguish transient unavailability from permanent rejection.
 * Responsibilities: Perform at most one transport operation per call, return an explicit ETransportResult for it, and own no
 *   scheduler, clock, thread, retry policy, peer identity, session, or protocol behavior.
 * Example:
 *   IDevice& Device = GetDevice();
 *   if (Device.TrySend(To, Packet) == ETransportResult::Success) { Sent(); }
 */
class IDevice
{
public:
	/**
	 * Motivation: Sends one complete packet transactionally so a non-success result never leaves a partial packet on the wire.
	 * Responsibilities: Return Success only when the whole span was accepted, Full for missing capacity, Invalid for a null span
	 *   with nonzero length, an oversize packet, or an unroutable address, and leave transport state unchanged otherwise.
	 */
	virtual ETransportResult TrySend(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept = 0;

	/**
	 * Motivation: Receives at most one packet transactionally so a caller never confuses a failed receive with a short read.
	 * Responsibilities: On Full, Invalid, or Unavailable leave the destination, OutResult.BytesReceived, and OutFrom unchanged;
	 *   write the head bytes, byte count, and sender address only on Success.
	 */
	virtual ETransportResult TryReceive(
		::MicroWorld::Transport::Address::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept = 0;

	/**
	 * Motivation: Lets staged devices advance one bounded unit of pending outbound work so a packet accepted before physical
	 *   transmission can drain across pumps.
	 * Responsibilities: Default to a no-op so autonomous devices keep the send/receive-only contract, and let staged devices
	 *   override it to make bounded progress.
	 */
	virtual void AdvanceTransmit() noexcept {}

	/**
	 * Motivation: Lets a caller bound a send to the transport's accepted packet size.
	 * Responsibilities: Report the largest packet, in bytes, the transport accepts on a single send.
	 */
	virtual std::size_t MaxPacketBytes() const noexcept = 0;

	/**
	 * Motivation: Gives every concrete device one stable virtual destructor anchored out of line.
	 * Responsibilities: Release no interface-owned resource and allow polymorphic destruction.
	 */
	virtual ~IDevice() noexcept;

	/**
	 * Motivation: Prevents slicing through the interface since devices are held by reference.
	 * Responsibilities: Reject copy construction so an IDevice value is never copied by its base.
	 */
	IDevice(const IDevice&) = delete;

	/**
	 * Motivation: Prevents slicing through the interface since devices are held by reference.
	 * Responsibilities: Reject copy assignment so an IDevice value is never copied by its base.
	 */
	IDevice& operator=(const IDevice&) = delete;

protected:
	/**
	 * Motivation: Lets concrete devices construct without exposing the interface itself as instantiable.
	 * Responsibilities: Provide a protected default constructor with no side effects.
	 */
	IDevice() noexcept = default;
};

} // namespace MicroWorld::Transport::Device
