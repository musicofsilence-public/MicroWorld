#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Core/IO/TransportResult.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Transport
{

/**
 * Motivation: Provides a deterministic, fixed-capacity addressed loopback transport for portable tests and compositions.
 * Responsibilities: Own one FIFO mailbox per port and embedded transport devices that route only to an explicitly
 *   addressed destination; it provides no broadcast or fanout, so a sender receives only when it deliberately addresses itself.
 * Example:
 *   TLoopbackNetwork<2, 4, 64> Network;
 *   Network.Port(0).TrySend(MakeLoopbackAddress(1), Packet);
 */
template<std::size_t MaxPorts, std::size_t MailboxCapacity, std::size_t PacketBytes>
class TLoopbackNetwork final
{
	static_assert(MaxPorts > 0, "TLoopbackNetwork requires at least one port.");
	static_assert(MaxPorts <= 256, "TLoopbackNetwork supports at most 256 one-byte addressed ports.");
	static_assert(MailboxCapacity > 0, "TLoopbackNetwork requires a nonzero per-mailbox capacity.");
	static_assert(PacketBytes > 0, "TLoopbackNetwork requires a nonzero per-packet byte capacity.");

	/**
	 * Motivation: Holds one accepted packet until its addressed port receives it.
	 * Responsibilities: Preserve sender identity, exact byte length, and packet bytes in fixed storage.
	 * Example:
	 *   // Internal value stored in a mailbox FIFO slot.
	 */
	struct FPacketSlot final
	{
		/** Motivation: Retains the sender address until the destination port receives this packet. */
		Core::FDeviceAddress Sender{};

		/** Motivation: Records how many leading Bytes elements belong to this packet. */
		std::size_t Length{0};

		/** Motivation: Provides fixed storage for one accepted packet without allocating. */
		std::array<std::uint8_t, PacketBytes> Bytes{};
	};

	/**
	 * Motivation: Gives one port bounded FIFO storage independent from every other port.
	 * Responsibilities: Own the packet slots and track the next read, next write, and current occupancy.
	 * Example:
	 *   // Internal value indexed by the addressed port number.
	 */
	struct FMailbox final
	{
		/** Motivation: Holds the fixed FIFO slots for one destination port. */
		std::array<FPacketSlot, MailboxCapacity> Slots{};

		/** Motivation: Identifies the next FIFO slot available to receive. */
		std::size_t HeadIndex{0};

		/** Motivation: Identifies the next FIFO slot available to enqueue. */
		std::size_t TailIndex{0};

		/** Motivation: Distinguishes full and empty states without relying on index equality. */
		std::size_t Count{0};
	};

	/**
	 * Motivation: Adapts one bound port index to the generic transport-device interface.
	 * Responsibilities: Route sends through the shared network, receive only this port's mailbox, and expose its packet limit.
	 * Example:
	 *   Core::ITransportDevice& Device = Network.Port(0);
	 */
	class FPort final : public Core::ITransportDevice
	{
	public:
		/**
		 * Motivation: Lets the owning network construct all embedded ports before binding their indices.
		 * Responsibilities: Construct an inert port that the owning network binds before exposing it.
		 */
		FPort() noexcept = default;

		/**
		 * Motivation: Keeps an embedded port side-effect free on destruction.
		 * Responsibilities: Release no resource because the owning network owns all mailbox storage.
		 */
		~FPort() noexcept override = default;

		/**
		 * Motivation: Runs the required lifecycle hook without introducing asynchronous state.
		 * Responsibilities: Perform no work because a synchronous mailbox has no staged pre-advance work.
		 */
		void PreAdvance(Core::TimePointMilliseconds) noexcept override {}

		/**
		 * Motivation: Runs the required lifecycle hook without introducing asynchronous state.
		 * Responsibilities: Perform no work because a synchronous mailbox has no staged post-advance work.
		 */
		void PostAdvance(Core::TimePointMilliseconds) noexcept override {}

		/**
		 * Motivation: Implements one transactional addressed send from this port.
		 * Responsibilities: Route the whole packet through the owning network and report the result without partial enqueue.
		 */
		Core::ETransportResult TrySend(const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override
		{
			return Network->TrySend(LocalPort, InTo, InPacket);
		}

		/**
		 * Motivation: Implements one transactional receive for this port.
		 * Responsibilities: Receive only this port's FIFO head through the owning network.
		 */
		Core::ETransportResult TryReceive(
			Core::FDeviceAddress& OutFrom, Core::TSpan<std::uint8_t> InDestination, Core::FReceiveResult& OutResult) noexcept override
		{
			return Network->TryReceive(LocalPort, OutFrom, InDestination, OutResult);
		}

		/**
		 * Motivation: Lets callers bound one send to the loopback packet capacity.
		 * Responsibilities: Report the largest unsplit packet accepted by this network.
		 */
		std::size_t MaxPacketBytes() const noexcept override { return PacketBytes; }

	private:
		friend class TLoopbackNetwork;

		/**
		 * Motivation: Binds an embedded device to its owning network and stable port number.
		 * Responsibilities: Store the valid network pointer and local port supplied only by the owner during construction.
		 */
		void Bind(TLoopbackNetwork* const InNetwork, const std::uint8_t InLocalPort) noexcept
		{
			Network = InNetwork;
			LocalPort = InLocalPort;
		}

		/** Motivation: References mailbox storage owned by the enclosing network. */
		TLoopbackNetwork* Network{nullptr};

		/** Motivation: Stores this port's one-byte source address. */
		std::uint8_t LocalPort{0};
	};

public:
	/**
	 * Motivation: Constructs all bounded mailboxes and binds every embedded port to its matching index.
	 * Responsibilities: Leave the network ready for synchronous sends and receives without allocation.
	 */
	TLoopbackNetwork() noexcept
	{
		for (std::size_t Index = 0; Index < MaxPorts; ++Index)
		{
			Ports[Index].Bind(this, static_cast<std::uint8_t>(Index));
		}
	}

	/**
	 * Motivation: Prevents two network values from aliasing one set of embedded port pointers.
	 * Responsibilities: Reject copy construction because every port remains bound to its original mailbox owner.
	 */
	TLoopbackNetwork(const TLoopbackNetwork&) = delete;

	/**
	 * Motivation: Prevents assignment from rebinding embedded ports behind existing interface references.
	 * Responsibilities: Reject copy assignment so mailbox and port ownership remain stable.
	 */
	TLoopbackNetwork& operator=(const TLoopbackNetwork&) = delete;

	/**
	 * Motivation: Prevents moved-from port interface references from targeting an invalid mailbox owner.
	 * Responsibilities: Reject move construction because embedded ports retain their owning network pointer.
	 */
	TLoopbackNetwork(TLoopbackNetwork&&) = delete;

	/**
	 * Motivation: Prevents assignment from changing the mailbox owner behind exposed port references.
	 * Responsibilities: Reject move assignment so every port binding remains stable for its lifetime.
	 */
	TLoopbackNetwork& operator=(TLoopbackNetwork&&) = delete;

	/**
	 * Motivation: Keeps a fixed-storage network side-effect free on destruction.
	 * Responsibilities: Release no external resource because all state is embedded value storage.
	 */
	~TLoopbackNetwork() noexcept = default;

	/**
	 * Motivation: Gives the application entry point the transport device bound to one port.
	 * Responsibilities: Return the port at InPort, which the caller keeps below PortCount().
	 */
	Core::ITransportDevice& Port(const std::uint8_t InPort) noexcept { return Ports[InPort]; }

	/**
	 * Motivation: Lets callers observe the fixed network port count without magic numbers.
	 * Responsibilities: Report the number of independently addressable ports.
	 */
	static constexpr std::size_t PortCount() noexcept { return MaxPorts; }

	/**
	 * Motivation: Lets callers observe each mailbox's fixed slot capacity.
	 * Responsibilities: Report the maximum queued packet count per destination port.
	 */
	static constexpr std::size_t MailboxCapacityValue() noexcept { return MailboxCapacity; }

	/**
	 * Motivation: Lets callers observe the fixed byte ceiling for one packet.
	 * Responsibilities: Report the maximum accepted byte length of an individual packet.
	 */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return PacketBytes; }

	/**
	 * Motivation: Lets tests observe whether one mailbox has pending packets.
	 * Responsibilities: Report true only when the named mailbox contains no packet.
	 */
	bool IsEmpty(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].Count == 0; }

	/**
	 * Motivation: Lets tests observe whether one mailbox will reject another packet.
	 * Responsibilities: Report true only when the named mailbox has no free slot.
	 */
	bool IsFull(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].Count == MailboxCapacity; }

	/**
	 * Motivation: Lets tests assert pending work without inspecting packet storage.
	 * Responsibilities: Report the number of packets queued for the named port.
	 */
	std::size_t QueuedCount(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].Count; }

	/**
	 * Motivation: Lets tests reset one destination mailbox deterministically.
	 * Responsibilities: Drop every queued packet for InPort and reset its FIFO indices.
	 */
	void Drain(const std::uint8_t InPort) noexcept
	{
		FMailbox& Mailbox = Mailboxes[InPort];
		Mailbox.HeadIndex = 0;
		Mailbox.TailIndex = 0;
		Mailbox.Count = 0;
	}

	/**
	 * Motivation: Lets tests reset every mailbox deterministically between scenarios.
	 * Responsibilities: Drop every queued packet on every port.
	 */
	void DrainAll() noexcept
	{
		for (std::size_t Index = 0; Index < MaxPorts; ++Index)
		{
			Drain(static_cast<std::uint8_t>(Index));
		}
	}

private:
	/** Motivation: Sizes an addressed port route, while an empty broadcast route is invalid for this network. */
	static constexpr std::uint8_t PortAddressBytes = 1;

	/**
	 * Motivation: Rejects broadcast, malformed, and out-of-range destinations before a send touches any mailbox.
	 * Responsibilities: Return true and write OutPort only for an exactly one-byte valid port address.
	 */
	static bool DecodeDestination(const Core::FDeviceAddress& InTo, std::uint8_t& OutPort) noexcept
	{
		if (InTo.Size != PortAddressBytes || InTo.Bytes[0] >= MaxPorts)
		{
			return false;
		}

		OutPort = InTo.Bytes[0];
		return true;
	}

	/**
	 * Motivation: Appends one validated packet without exposing partial state to a receiver.
	 * Responsibilities: Copy the packet and sender into the tail slot, then advance occupancy once.
	 */
	static void Enqueue(FMailbox& InMailbox, const Core::FDeviceAddress& InFrom, Core::TSpan<const std::uint8_t> InPacket) noexcept
	{
		FPacketSlot& Slot = InMailbox.Slots[InMailbox.TailIndex];
		const std::size_t PacketSize = InPacket.Size();
		if (PacketSize > 0)
		{
			std::memcpy(Slot.Bytes.data(), InPacket.Data(), PacketSize);
		}
		Slot.Sender = InFrom;
		Slot.Length = PacketSize;
		InMailbox.TailIndex = (InMailbox.TailIndex + 1) % MailboxCapacity;
		++InMailbox.Count;
	}

	/**
	 * Motivation: Routes a packet from an embedded source port to one explicitly addressed mailbox.
	 * Responsibilities: Validate the whole request before mutation, retain a full mailbox unchanged, and enqueue atomically on Success.
	 */
	Core::ETransportResult TrySend(const std::uint8_t InFromPort, const Core::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept
	{
		std::uint8_t DestinationPort{0};
		if (!DecodeDestination(InTo, DestinationPort))
		{
			return Core::ETransportResult::Invalid;
		}

		const std::size_t PacketSize = InPacket.Size();
		if ((PacketSize > 0 && InPacket.Data() == nullptr) || PacketSize > PacketBytes)
		{
			return Core::ETransportResult::Invalid;
		}

		FMailbox& DestinationMailbox = Mailboxes[DestinationPort];
		if (DestinationMailbox.Count == MailboxCapacity)
		{
			return Core::ETransportResult::Full;
		}

		Enqueue(DestinationMailbox, Core::MakeLoopbackAddress(InFromPort), InPacket);
		return Core::ETransportResult::Success;
	}

	/**
	 * Motivation: Delivers the FIFO head only when the caller supplied a valid, large enough destination.
	 * Responsibilities: Preserve outputs and the mailbox head on every failure, then copy, report, and pop only on Success.
	 */
	Core::ETransportResult TryReceive(
		const std::uint8_t InLocalPort,
		Core::FDeviceAddress& OutFrom,
		Core::TSpan<std::uint8_t> InDestination,
		Core::FReceiveResult& OutResult) noexcept
	{
		if (InDestination.Size() > 0 && InDestination.Data() == nullptr)
		{
			return Core::ETransportResult::Invalid;
		}

		FMailbox& Mailbox = Mailboxes[InLocalPort];
		if (Mailbox.Count == 0)
		{
			return Core::ETransportResult::Unavailable;
		}

		FPacketSlot& Head = Mailbox.Slots[Mailbox.HeadIndex];
		if (Head.Length > InDestination.Size())
		{
			return Core::ETransportResult::Full;
		}

		if (Head.Length > 0)
		{
			std::memcpy(InDestination.Data(), Head.Bytes.data(), Head.Length);
		}
		OutResult.BytesReceived = Head.Length;
		OutFrom = Head.Sender;
		Mailbox.HeadIndex = (Mailbox.HeadIndex + 1) % MailboxCapacity;
		--Mailbox.Count;
		return Core::ETransportResult::Success;
	}

	/** Motivation: Owns one isolated mailbox for every addressable port. */
	std::array<FMailbox, MaxPorts> Mailboxes{};

	/** Motivation: Owns the embedded transport devices returned by Port(). */
	std::array<FPort, MaxPorts> Ports{};
};

} // namespace MicroWorld::Transport
