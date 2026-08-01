#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld::Transport
{

/**
 * Motivation: Wraps another device to inject deterministic packet loss so a host test can exercise retransmit and reliability paths.
 * Responsibilities: Drop every Nth outgoing send by returning Success without touching the inner device or inspecting the
 *   packet (modeling a packet that left the wire and was lost), and pass receives and MaxPacketBytes through unchanged.
 * Example:
 *   FPacketDropDevice Lossy(Inner, 3);
 *   Lossy.TrySend(To, Packet);
 *   if (Lossy.DroppedSendCount() > 0) { Retried(); }
 */
class FPacketDropDevice final : public Core::ITransportDevice
{
public:
	/**
	 * Motivation: Binds the wrapped device and the drop interval at construction.
	 * Responsibilities: Store the inner device reference and the drop interval (zero disables dropping).
	 */
	FPacketDropDevice(Core::ITransportDevice& InInnerDevice, std::uint32_t InDropEveryNthSend) noexcept;

	/**
	 * Motivation: Prevents copying since this device holds InnerDevice by reference and is itself held by reference.
	 * Responsibilities: Reject copy construction so relocation cannot dangle the inner reference.
	 */
	FPacketDropDevice(const FPacketDropDevice&) = delete;

	/**
	 * Motivation: Prevents copying since this device holds InnerDevice by reference and is itself held by reference.
	 * Responsibilities: Reject copy assignment so relocation cannot dangle the inner reference.
	 */
	FPacketDropDevice& operator=(const FPacketDropDevice&) = delete;

	/**
	 * Motivation: Prevents moving since this device holds InnerDevice by reference and is itself held by reference.
	 * Responsibilities: Reject move construction so relocation cannot dangle the inner reference.
	 */
	FPacketDropDevice(FPacketDropDevice&&) = delete;

	/**
	 * Motivation: Prevents moving since this device holds InnerDevice by reference and is itself held by reference.
	 * Responsibilities: Reject move assignment so relocation cannot dangle the inner reference.
	 */
	FPacketDropDevice& operator=(FPacketDropDevice&&) = delete;

	/**
	 * Motivation: Anchors the vtable in one translation unit matching the ITransportDevice out-of-line destructor rule.
	 * Responsibilities: Define one out-of-line virtual destructor without side effects.
	 */
	~FPacketDropDevice() noexcept override;

	/**
	 * Motivation: Injects loss on a fixed cadence so a test drives reliability code predictably.
	 * Responsibilities: Count each call, drop every DropEveryNthSend-th send by returning Success without forwarding, and forward
	 *   every other call verbatim to the inner device.
	 */
	ETransportResult TrySend(
		const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override;

	/**
	 * Motivation: Keeps receive behavior untouched so loss injection affects only the send path.
	 * Responsibilities: Forward verbatim to the inner device without counting or dropping.
	 */
	ETransportResult TryReceive(
		::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
		Core::TSpan<std::uint8_t> InDestination,
		Core::FReceiveResult& OutResult) noexcept override;

	/**
	 * Motivation: Keeps the required pre-advance transport turn working so a wrapped staged device cannot stall behind loss injection.
	 * Responsibilities: Forward bounded physical transmit progress and the caller-supplied time to the inner device.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override { InnerDevice.PreAdvance(InNowMilliseconds); }

	/**
	 * Motivation: Keeps the capacity query consistent with the wrapped device.
	 * Responsibilities: Forward MaxPacketBytes verbatim to the inner device.
	 */
	std::size_t MaxPacketBytes() const noexcept override;

	/**
	 * Motivation: Lets a test assert how much loss was injected.
	 * Responsibilities: Report how many sends this device has dropped so far.
	 */
	std::uint32_t DroppedSendCount() const noexcept;

private:
	/** Motivation: References the wrapped device that every non-dropped send and every receive forwards to. */
	Core::ITransportDevice& InnerDevice;

	/** Motivation: Fixes the drop cadence; zero disables dropping entirely. */
	const std::uint32_t DropEveryNthSend;

	/** Motivation: Counts total TrySend calls; wraps after 2^32 sends, accepted for a test/demo loss injector. */
	std::uint32_t SendCallCount{0};

	/** Motivation: Counts dropped sends; wraps after 2^32 sends, accepted for a test/demo loss injector. */
	std::uint32_t DroppedSendTotal{0};
};

} // namespace MicroWorld::Transport
