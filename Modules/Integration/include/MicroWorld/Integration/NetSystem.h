#pragma once

#include <MicroWorld/Engine/EngineSystem.h>
#include <MicroWorld/Messaging/Message.h>
#include <MicroWorld/Messaging/MessageChannelBinding.h>
#include <MicroWorld/Messaging/MessageRouter.h>
#include <MicroWorld/Messaging/ReliableChannel.h>
#include <MicroWorld/Net/NetHost.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

namespace MicroWorld
{

/** Selects whether a TNetSystem channel resends unacknowledged messages or sends best-effort once. */
enum class EChannelReliability : std::uint8_t
{
	/** Send once; packet loss drops the message. The channel binding forwards directly to the router. */
	BestEffort,

	/** Resend unacknowledged messages until acknowledged or the retry budget is exhausted. A reliable channel wraps the binding. */
	Guaranteed,
};

/**
 * Carries the fixed capacities a TNetSystem sizes itself with: how many net drivers it accepts,
 * the peer and packet sizing every TNetHost shares, the shared router sizing, the reliable-channel
 * retry sizing, and how many channels one system accepts in total. Every member is a compile-time
 * capacity so the system never allocates.
 */
struct FDefaultNetSystemTraits
{
	/** Maximum net drivers (one TNetHost each) the system accepts through AddNetDriver. */
	static constexpr std::size_t MaxNetDrivers = 2;

	/** Peer slots each TNetHost owns; reserved indices 0xFE/0xFF keep this below 0xFE. */
	static constexpr std::size_t MaxPeers = 2;

	/** Packet byte budget each TNetHost owns; must fit the largest control frame. */
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
 * Generation-checked identity of one net driver added to a TNetSystem. The index addresses the
 * fixed slot and the generation prevents a stale identity from addressing a later occupant.
 */
struct FNetDriverHandle
{
	/** Reserved index that names no driver; the default handle is deliberately invalid. */
	static constexpr std::uint8_t InvalidIndex = 0xFF;

	/** Driver slot index, or InvalidIndex. */
	std::uint8_t Index{InvalidIndex};

	/** Slot generation at issue; a released slot advances it before a future reuse. */
	std::uint8_t Generation{0};

	/** Reports whether this value names a candidate driver slot before its owning system validates the generation. */
	constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }
};

/**
 * Generation-checked identity of one channel added to a TNetSystem. It mirrors FNetDriverHandle
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
 * One object that turns drivers into a working networked engine, owning fixed-capacity hosts,
 * bindings, reliable wrappers, and a shared router. AddNetDriver and AddChannel compose it;
 * BeginPlay closes composition and starts hosts at the engine's canonical time, while direct
 * frame pumping preserves the net -> reliable -> router ordering without adapter objects.
 */
template<typename TTraits = FDefaultNetSystemTraits>
class TNetSystem final : public IEngineSystem
{
	/** Prevents a valid handle index from colliding with the reserved invalid sentinel. */
	static_assert(
		TTraits::MaxNetDrivers <= FNetDriverHandle::InvalidIndex, "TNetSystem driver capacity must fit below FNetDriverHandle::InvalidIndex.");

	/** Prevents a valid handle index from colliding with the reserved invalid sentinel. */
	static_assert(TTraits::MaxChannels <= FChannelHandle::InvalidIndex, "TNetSystem channel capacity must fit below FChannelHandle::InvalidIndex.");

	/** TNetHost itself requires a bounded, nonzero peer table. */
	static_assert(TTraits::MaxPeers > 0, "TNetSystem requires at least one peer per driver.");

private:
	/** Names the concrete host type every driver slot stores. */
	using FNetHost = TNetHost<TTraits::MaxPeers, TTraits::MaxPacketBytes>;

	/** Names the binding that connects one host wire channel to the shared router. */
	using FChannelBinding = TMessageChannelBinding<FNetHost>;

	/** Names the one shared actor-message router. */
	using FRouter =
		TMessageRouter<TTraits::MaxRouterHandlers, TTraits::MaxRouterQueuedMessages, TTraits::MaxRouterMessageBytes, TTraits::MaxRouterChannels>;

	/** Names the optional wrapper that retries a guaranteed channel. */
	using FReliableChannel = TReliableChannel<TTraits::MaxReliablePendingMessages, TTraits::MaxRouterMessageBytes>;

public:
	/** Creates an empty system; callers finish its driver and channel composition before engine BeginPlay. */
	TNetSystem() noexcept = default;

	/** Stable in-place ownership keeps host and channel addresses valid for their bound relationships, so a net system cannot copy or relocate. */
	TNetSystem(const TNetSystem&) = delete;
	TNetSystem& operator=(const TNetSystem&) = delete;
	TNetSystem(TNetSystem&&) = delete;
	TNetSystem& operator=(TNetSystem&&) = delete;

	/** Removes channel bindings before their hosts, preserving each host-handler registration's lifetime boundary. */
	~TNetSystem() noexcept override
	{
		for (FChannelSlot& Slot : ChannelSlots)
		{
			ReleaseChannelSlot(Slot);
		}
		for (FDriverSlot& Slot : DriverSlots)
		{
			ReleaseDriverSlot(Slot);
		}
	}

	/**
	 * Adds one driver-backed host and configures its role/session policy. This only performs
	 * TNetHost::Configure: host start is deferred to BeginPlay's engine lifecycle turn.
	 *
	 * @return A generation-checked driver handle, or an invalid handle on closed composition,
	 *         exhausted capacity, or a rejected host configuration.
	 */
	FNetDriverHandle AddNetDriver(INetDriver& InDriver, ENetMode InMode, const FNetHostConfig& InConfig) noexcept
	{
		if (bCompositionClosed)
		{
			return {};
		}

		FDriverSlot* const Slot = AcquireDriverSlot(InDriver);
		if (Slot == nullptr)
		{
			return {};
		}
		if (Slot->Host->Configure(InMode, InConfig) != ENetResult::Success)
		{
			ReleaseDriverSlot(*Slot);
			return {};
		}

		Slot->Mode = InMode;
		return FNetDriverHandle{static_cast<std::uint8_t>(Slot - DriverSlots.data()), Slot->Generation};
	}

	/**
	 * Adds one router channel on a configured driver. The wire channel byte is derived directly
	 * from InChannel and the host mode derives whether the binding targets the server or all peers.
	 * A guaranteed wrapper receives its inner binding before the router observes that wrapper.
	 *
	 * @return A generation-checked channel handle, or an invalid handle for an invalid/stale driver,
	 *         closed composition, invalid channel id, or any fixed-capacity registration failure.
	 */
	FChannelHandle AddChannel(FNetDriverHandle InDriver, FMessageChannelId InChannel, EChannelReliability InReliability) noexcept
	{
		if (bCompositionClosed || InChannel == LocalChannelId)
		{
			return {};
		}

		FDriverSlot* const Driver = ResolveDriver(InDriver);
		if (Driver == nullptr)
		{
			return {};
		}
		FChannelSlot* const Slot = AcquireChannelSlot();
		if (Slot == nullptr)
		{
			return {};
		}

		new (&Slot->BindingStorage)
			FChannelBinding(*Driver->Host, static_cast<std::uint8_t>(InChannel), InChannel, GetSendTarget(Driver->Mode), Router);
		Slot->Binding = reinterpret_cast<FChannelBinding*>(&Slot->BindingStorage);
		if (!Slot->Binding->IsAttached())
		{
			ReleaseChannelSlot(*Slot);
			return {};
		}

		const EMessageResult AddResult = AddChannelToRouter(*Slot, InReliability);
		if (AddResult != EMessageResult::Success)
		{
			ReleaseChannelSlot(*Slot);
			return {};
		}

		return FChannelHandle{static_cast<std::uint8_t>(Slot - ChannelSlots.data()), Slot->Generation};
	}

	/** Returns the single shared router that every added driver demultiplexes into by channel id. */
	IMessageRouter& GetRouter() noexcept { return Router; }

	/**
	 * Closes composition before starting each live host in driver add order at the engine's
	 * canonical play-start time.
	 */
	void BeginPlay(TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		bCompositionClosed = true;
		for (FDriverSlot& Slot : DriverSlots)
		{
			if (Slot.bLive)
			{
				(void)Slot.Host->Start(InNowMilliseconds);
			}
		}
	}

	/** Stops live hosts in reverse driver add order after the engine has ended its world. */
	void EndPlay() noexcept override
	{
		if (!bCompositionClosed)
		{
			return;
		}

		for (std::size_t Index = DriverSlots.size(); Index > 0; --Index)
		{
			FDriverSlot& Slot = DriverSlots[Index - 1];
			if (Slot.bLive)
			{
				Slot.Host->Stop();
			}
		}
	}

	/** Pumps live hosts in driver add order, then dispatches the shared router. */
	void PreAdvance(TimePointMilliseconds InNowMilliseconds) noexcept override
	{
		if (!bCompositionClosed)
		{
			return;
		}

		for (FDriverSlot& Slot : DriverSlots)
		{
			if (Slot.bLive)
			{
				(void)Slot.Host->PumpReceive(InNowMilliseconds);
			}
		}
		Router.PreAdvance(InNowMilliseconds);
	}

	/** Flushes the router, retries live reliable channels in reverse order, then pumps hosts in reverse driver order. */
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
		for (std::size_t Index = DriverSlots.size(); Index > 0; --Index)
		{
			FDriverSlot& Slot = DriverSlots[Index - 1];
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
	struct FDriverSlot
	{
		/** Generation published with this slot's handle; release advances it before reuse. */
		std::uint8_t Generation{1};

		/** Distinguishes an occupied slot from raw storage that has no constructed host. */
		bool bLive{false};

		/** Configured host role, retained so AddChannel can derive its outbound peer target. */
		ENetMode Mode{ENetMode::Standalone};

		/** Storage for the host, whose address must remain fixed while bindings reference it. */
		alignas(FNetHost) std::byte HostStorage[sizeof(FNetHost)]{};

		/** Points at the host constructed in HostStorage while the slot is live. */
		FNetHost* Host{nullptr};
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

	/** Constructs a host in the first free slot, or returns null when driver capacity is exhausted. */
	FDriverSlot* AcquireDriverSlot(INetDriver& InDriver) noexcept
	{
		for (FDriverSlot& Slot : DriverSlots)
		{
			if (!Slot.bLive)
			{
				Slot.Host = new (&Slot.HostStorage) FNetHost(InDriver);
				Slot.bLive = true;
				return &Slot;
			}
		}
		return nullptr;
	}

	/** Destroys a driver host and advances the generation that invalidates any old handle. */
	void ReleaseDriverSlot(FDriverSlot& InSlot) noexcept
	{
		if (!InSlot.bLive)
		{
			return;
		}
		if (InSlot.Host != nullptr)
		{
			InSlot.Host->~FNetHost();
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

	/** Resolves a generation-checked driver handle to its live slot, rejecting forged or stale identities. */
	FDriverSlot* ResolveDriver(FNetDriverHandle InHandle) noexcept
	{
		if (!InHandle.IsValid() || InHandle.Index >= DriverSlots.size())
		{
			return nullptr;
		}

		FDriverSlot& Slot = DriverSlots[InHandle.Index];
		return (Slot.bLive && Slot.Generation == InHandle.Generation) ? &Slot : nullptr;
	}

	/** Maps the configured role to the only valid outbound target for its bindings. */
	static constexpr EChannelSendTarget GetSendTarget(ENetMode InMode) noexcept
	{
		return InMode == ENetMode::Client ? EChannelSendTarget::Server : EChannelSendTarget::AllPeers;
	}

	/** Creates a guaranteed wrapper before router registration, or registers a best-effort binding directly. */
	EMessageResult AddChannelToRouter(FChannelSlot& InSlot, EChannelReliability InReliability) noexcept
	{
		if (InReliability == EChannelReliability::BestEffort)
		{
			return Router.AddChannel(*InSlot.Binding);
		}

		const FReliableChannelConfig Config{TTraits::ReliableRetryIntervalMilliseconds, TTraits::MaxReliableSendAttempts};
		new (&InSlot.ReliableStorage) FReliableChannel(Router, Config);
		InSlot.Reliable = reinterpret_cast<FReliableChannel*>(&InSlot.ReliableStorage);
		InSlot.Reliable->SetInnerChannel(*InSlot.Binding);
		return Router.AddChannel(*InSlot.Reliable);
	}

	/** Shared router every configured channel registers with and every driver delivers into. */
	FRouter Router;

	/** Fixed slots that own all configured driver hosts. */
	std::array<FDriverSlot, TTraits::MaxNetDrivers> DriverSlots{};

	/** Fixed slots that own all configured bindings and optional reliable wrappers. */
	std::array<FChannelSlot, TTraits::MaxChannels> ChannelSlots{};

	/** Becomes true at the first BeginPlay so later composition cannot change the direct pumping order. */
	bool bCompositionClosed{false};
};

} // namespace MicroWorld
