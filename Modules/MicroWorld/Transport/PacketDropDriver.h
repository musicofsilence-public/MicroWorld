#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <cstddef>
#include <cstdint>

namespace MicroWorld
{

/**
 * Wraps another driver and silently drops every Nth outgoing send.
 * A dropped send returns `Success` without touching the inner driver or inspecting the packet,
 * modeling a packet that left the wire and was lost; receives and `MaxPacketBytes` always pass through.
 */
class FPacketDropDriver final : public IDevice
{
public:
	/** Binds the driver to wrap and the drop interval; `DropEveryNthSend == 0` disables dropping. */
	FPacketDropDriver(IDevice& InInnerDriver, std::uint32_t InDropEveryNthSend) noexcept;

	/** Deleted: this driver holds `InnerDriver` by reference and is itself held by reference, so copying would risk dangling. */
	FPacketDropDriver(const FPacketDropDriver&) = delete;

	/** Deleted: this driver holds `InnerDriver` by reference and is itself held by reference, so copying would risk dangling. */
	FPacketDropDriver& operator=(const FPacketDropDriver&) = delete;

	/** Deleted: this driver holds `InnerDriver` by reference and is itself held by reference, so relocating it would risk dangling. */
	FPacketDropDriver(FPacketDropDriver&&) = delete;

	/** Deleted: this driver holds `InnerDriver` by reference and is itself held by reference, so relocating it would risk dangling. */
	FPacketDropDriver& operator=(FPacketDropDriver&&) = delete;

	/** Anchors the vtable in one translation unit, matching `IDevice`'s out-of-line destructor rule. */
	~FPacketDropDriver() noexcept override;

	/**
	 * Counts this call and, on every `DropEveryNthSend`-th call, drops the packet by returning
	 * `Success` without forwarding it; every other call forwards verbatim to the inner driver.
	 */
	ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override;

	/** Forwards verbatim to the inner driver; receives are never counted or dropped. */
	ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override;

	/** Forwards bounded physical transmit progress so wrapped staged drivers cannot stall behind loss injection. */
	void AdvanceTransmit() noexcept override { InnerDriver.AdvanceTransmit(); }

	/** Forwards verbatim to the inner driver. */
	std::size_t MaxPacketBytes() const noexcept override;

	/** Reports how many sends this driver has dropped so far. */
	std::uint32_t DroppedSendCount() const noexcept;

private:
	/** The wrapped driver every non-dropped send and every receive forwards to. */
	IDevice& InnerDriver;

	/** Every `DropEveryNthSend`-th send is dropped; zero disables dropping entirely. */
	const std::uint32_t DropEveryNthSend;

	/** Counts total `TrySend` calls; wraps after 2^32 sends, accepted for a test/demo loss injector. */
	std::uint32_t SendCallCount{0};

	/** Counts dropped sends; wraps after 2^32 sends, accepted for a test/demo loss injector. */
	std::uint32_t DroppedSendTotal{0};
};

} // namespace MicroWorld
