#pragma once

#include <MicroWorld/Core/PlaySystem.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Transport/TransportHost.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace MicroWorld::Networking
{

/**
 * Motivation: Lets one channel choose whether it resends unacknowledged messages or sends best-effort once, so the
 *   caller picks per-channel delivery semantics without a separate type per shape.
 * Responsibilities: Distinguish the send-once and resend-until-acknowledged shapes that AddChannel accepts.
 * Example:
 *   Net.AddChannel(Device, ChannelId, EChannelReliability::Guaranteed);
 */
enum class EChannelReliability : std::uint8_t
{
	BestEffort, ///< Motivation: Send once; packet loss drops the message. The channel binding forwards directly to the router.

	Guaranteed, ///< Motivation: Resend unacknowledged messages until acknowledged or the retry budget is exhausted. A reliable channel wraps the
				///< binding.
};

/**
 * Motivation: Carries the fixed capacities a TNetworking sizes itself with so the system never allocates at runtime: device, peer,
 *   and packet sizing per host, shared router sizing, reliable-channel retry sizing, and the total channel count.
 * Responsibilities: Name every compile-time capacity exactly once and keep each within the bound its owning type requires.
 * Example:
 *   using FTraits = FDefaultNetworkingTraits;
 *   TNetworking<FTraits> Net;
 */
struct FDefaultNetworkingTraits
{
	/** Motivation: Maximum devices (one TTransportHost each) the system accepts through AddDevice. */
	static constexpr std::size_t MaxDevices = 2;

	/** Motivation: Peer slots each TTransportHost owns; reserved indices 0xFE/0xFF keep this below 0xFE. */
	static constexpr std::size_t MaxPeers = 2;

	/** Motivation: Packet byte budget each TTransportHost owns; must fit the largest control frame. */
	static constexpr std::size_t MaxPacketBytes = 256;

	/** Motivation: Maximum handlers the shared router accepts. */
	static constexpr std::size_t MaxRouterHandlers = 16;

	/** Motivation: Maximum messages the shared router queues inbound or outbound. */
	static constexpr std::size_t MaxRouterQueuedMessages = 8;

	/** Motivation: Maximum encoded message bytes the shared router stores per queued message. */
	static constexpr std::size_t MaxRouterMessageBytes = 96;

	/** Motivation: Maximum channels the shared router registers (one per AddChannel call). */
	static constexpr std::size_t MaxRouterChannels = 4;

	/** Motivation: Maximum pending unacknowledged messages one guaranteed channel retries. */
	static constexpr std::size_t MaxReliablePendingMessages = 8;

	/** Motivation: Retry interval a guaranteed channel paces resends at. */
	static constexpr Core::DurationMilliseconds ReliableRetryIntervalMilliseconds = 200;

	/** Motivation: Total send attempts (initial plus retries) a guaranteed channel makes before abandoning one message. */
	static constexpr std::uint8_t MaxReliableSendAttempts = 8;

	/** Motivation: Maximum channels the system accepts through AddChannel across every reliability. */
	static constexpr std::size_t MaxChannels = 4;
};

/**
 * Motivation: Gives one added device a generation-checked identity so a stale handle cannot address a later slot occupant.
 * Responsibilities: Hold the slot index and generation and report candidate validity before the owning system checks the generation.
 * Example:
 *   FDeviceHandle Device = Net.AddDevice(Radio, Mode, Config);
 *   if (Device.IsValid()) { Use(Device); }
 */
struct FDeviceHandle
{
	/** Motivation: Reserved index that names no device; the default handle is deliberately invalid. */
	static constexpr std::uint8_t InvalidIndex = 0xFF;

	/** Motivation: Device slot index, or InvalidIndex. */
	std::uint8_t Index{InvalidIndex};

	/** Motivation: Slot generation at issue; a released slot advances it before a future reuse. */
	std::uint8_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning system.
	 * Responsibilities: Report whether the index names a candidate slot, without asserting the generation.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/**
 * Motivation: Gives one added channel a generation-checked identity that mirrors FDeviceHandle, so a stale channel identity
 *   cannot address a future slot occupant.
 * Responsibilities: Hold the slot index and generation and report candidate validity before the owning system checks the generation.
 * Example:
 *   FChannelHandle Channel = Net.AddChannel(Device, Id, EChannelReliability::Guaranteed);
 *   if (Channel.IsValid()) { Use(Channel); }
 */
struct FChannelHandle
{
	/** Motivation: Reserved index that names no channel; the default handle is deliberately invalid. */
	static constexpr std::uint8_t InvalidIndex = 0xFF;

	/** Motivation: Channel slot index, or InvalidIndex. */
	std::uint8_t Index{InvalidIndex};

	/** Motivation: Slot generation at issue; a released slot advances it before a future reuse. */
	std::uint8_t Generation{0};

	/**
	 * Motivation: Lets a caller reject a default or stale value before consulting its owning system.
	 * Responsibilities: Report whether the index names a candidate slot, without asserting the generation.
	 */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/**
 * Motivation: Turns devices into a working networked engine in one object, owning fixed-capacity hosts, bindings, reliable wrappers, and a
 *   shared router so a composition root wires transport to messaging without adapter objects.
 * Responsibilities: Accept devices and channels during composition, start and stop hosts on the engine lifecycle, and pump frames in the fixed
 *   transport -> reliable -> router order without hidden clocks or heap growth.
 * Example:
 *   TNetworking<> Net;
 *   FDeviceHandle Device = Net.AddDevice(Radio, ENetworkMode::Standalone, HostConfig);
 *   Net.AddChannel(Device, ChannelId, EChannelReliability::BestEffort);
 *   Net.BeginPlay(NowMs);
 *   Net.PreAdvance(NowMs);
 *   Net.PostAdvance(NowMs);
 */
template<typename TTraits = FDefaultNetworkingTraits>
class TNetworking final : public Core::IPlaySystem
{
	/**
	 * Motivation: Keeps every valid device index below the reserved invalid sentinel.
	 * Responsibilities: Reject a traits capacity that would let a live device collide with InvalidIndex.
	 */
	static_assert(TTraits::MaxDevices <= FDeviceHandle::InvalidIndex, "TNetworking device capacity must fit below FDeviceHandle::InvalidIndex.");

	/**
	 * Motivation: Keeps every valid channel index below the reserved invalid sentinel.
	 * Responsibilities: Reject a traits capacity that would let a live channel collide with InvalidIndex.
	 */
	static_assert(TTraits::MaxChannels <= FChannelHandle::InvalidIndex, "TNetworking channel capacity must fit below FChannelHandle::InvalidIndex.");

	/**
	 * Motivation: Gives every host at least one peer so TTransportHost's peer table is bounded and nonzero.
	 * Responsibilities: Reject a traits capacity that would leave a host with no peers.
	 */
	static_assert(TTraits::MaxPeers > 0, "TNetworking requires at least one peer per device.");

private:
	/** Motivation: Names the concrete host type every device slot stores. */
	using FTransportHost = ::MicroWorld::Transport::TTransportHost<TTraits::MaxPeers, TTraits::MaxPacketBytes>;

	/** Motivation: Names the binding that connects one host wire channel to the shared router. */
	using FChannelBinding = ::MicroWorld::Messaging::TMessageChannelBinding<FTransportHost>;

	/** Motivation: Names the one shared actor-message router. */
	using FRouter = ::MicroWorld::Messaging::
		TMessageRouter<TTraits::MaxRouterHandlers, TTraits::MaxRouterQueuedMessages, TTraits::MaxRouterMessageBytes, TTraits::MaxRouterChannels>;

	/** Motivation: Names the optional wrapper that retries a guaranteed channel. */
	using FReliableChannel = ::MicroWorld::Messaging::TReliableChannel<TTraits::MaxReliablePendingMessages, TTraits::MaxRouterMessageBytes>;

public:
	/**
	 * Motivation: Lets a composition root construct an empty system before wiring devices and channels.
	 * Responsibilities: Produce a system with no live devices or channels and composition open.
	 */
	TNetworking() noexcept = default;

	/**
	 * Motivation: Keeps host and channel addresses fixed for their bound relationships, so a networking system cannot copy or relocate.
	 * Responsibilities: Reject copy and move construction so in-place ownership stays at one address.
	 */
	TNetworking(const TNetworking&) = delete;

	/**
	 * Motivation: Keeps host and channel addresses fixed for their bound relationships, so a networking system cannot copy or relocate.
	 * Responsibilities: Reject copy assignment so in-place ownership stays at one address.
	 */
	TNetworking& operator=(const TNetworking&) = delete;

	/**
	 * Motivation: Keeps host and channel addresses fixed for their bound relationships, so a networking system cannot copy or relocate.
	 * Responsibilities: Reject move construction so in-place ownership stays at one address.
	 */
	TNetworking(TNetworking&&) = delete;

	/**
	 * Motivation: Keeps host and channel addresses fixed for their bound relationships, so a networking system cannot copy or relocate.
	 * Responsibilities: Reject move assignment so in-place ownership stays at one address.
	 */
	TNetworking& operator=(TNetworking&&) = delete;

	/**
	 * Motivation: Ensures channel bindings are removed before their hosts, preserving each host-handler registration's lifetime boundary.
	 * Responsibilities: Destroy every channel binding then every device host in the correct order.
	 */
	~TNetworking() noexcept override
	{
		for (FChannelSlot& Slot : ChannelSlots)
		{
			ReleaseChannelSlot(Slot);
		}
		for (FDeviceSlot& Slot : DeviceSlots)
		{
			ReleaseDeviceSlot(Slot);
		}
	}

	/**
	 * Motivation: Lets a composition root add one device-backed host and configure its role/session policy before play starts.
	 * Responsibilities: Acquire a device slot, configure the host, and return a generation-checked handle; leave composition untouched on failure.
	 */
	FDeviceHandle AddDevice(
		::MicroWorld::Transport::Device::IDevice& InDevice,
		::MicroWorld::Transport::ENetworkMode InMode,
		const ::MicroWorld::Transport::FTransportHostConfig& InConfig) noexcept
	{
		if (bCompositionClosed)
		{
			return {};
		}

		FDeviceSlot* const Slot = AcquireDeviceSlot(InDevice);
		if (Slot == nullptr)
		{
			return {};
		}
		if (Slot->Host->Configure(InMode, InConfig) != ::MicroWorld::Transport::ETransportResult::Success)
		{
			ReleaseDeviceSlot(*Slot);
			return {};
		}

		Slot->Mode = InMode;
		return FDeviceHandle{static_cast<std::uint8_t>(Slot - DeviceSlots.data()), Slot->Generation};
	}

	/**
	 * Motivation: Lets a composition root add one router channel on a configured device, choosing best-effort or guaranteed delivery.
	 * Responsibilities: Resolve the device, acquire a channel slot, build the binding (and optional reliable wrapper), and register with the router.
	 */
	FChannelHandle AddChannel(
		FDeviceHandle InDevice, ::MicroWorld::Messaging::FMessageChannelId InChannel, EChannelReliability InReliability) noexcept
	{
		if (bCompositionClosed || InChannel == ::MicroWorld::Messaging::LocalChannelId)
		{
			return {};
		}

		FDeviceSlot* const Device = ResolveDevice(InDevice);
		if (Device == nullptr)
		{
			return {};
		}
		FChannelSlot* const Slot = AcquireChannelSlot();
		if (Slot == nullptr)
		{
			return {};
		}

		new (&Slot->BindingStorage)
			FChannelBinding(*Device->Host, static_cast<std::uint8_t>(InChannel), InChannel, GetSendTarget(Device->Mode), Router);
		Slot->Binding = reinterpret_cast<FChannelBinding*>(&Slot->BindingStorage);
		if (!Slot->Binding->IsAttached())
		{
			ReleaseChannelSlot(*Slot);
			return {};
		}

		const ::MicroWorld::Messaging::EMessageResult AddResult = AddChannelToRouter(*Slot, InReliability);
		if (AddResult != ::MicroWorld::Messaging::EMessageResult::Success)
		{
			ReleaseChannelSlot(*Slot);
			return {};
		}

		return FChannelHandle{static_cast<std::uint8_t>(Slot - ChannelSlots.data()), Slot->Generation};
	}

	/**
	 * Motivation: Lets a caller reach the single shared router for direct message sending or handler registration.
	 * Responsibilities: Return the router every added device demultiplexes into by channel id.
	 */
	::MicroWorld::Messaging::IMessageRouter& GetRouter() noexcept { return Router; }

	/**
	 * Motivation: Closes composition and starts each live host so the engine lifecycle owns host start at the canonical play time.
	 * Responsibilities: Mark composition closed and start each live host in device add order.
	 */
	void BeginPlay(Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		bCompositionClosed = true;
		for (FDeviceSlot& Slot : DeviceSlots)
		{
			if (Slot.bLive)
			{
				(void)Slot.Host->Start(InNowMilliseconds);
			}
		}
	}

	/**
	 * Motivation: Stops live hosts after the engine has ended its world, in the reverse order they started.
	 * Responsibilities: Stop each live host in reverse device add order once composition has closed.
	 */
	void EndPlay() noexcept override
	{
		if (!bCompositionClosed)
		{
			return;
		}

		for (std::size_t Index = DeviceSlots.size(); Index > 0; --Index)
		{
			FDeviceSlot& Slot = DeviceSlots[Index - 1];
			if (Slot.bLive)
			{
				Slot.Host->Stop();
			}
		}
	}

	/**
	 * Motivation: Pumps live hosts and the router so inbound frames are received and dispatched before the world advances.
	 * Responsibilities: Pump each live host's receive in device add order, then advance the shared router.
	 */
	void PreAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		if (!bCompositionClosed)
		{
			return;
		}

		for (FDeviceSlot& Slot : DeviceSlots)
		{
			if (Slot.bLive)
			{
				(void)Slot.Host->PumpReceive(InNowMilliseconds);
			}
		}
		Router.PreAdvance(InNowMilliseconds);
	}

	/**
	 * Motivation: Flushes outbound work after the world advances, preserving the router -> reliable -> host send order.
	 * Responsibilities: Advance the router, retry live reliable channels in reverse order, then pump each live host's send in reverse device order.
	 */
	void PostAdvance(Core::TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		if (!bCompositionClosed)
		{
			return;
		}

		Router.PostAdvance(InNowMilliseconds);
		for (std::size_t Index = ChannelSlots.size(); Index > 0; --Index)
		{
			FChannelSlot& Slot = ChannelSlots[Index - 1];
			if (Slot.bLive && Slot.Reliable != nullptr)
			{
				Slot.Reliable->PostAdvance(InNowMilliseconds);
			}
		}
		for (std::size_t Index = DeviceSlots.size(); Index > 0; --Index)
		{
			FDeviceSlot& Slot = DeviceSlots[Index - 1];
			if (Slot.bLive)
			{
				(void)Slot.Host->PumpSend(InNowMilliseconds);
			}
		}
	}

private:
#if defined(_MSC_VER)
	// The byte buffers deliberately carry their owned objects' alignment. MSVC
	// reports that unavoidable layout as C4324 even though it is the invariant
	// that keeps the in-place object addresses valid for their bound relationships.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

	/**
	 * Motivation: Owns one configured host in stable in-place storage so its address stays valid for its bound relationships.
	 * Responsibilities: Hold the host storage, the live flag, the role, and the generation that invalidates prior handles on release.
	 * Example:
	 *   FDeviceSlot Slot;
	 *   Slot.Host = new (&Slot.HostStorage) FTransportHost(Device);
	 */
	struct FDeviceSlot
	{
		/** Motivation: Generation published with this slot's handle; release advances it before reuse. */
		std::uint8_t Generation{1};

		/** Motivation: Distinguishes an occupied slot from raw storage that has no constructed host. */
		bool bLive{false};

		/** Motivation: Configured host role, retained so AddChannel can derive its outbound peer target. */
		::MicroWorld::Transport::ENetworkMode Mode{::MicroWorld::Transport::ENetworkMode::Standalone};

		/** Motivation: Storage for the host, whose address must remain fixed while bindings reference it. */
		alignas(FTransportHost) std::byte HostStorage[sizeof(FTransportHost)]{};

		/** Motivation: Points at the host constructed in HostStorage while the slot is live. */
		FTransportHost* Host{nullptr};
	};

	/**
	 * Motivation: Owns one binding and, only for guaranteed delivery, its reliable wrapper in stable storage.
	 * Responsibilities: Hold the binding and reliable storage, the live flag, both pointers, and the generation that invalidates prior handles.
	 * Example:
	 *   FChannelSlot Slot;
	 *   Slot.Binding = new (&Slot.BindingStorage) FChannelBinding(Host, ...);
	 */
	struct FChannelSlot
	{
		/** Motivation: Generation published with this slot's handle; release advances it before reuse. */
		std::uint8_t Generation{1};

		/** Motivation: Distinguishes an occupied slot from raw storage that has no constructed binding. */
		bool bLive{false};

		/** Motivation: Storage for the binding that registers exactly one inbound host handler. */
		alignas(FChannelBinding) std::byte BindingStorage[sizeof(FChannelBinding)]{};

		/** Motivation: Storage for the optional reliable wrapper. */
		alignas(FReliableChannel) std::byte ReliableStorage[sizeof(FReliableChannel)]{};

		/** Motivation: Points at the binding constructed in BindingStorage while the slot is live. */
		FChannelBinding* Binding{nullptr};

		/** Motivation: Points at the wrapper constructed in ReliableStorage, or null for best-effort channels. */
		FReliableChannel* Reliable{nullptr};
	};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

	/**
	 * Motivation: Finds the first free device slot and constructs a host in it.
	 * Responsibilities: Construct the host in stable storage and mark the slot live, or return null when capacity is exhausted.
	 */
	FDeviceSlot* AcquireDeviceSlot(::MicroWorld::Transport::Device::IDevice& InDevice) noexcept
	{
		for (FDeviceSlot& Slot : DeviceSlots)
		{
			if (!Slot.bLive)
			{
				Slot.Host = new (&Slot.HostStorage) FTransportHost(InDevice);
				Slot.bLive = true;
				return &Slot;
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Returns a device slot to reusable storage while invalidating any outstanding handle.
	 * Responsibilities: Destroy the host, clear the live flag, and advance the generation so stale handles cannot reuse the slot.
	 */
	void ReleaseDeviceSlot(FDeviceSlot& InSlot) noexcept
	{
		if (!InSlot.bLive)
		{
			return;
		}
		if (InSlot.Host != nullptr)
		{
			InSlot.Host->~FTransportHost();
			InSlot.Host = nullptr;
		}
		InSlot.bLive = false;
		++InSlot.Generation;
	}

	/**
	 * Motivation: Reserves the first unused channel slot for a new binding.
	 * Responsibilities: Mark the first free slot live and return it, or return null when channel capacity is exhausted.
	 */
	FChannelSlot* AcquireChannelSlot() noexcept
	{
		for (FChannelSlot& Slot : ChannelSlots)
		{
			if (!Slot.bLive)
			{
				Slot.bLive = true;
				return &Slot;
			}
		}
		return nullptr;
	}

	/**
	 * Motivation: Returns a channel slot to reusable storage, destroying the reliable wrapper before its binding.
	 * Responsibilities: Destroy the reliable wrapper then the binding, clear the live flag, and advance the generation.
	 */
	void ReleaseChannelSlot(FChannelSlot& InSlot) noexcept
	{
		if (!InSlot.bLive)
		{
			return;
		}
		if (InSlot.Reliable != nullptr)
		{
			InSlot.Reliable->~FReliableChannel();
			InSlot.Reliable = nullptr;
		}
		if (InSlot.Binding != nullptr)
		{
			InSlot.Binding->~FChannelBinding();
			InSlot.Binding = nullptr;
		}
		InSlot.bLive = false;
		++InSlot.Generation;
	}

	/**
	 * Motivation: Guards AddChannel against a forged or stale device identity before it mutates channel storage.
	 * Responsibilities: Return the live slot whose generation matches the handle, or null.
	 */
	FDeviceSlot* ResolveDevice(FDeviceHandle InHandle) noexcept
	{
		if (!InHandle.IsValid() || InHandle.Index >= DeviceSlots.size())
		{
			return nullptr;
		}

		FDeviceSlot& Slot = DeviceSlots[InHandle.Index];
		return (Slot.bLive && Slot.Generation == InHandle.Generation) ? &Slot : nullptr;
	}

	/**
	 * Motivation: Maps the configured role to the only valid outbound target for its bindings.
	 * Responsibilities: Return Server for a client role and AllPeers otherwise.
	 */
	static constexpr ::MicroWorld::Messaging::EChannelSendTarget GetSendTarget(::MicroWorld::Transport::ENetworkMode InMode) noexcept
	{
		return InMode == ::MicroWorld::Transport::ENetworkMode::Client ? ::MicroWorld::Messaging::EChannelSendTarget::Server
																	   : ::MicroWorld::Messaging::EChannelSendTarget::AllPeers;
	}

	/**
	 * Motivation: Registers a channel with the router, wrapping it in a reliable channel only when guaranteed delivery is requested.
	 * Responsibilities: Register the binding directly for best-effort, or construct and wrap a reliable channel before registering.
	 */
	::MicroWorld::Messaging::EMessageResult AddChannelToRouter(FChannelSlot& InSlot, EChannelReliability InReliability) noexcept
	{
		if (InReliability == EChannelReliability::BestEffort)
		{
			return Router.AddChannel(*InSlot.Binding);
		}

		const ::MicroWorld::Messaging::FReliableChannelConfig Config{TTraits::ReliableRetryIntervalMilliseconds, TTraits::MaxReliableSendAttempts};
		new (&InSlot.ReliableStorage) FReliableChannel(Router, Config);
		InSlot.Reliable = reinterpret_cast<FReliableChannel*>(&InSlot.ReliableStorage);
		InSlot.Reliable->SetInnerChannel(*InSlot.Binding);
		return Router.AddChannel(*InSlot.Reliable);
	}

	/** Motivation: Shared router every configured channel registers with and every device delivers into. */
	FRouter Router;

	/** Motivation: Fixed slots that own all configured device hosts. */
	std::array<FDeviceSlot, TTraits::MaxDevices> DeviceSlots{};

	/** Motivation: Fixed slots that own all configured bindings and optional reliable wrappers. */
	std::array<FChannelSlot, TTraits::MaxChannels> ChannelSlots{};

	/** Motivation: Becomes true at the first BeginPlay so later composition cannot change the direct pumping order. */
	bool bCompositionClosed{false};
};

} // namespace MicroWorld::Networking
