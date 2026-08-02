#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Core/PlaySystem.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Core
{

struct FDeviceAddress;
struct FReceiveResult;

/**
 * Motivation: Bounds one non-blocking addressed packet transport behind one reference-held interface so Messaging can
 *   reach a wire without knowing the medium.
 * Responsibilities: Accept or receive at most one complete packet per call; own no scheduler, clock, thread, retry
 *   policy, or knowledge of byte meaning; and report acceptance rather than delivery, whose acknowledgement is higher-level.
 * Example:
 *   ITransportDevice& Device = GetDevice();
 *   Device.TrySend(MakeBroadcastAddress(), Packet);
 */
class ITransportDevice : public IPlaySystem
{
public:
	/**
	 * Motivation: Sends one complete packet transactionally so a non-success result never leaves a partial packet on the wire.
	 * Responsibilities: Never block; return Success only when the whole span was accepted; return Full for missing capacity;
	 *   return Invalid for a null span with nonzero length, an oversize packet, or an unroutable address; reject packets larger
	 *   than MaxPacketBytes without splitting; and treat an empty address as the medium default route or return Invalid when it
	 *   cannot be reached, while point-to-point media ignore the address.
	 */
	virtual ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept = 0;

	/**
	 * Motivation: Receives at most one packet transactionally so a caller never confuses a failed receive with a short read.
	 * Responsibilities: Never block; on Full, Invalid, or Unavailable leave OutFrom, OutResult.BytesReceived, and destination
	 *   bytes unchanged; write all three only on Success; write into the caller-owned destination buffer; and retain none of it.
	 */
	virtual ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept = 0;

	/**
	 * Motivation: Lets a caller bound a send to the transport's accepted packet size.
	 * Responsibilities: Report the largest packet, in bytes, the device accepts in one unsplit send.
	 */
	virtual std::size_t MaxPacketBytes() const noexcept = 0;

	/**
	 * Motivation: Avoids ceremony for a transport turn no device currently needs, while keeping PreAdvance inherited as a
	 *   required declaration: a staged device that omits its pump still accepts TrySend and silently transmits nothing.
	 * Responsibilities: Perform no post-advance work by default; concrete devices must still explicitly state PreAdvance,
	 *   including an empty body when they transmit synchronously within TrySend and stage nothing.
	 */
	void PostAdvance(TimePointMilliseconds) noexcept override {}

	/**
	 * Motivation: Gives every concrete device one stable virtual destructor anchored out of line.
	 * Responsibilities: Release no interface-owned resource and allow destruction through the interface base.
	 */
	virtual ~ITransportDevice() noexcept;

	/**
	 * Motivation: Prevents slicing through the interface since devices are held by reference.
	 * Responsibilities: Reject copy construction so an ITransportDevice value is never copied by its base.
	 */
	ITransportDevice(const ITransportDevice&) = delete;

	/**
	 * Motivation: Prevents slicing through the interface since devices are held by reference.
	 * Responsibilities: Reject copy assignment so an ITransportDevice value is never copied by its base.
	 */
	ITransportDevice& operator=(const ITransportDevice&) = delete;

protected:
	/**
	 * Motivation: Lets concrete devices construct without exposing the interface itself as instantiable.
	 * Responsibilities: Provide a protected default constructor with no side effects.
	 */
	ITransportDevice() noexcept = default;
};

} // namespace MicroWorld::Core
