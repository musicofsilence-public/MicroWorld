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

using namespace ::MicroWorld::Core;

/** Selects whether a TNetworking channel resends unacknowledged messages or sends best-effort once. */
enum class EChannelReliability : std::uint8_t
{
	/** Send once; packet loss drops the message. The channel binding forwards directly to the router. */
	BestEffort,

	/** Resend unacknowledged messages until acknowledged or the retry budget is exhausted. A reliable channel wraps the binding. */
	Guaranteed,
};

/**
 * Carries the fixed capacities a TNetworking sizes itself with: how many devices it accepts,
 * the peer and packet sizing every TTransportHost shares, the shared router sizing, the reliable-channel
 * retry sizing, and how many channels one system accepts in total. Every member is a compile-time
 * capacity so the system never allocates.
 */
struct FDefaultNetworkingTraits
{
	/** Maximum devices (one TTransportHost each) the system accepts through AddDevice. */
	static constexpr std::size_t MaxDevices = 2;

	/** Peer slots each TTransportHost owns; reserved indices 0xFE/0xFF keep this below 0xFE. */
	static constexpr std::size_t MaxPeers = 2;

	/** Packet byte budget each TTransportHost owns; must fit the largest control frame. */
	static constexpr std::size_t MaxPacketBytes = 256;

	/** Maximum handlers the shared router accepts. */
	static constexpr std::size_t MaxRouterHandlers = 16;

	/** Maximum messages the shared router queues inbound or outbound. */
	static constexpr std::size_t MaxRouterQueuedMessages = 8;

	/** Maximum encoded message bytes the shared router stores per queued message. */
	static constexpr std::size_t MaxRouterMessageBytes = 96;

	/** Maximum channels the shared router registers (one per AddChannel call). */
	static constexpr std::size_t MaxRouterChannels = 4;

	/** Maximum pending unacknowledged messages one guaranteed channel retries. */
	static constexpr std::size_t MaxReliablePendingMessages = 8;

	/** Retry interval a guaranteed channel paces resends at. */
	static constexpr DurationMilliseconds ReliableRetryIntervalMilliseconds = 200;

	/** Total send attempts (initial plus retries) a guaranteed channel makes before abandoning one message. */
	static constexpr std::uint8_t MaxReliableSendAttempts = 8;

	/** Maximum channels the system accepts through AddChannel across every reliability. */
	static constexpr std::size_t MaxChannels = 4;
};

/**
 * Generation-checked identity of one device added to a TNetworking. The index addresses the
 * fixed slot and the generation prevents a stale identity from addressing a later occupant.
 */
struct FDeviceHandle
{
	/** Reserved index that names no device; the default handle is deliberately invalid. */
	static constexpr std::uint8_t InvalidIndex = 0xFF;

	/** Device slot index, or InvalidIndex. */
	std::uint8_t Index{InvalidIndex};

	/** Slot generation at issue; a released slot advances it before a future reuse. */
	std::uint8_t Generation{0};

	/** Reports whether this value names a candidate device slot before its owning system validates the generation. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/**
 * Generation-checked identity of one channel added to a TNetworking. It mirrors FDeviceHandle
 * so a stale channel identity cannot address a future slot occupant.
 */
struct FChannelHandle
{
	/** Reserved index that names no channel; the default handle is deliberately invalid. */
	static constexpr std::uint8_t InvalidIndex = 0xFF;

	/** Channel slot index, or InvalidIndex. */
	std::uint8_t Index{InvalidIndex};

	/** Slot generation at issue; a released slot advances it before a future reuse. */
	std::uint8_t Generation{0};

	/** Reports whether this value names a candidate channel slot before its owning system validates the generation. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/**
 * One object that turns devices into a working networked engine, owning fixed-capacity hosts,
 * bindings, reliable wrappers, and a shared router. AddDevice and AddChannel compose it;
 * BeginPlay closes composition and starts hosts at the engine's canonical time, while direct
 * frame pumping preserves the transport -> reliable -> router ordering without adapter objects.
 */
template<typename TTraits = FDefaultNetworkingTraits>
class TNetworking final : public IPlaySystem
{
	/** Prevents a valid handle index from colliding with the reserved invalid sentinel. */
	static_assert(TTraits::MaxDevices <= FDeviceHandle::InvalidIndex, "TNetworking device capacity must fit below FDeviceHandle::InvalidIndex.");

	/** Prevents a valid handle index from colliding with the reserved invalid sentinel. */
	static_assert(TTraits::MaxChannels <= FChannelHandle::InvalidIndex, "TNetworking channel capacity must fit below FChannelHandle::InvalidIndex.");

	/** TTransportHost itself requires a bounded, nonzero peer table. */
	static_assert(TTraits::MaxPeers > 0, "TNetworking requires at least one peer per device.");

private:
	/** Names the concrete host type every device slot stores. */
	using FTransportHost = ::MicroWorld::Transport::TTransportHost<TTraits::MaxPeers, TTraits::MaxPacketBytes>;

	/** Names the binding that connects one host wire channel to the shared router. */
	using FChannelBinding = ::MicroWorld::Messaging::TMessageChannelBinding<FTransportHost>;

	/** Names the one shared actor-message router. */
	using FRouter = ::MicroWorld::Messaging::
		TMessageRouter<TTraits::MaxRouterHandlers, TTraits::MaxRouterQueuedMessages, TTraits::MaxRouterMessageBytes, TTraits::MaxRouterChannels>;

	/** Names the optional wrapper that retries a guaranteed channel. */
	using FReliableChannel = ::MicroWorld::Messaging::TReliableChannel<TTraits::MaxReliablePendingMessages, TTraits::MaxRouterMessageBytes>;

public:
	/** Creates an empty system; callers finish its device and channel composition before engine BeginPlay. */
	TNetworking() noexcept = default;

	/** Stable in-place ownership keeps host and channel addresses valid for their bound relationships, so a networking system cannot copy or
	 * relocate. */
	TNetworking(const TNetworking&) = delete;
	TNetworking& operator=(const TNetworking&) = delete;
	TNetworking(TNetworking&&) = delete;
	TNetworking& operator=(TNetworking&&) = delete;

	/** Removes channel bindings before their hosts, preserving each host-handler registration's lifetime boundary. */
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
	 * Adds one device-backed host and configures its role/session policy. This only performs
	 * TTransportHost::Configure: host start is deferred to BeginPlay's engine lifecycle turn.
	 *
	 * @return A generation-checked device handle, or an invalid handle on closed composition,
	 *         exhausted capacity, or a rejected host configuration.
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
	 * Adds one router channel on a configured device. The wire channel byte is derived directly
	 * from InChannel and the host mode derives whether the binding targets the server or all peers.
	 * A guaranteed wrapper receives its inner binding before the router observes that wrapper.
	 *
	 * @return A generation-checked channel handle, or an invalid handle for an invalid/stale device,
	 *         closed composition, invalid channel id, or any fixed-capacity registration failure.
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

	/** Returns the single shared router that every added device demultiplexes into by channel id. */
	::MicroWorld::Messaging::IMessageRouter& GetRouter() noexcept { return Router; }

	/**
	 * Closes composition before starting each live host in device add order at the engine's
	 * canonical play-start time.
	 */
	void BeginPlay(TimePointMilliseconds InNowMilliseconds) noexcept override
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

	/** Stops live hosts in reverse device add order after the engine has ended its world. */
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

	/** Pumps live hosts in device add order, then dispatches the shared router. */
	void PreAdvance(TimePointMilliseconds InNowMilliseconds) noexcept override
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

	/** Flushes the router, retries live reliable channels in reverse order, then pumps hosts in reverse device order. */
	void PostAdvance(TimePointMilliseconds InNowMilliseconds) noexcept override
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

	/** Owns one configured host in stable in-place storage. */
	struct FDeviceSlot
	{
		/** Generation published with this slot's handle; release advances it before reuse. */
		std::uint8_t Generation{1};

		/** Distinguishes an occupied slot from raw storage that has no constructed host. */
		bool bLive{false};

		/** Configured host role, retained so AddChannel can derive its outbound peer target. */
		::MicroWorld::Transport::ENetworkMode Mode{::MicroWorld::Transport::ENetworkMode::Standalone};

		/** Storage for the host, whose address must remain fixed while bindings reference it. */
		alignas(FTransportHost) std::byte HostStorage[sizeof(FTransportHost)]{};

		/** Points at the host constructed in HostStorage while the slot is live. */
		FTransportHost* Host{nullptr};
	};

	/** Owns one binding and, only for guaranteed delivery, its reliable wrapper in stable storage. */
	struct FChannelSlot
	{
		/** Generation published with this slot's handle; release advances it before reuse. */
		std::uint8_t Generation{1};

		/** Distinguishes an occupied slot from raw storage that has no constructed binding. */
		bool bLive{false};

		/** Storage for the binding that registers exactly one inbound host handler. */
		alignas(FChannelBinding) std::byte BindingStorage[sizeof(FChannelBinding)]{};

		/** Storage for the optional reliable wrapper. */
		alignas(FReliableChannel) std::byte ReliableStorage[sizeof(FReliableChannel)]{};

		/** Points at the binding constructed in BindingStorage while the slot is live. */
		FChannelBinding* Binding{nullptr};

		/** Points at the wrapper constructed in ReliableStorage, or null for best-effort channels. */
		FReliableChannel* Reliable{nullptr};
	};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

	/** Constructs a host in the first free slot, or returns null when device capacity is exhausted. */
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

	/** Destroys a device host and advances the generation that invalidates any old handle. */
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

	/** Reserves the first unused channel slot, or returns null when the channel capacity is exhausted. */
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

	/** Destroys a reliable wrapper before its binding so the binding can remove its host handler safely. */
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

	/** Resolves a generation-checked device handle to its live slot, rejecting forged or stale identities. */
	FDeviceSlot* ResolveDevice(FDeviceHandle InHandle) noexcept
	{
		if (!InHandle.IsValid() || InHandle.Index >= DeviceSlots.size())
		{
			return nullptr;
		}

		FDeviceSlot& Slot = DeviceSlots[InHandle.Index];
		return (Slot.bLive && Slot.Generation == InHandle.Generation) ? &Slot : nullptr;
	}

	/** Maps the configured role to the only valid outbound target for its bindings. */
	static constexpr ::MicroWorld::Messaging::EChannelSendTarget GetSendTarget(::MicroWorld::Transport::ENetworkMode InMode) noexcept
	{
		return InMode == ::MicroWorld::Transport::ENetworkMode::Client ? ::MicroWorld::Messaging::EChannelSendTarget::Server
																	   : ::MicroWorld::Messaging::EChannelSendTarget::AllPeers;
	}

	/** Creates a guaranteed wrapper before router registration, or registers a best-effort binding directly. */
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

	/** Shared router every configured channel registers with and every device delivers into. */
	FRouter Router;

	/** Fixed slots that own all configured device hosts. */
	std::array<FDeviceSlot, TTraits::MaxDevices> DeviceSlots{};

	/** Fixed slots that own all configured bindings and optional reliable wrappers. */
	std::array<FChannelSlot, TTraits::MaxChannels> ChannelSlots{};

	/** Becomes true at the first BeginPlay so later composition cannot change the direct pumping order. */
	bool bCompositionClosed{false};
};

} // namespace MicroWorld::Networking
