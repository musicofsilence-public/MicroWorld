#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld::Transport
{

/**
 * Motivation: Provides one deterministic in-process multi-port loopback network for host tests so two hosts exchange
 *   packets without a physical transport.
 * Responsibilities: Own N mailboxes and N embedded per-port IDevice values, hand out the device bound to the 1-byte
 *   loopback address equal to its index via Port(index), and keep the ports' lifetimes tracked by the network.
 * Example:
 *   THostLoopback<2, 4, 64> Net;
 *   IDevice& A = Net.Port(0);
 *   IDevice& B = Net.Port(1);
 *   A.TrySend(MakeLoopbackAddress(1), Packet);
 */
template<std::size_t MaxPorts, std::size_t MailboxCapacity, std::size_t PacketBytes>
class THostLoopback final
{
	static_assert(MaxPorts > 0, "THostLoopback requires at least one port.");
	static_assert(MailboxCapacity > 0, "THostLoopback requires a nonzero per-mailbox capacity.");
	static_assert(PacketBytes > 0, "THostLoopback requires a nonzero per-packet byte capacity.");

	/**
	 * Motivation: Owns the N per-port inbound mailboxes and address-keyed routing so one loopback network generalizes the
	 *   single-link loopback to many ports.
	 * Responsibilities: Hold one bounded FIFO mailbox per port where each queued packet carries its sender address, and keep
	 *   delivery and receive transactional exactly like the single-link loopback.
	 * Example:
	 *   // Internal: driven through the per-port FPort devices, not directly.
	 */
	template<std::size_t MaxPorts, std::size_t MailboxCapacity, std::size_t PacketBytes>
	class TLoopbackMailboxes final
	{
		static_assert(MaxPorts > 0, "TLoopbackMailboxes requires at least one port.");
		static_assert(MailboxCapacity > 0, "TLoopbackMailboxes requires a nonzero per-mailbox capacity.");
		static_assert(PacketBytes > 0, "TLoopbackMailboxes requires a nonzero per-packet byte capacity.");

	public:
		/**
		 * Motivation: Lets the network live in automatic or static storage without side effects.
		 * Responsibilities: Default-construct fixed storage with all mailboxes empty.
		 */
		TLoopbackMailboxes() noexcept = default;

		/**
		 * Motivation: Routes one packet to a destination port's mailbox stamped with its sender, mirroring a real send.
		 * Responsibilities: Require a 1-byte destination whose value is a valid port index (else Invalid), accept a zero-length
		 *   packet as a valid enqueue, reject a null-with-nonzero-length or oversize packet with Invalid, and report Full when
		 *   no slot remains.
		 */
		ETransportResult Deliver(
			const ::MicroWorld::Transport::Address::FDeviceAddress& InTo,
			const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom,
			Core::TSpan<const std::uint8_t> InPacket) noexcept
		{
			const ETransportResult AddressResult = ValidateDeliverAddress(InTo);
			if (AddressResult != ETransportResult::Success)
			{
				return AddressResult;
			}
			FMailbox& Target = Mailboxes[InTo.Bytes[0]];
			const std::size_t PacketSize = InPacket.Size();
			if (PacketSize == 0)
			{
				// A zero-length packet is a valid transport op; enqueue it so receive mirrors send.
				return EnqueuePacket(Target, InFrom, InPacket);
			}
			if (InPacket.Data() == nullptr)
			{
				return ETransportResult::Invalid;
			}
			if (PacketSize > PacketBytes)
			{
				// The packet can never fit a slot; the request is malformed.
				return ETransportResult::Invalid;
			}
			return EnqueuePacket(Target, InFrom, InPacket);
		}

		/**
		 * Motivation: Pops one packet from a port's mailbox transactionally so a failed receive never reports a partial delivery.
		 * Responsibilities: On Success write the head bytes, OutResult.BytesReceived, and OutFrom (the stored sender); on
		 *   Full, Invalid, or Unavailable leave destination, OutResult, and OutFrom unchanged.
		 */
		ETransportResult Receive(
			const std::uint8_t InLocalPort,
			::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
			Core::TSpan<std::uint8_t> InDestination,
			::MicroWorld::Transport::Device::FReceiveResult& OutResult) noexcept
		{
			const ETransportResult DestinationResult = ValidateReceiveDestination(InDestination);
			if (DestinationResult != ETransportResult::Success)
			{
				return DestinationResult;
			}
			FMailbox& Mailbox = Mailboxes[InLocalPort];
			if (Mailbox.QueuedCount == 0)
			{
				return ETransportResult::Unavailable;
			}
			const std::size_t HeadSize = Mailbox.PacketLengths[Mailbox.HeadIndex];
			if (!HeadFitsDestination(HeadSize, InDestination.Size()))
			{
				// Keep the head packet so the caller can retry with a larger buffer.
				return ETransportResult::Full;
			}
			PopHeadInto(Mailbox, HeadSize, OutFrom, InDestination, OutResult);
			return ETransportResult::Success;
		}

		/**
		 * Motivation: Lets a test distinguish an empty mailbox without inspecting packet storage.
		 * Responsibilities: Report whether the named port's mailbox has no queued packet.
		 */
		bool IsEmpty(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].QueuedCount == 0; }

		/**
		 * Motivation: Lets a test observe backpressure before a deliver would report Full.
		 * Responsibilities: Report whether the named port's mailbox has reached its slot capacity.
		 */
		bool IsFull(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].QueuedCount >= MailboxCapacity; }

		/**
		 * Motivation: Lets a test observe pending work on a port.
		 * Responsibilities: Report how many packets are queued for receive on the named port.
		 */
		std::size_t QueuedCount(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].QueuedCount; }

		/**
		 * Motivation: Lets a test reuse one mailbox's capacity deterministically between scenarios.
		 * Responsibilities: Drop every queued packet on the named port and reset its FIFO indices.
		 */
		void Drain(const std::uint8_t InPort) noexcept
		{
			FMailbox& Mailbox = Mailboxes[InPort];
			Mailbox.PacketLengths.fill(0);
			Mailbox.HeadIndex = 0;
			Mailbox.TailIndex = 0;
			Mailbox.QueuedCount = 0;
		}

		/**
		 * Motivation: Lets a test reuse the whole network deterministically between scenarios.
		 * Responsibilities: Drop every queued packet on every port.
		 */
		void DrainAll() noexcept
		{
			for (std::uint8_t Port = 0; Port < MaxPorts; ++Port)
			{
				Drain(Port);
			}
		}

	private:
		/** Motivation: Fixes the active byte count of a loopback port address that names the destination port index. */
		static constexpr std::uint8_t LoopbackPortAddressBytes = 1;

		/**
		 * Motivation: Holds one bounded FIFO mailbox so a loopback port queues packets with their sender addresses.
		 * Responsibilities: Carry fixed per-packet byte storage, per-slot lengths, per-slot senders, and head/tail indices.
		 * Example:
		 *   // Internal value type owned by TLoopbackMailboxes.
		 */
		struct FMailbox
		{
			/** Motivation: Provides fixed per-packet byte storage where only the leading PacketLengths[i] bytes are valid. */
			std::array<std::array<std::uint8_t, PacketBytes>, MailboxCapacity> PacketStorage{};

			/** Motivation: Records the valid byte length of each queued packet so receives stay exact. */
			std::array<std::size_t, MailboxCapacity> PacketLengths{};

			/** Motivation: Records the sender address stamped on each queued packet so receive can report it. */
			std::array<::MicroWorld::Transport::Address::FDeviceAddress, MailboxCapacity> SenderAddresses{};

			/** Motivation: Indexes the next packet to receive so the FIFO order is preserved. */
			std::size_t HeadIndex{0};

			/** Motivation: Indexes the next free slot so delivers append without overwriting the head. */
			std::size_t TailIndex{0};

			/** Motivation: Tracks occupancy so full and empty states are observable without wrap arithmetic. */
			std::size_t QueuedCount{0};
		};

		/**
		 * Motivation: Guards delivery against a destination that names no port before any mailbox state is consulted.
		 * Responsibilities: Return Success only when the destination is exactly one byte naming a valid port index, else Invalid.
		 */
		static ETransportResult ValidateDeliverAddress(const ::MicroWorld::Transport::Address::FDeviceAddress& InTo) noexcept
		{
			if (InTo.Size != LoopbackPortAddressBytes || InTo.Bytes[0] >= MaxPorts)
			{
				return ETransportResult::Invalid;
			}
			return ETransportResult::Success;
		}

		/**
		 * Motivation: Appends one validated packet at the tail so the FIFO order survives concurrent delivers.
		 * Responsibilities: Return Full when no slot is free, otherwise store the packet and advance the tail.
		 */
		static ETransportResult EnqueuePacket(
			FMailbox& InTarget, const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom, Core::TSpan<const std::uint8_t> InPacket) noexcept
		{
			if (InTarget.QueuedCount >= MailboxCapacity)
			{
				return ETransportResult::Full;
			}
			StorePacketAt(InTarget, InTarget.TailIndex, InFrom, InPacket, InPacket.Size());
			AdvanceTail(InTarget);
			return ETransportResult::Success;
		}

		/**
		 * Motivation: Rejects a null destination with nonzero length before mailbox state is consulted.
		 * Responsibilities: Return Invalid for a null destination with nonzero length independent of mailbox state, else Success.
		 */
		static ETransportResult ValidateReceiveDestination(Core::TSpan<std::uint8_t> InDestination) noexcept
		{
			// A null destination with nonzero length is an invalid request independent of the
			// mailbox state: validate it before the empty check so an empty mailbox still
			// returns Invalid for a malformed destination.
			if (InDestination.Size() != 0 && InDestination.Data() == nullptr)
			{
				return ETransportResult::Invalid;
			}
			return ETransportResult::Success;
		}

		/**
		 * Motivation: Decides whether a receive can succeed without dropping the head packet.
		 * Responsibilities: Return false (so the caller retries larger) when the head does not fit, including a nonzero head
		 *   into an empty destination that could not otherwise signal delivery.
		 */
		static bool HeadFitsDestination(const std::size_t InHeadSize, const std::size_t InDestinationSize) noexcept
		{
			if (InDestinationSize == 0)
			{
				// An empty destination cannot accept even a zero-length head packet
				// without losing the ability to signal that a packet was delivered.
				if (InHeadSize != 0)
				{
					return false;
				}
			}
			return InHeadSize <= InDestinationSize;
		}

		/**
		 * Motivation: Completes a successful receive by copying the head packet and advancing the FIFO.
		 * Responsibilities: Copy the head bytes, stamp the sender and byte count on the success path, and advance the head
		 *   past the delivered slot.
		 */
		static void PopHeadInto(
			FMailbox& InMailbox,
			const std::size_t InHeadSize,
			::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
			Core::TSpan<std::uint8_t> InDestination,
			::MicroWorld::Transport::Device::FReceiveResult& OutResult) noexcept
		{
			if (InHeadSize > 0)
			{
				std::memcpy(InDestination.Data(), InMailbox.PacketStorage[InMailbox.HeadIndex].data(), InHeadSize);
			}
			OutResult.BytesReceived = InHeadSize;
			// Stamp the sender only on the success path, before the head advances past it.
			OutFrom = InMailbox.SenderAddresses[InMailbox.HeadIndex];
			InMailbox.PacketLengths[InMailbox.HeadIndex] = 0;
			InMailbox.HeadIndex = (InMailbox.HeadIndex + 1) % MailboxCapacity;
			--InMailbox.QueuedCount;
		}

		/**
		 * Motivation: Writes one accepted packet, its length, and its sender into a named slot.
		 * Responsibilities: Copy the bytes (if any), record the length, and stamp the sender address.
		 */
		static void StorePacketAt(
			FMailbox& InMailbox,
			const std::size_t InIndex,
			const ::MicroWorld::Transport::Address::FDeviceAddress& InFrom,
			Core::TSpan<const std::uint8_t> InPacket,
			const std::size_t InPacketSize) noexcept
		{
			if (InPacketSize > 0)
			{
				std::memcpy(InMailbox.PacketStorage[InIndex].data(), InPacket.Data(), InPacketSize);
			}
			InMailbox.PacketLengths[InIndex] = InPacketSize;
			InMailbox.SenderAddresses[InIndex] = InFrom;
		}

		/**
		 * Motivation: Completes one enqueue by moving the tail and count forward.
		 * Responsibilities: Advance the tail index with wraparound and increment occupancy.
		 */
		static void AdvanceTail(FMailbox& InMailbox) noexcept
		{
			InMailbox.TailIndex = (InMailbox.TailIndex + 1) % MailboxCapacity;
			++InMailbox.QueuedCount;
		}

		/** Motivation: Holds the N caller-owned mailboxes indexed by port. */
		std::array<FMailbox, MaxPorts> Mailboxes{};
	};

	/**
	 * Motivation: Adapts one port's index to the IDevice interface so each host drives its own device backed by shared mailboxes.
	 * Responsibilities: Forward send and receive to the shared mailboxes using the bound port index, and report the network's
	 *   per-packet byte capacity.
	 * Example:
	 *   // Returned by THostLoopback::Port(index); not constructed directly.
	 */
	class FPort final : public ::MicroWorld::Transport::Device::IDevice
	{
	public:
		/**
		 * Motivation: Allows the enclosing network to default-construct each port before binding it.
		 * Responsibilities: Construct an inert port bound to nothing until Bind is called.
		 */
		FPort() noexcept = default;

		/**
		 * Motivation: Keeps an embedded port side-effect free on destruction.
		 * Responsibilities: Default the destructor since the port owns no resource.
		 */
		~FPort() noexcept override = default;

		/**
		 * Motivation: Wires a port to the shared mailboxes and its own 1-byte address once at construction.
		 * Responsibilities: Store the mailbox pointer and the port index.
		 */
		void Bind(TLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes>* InMailboxes, const std::uint8_t InLocalIndex) noexcept
		{
			Mailboxes = InMailboxes;
			LocalIndex = InLocalIndex;
		}

		/**
		 * Motivation: Implements the device send contract by routing to the destination port's mailbox.
		 * Responsibilities: Deliver one packet stamped with this port's address to the shared mailboxes.
		 */
		ETransportResult TrySend(
			const ::MicroWorld::Transport::Address::FDeviceAddress& InTo, Core::TSpan<const std::uint8_t> InPacket) noexcept override
		{
			return Mailboxes->Deliver(InTo, ::MicroWorld::Transport::Address::MakeLoopbackAddress(LocalIndex), InPacket);
		}

		/**
		 * Motivation: Implements the device receive contract by popping from this port's mailbox.
		 * Responsibilities: Pop one packet from this port's mailbox and report its sender via OutFrom.
		 */
		ETransportResult TryReceive(
			::MicroWorld::Transport::Address::FDeviceAddress& OutFrom,
			Core::TSpan<std::uint8_t> InDestination,
			::MicroWorld::Transport::Device::FReceiveResult& OutResult) noexcept override
		{
			return Mailboxes->Receive(LocalIndex, OutFrom, InDestination, OutResult);
		}

		/**
		 * Motivation: Implements the device capacity query so callers bound sends to the loopback's packet size.
		 * Responsibilities: Report the per-packet byte capacity of this loopback network.
		 */
		std::size_t MaxPacketBytes() const noexcept override { return PacketBytes; }

	private:
		/** Motivation: References the shared mailboxes owned by the enclosing network. */
		TLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes>* Mailboxes{nullptr};

		/** Motivation: Holds this port's 1-byte loopback address value. */
		std::uint8_t LocalIndex{0};
	};

public:
	/**
	 * Motivation: Builds the network ready to use by constructing its mailboxes and binding each embedded port.
	 * Responsibilities: Construct N mailboxes and bind each port to its own index.
	 */
	THostLoopback() noexcept
	{
		for (std::uint8_t Index = 0; Index < MaxPorts; ++Index)
		{
			Ports[Index].Bind(&Mailboxes, Index);
		}
	}

	/**
	 * Motivation: Prevents copying so one network value owns its fixed mailbox storage and ports.
	 * Responsibilities: Reject copy construction so two networks never alias one set of mailboxes.
	 */
	THostLoopback(const THostLoopback&) = delete;

	/**
	 * Motivation: Prevents copying so one network value owns its fixed mailbox storage and ports.
	 * Responsibilities: Reject copy assignment so two networks never alias one set of mailboxes.
	 */
	THostLoopback& operator=(const THostLoopback&) = delete;

	/**
	 * Motivation: Keeps a network with automatic storage side-effect free on destruction.
	 * Responsibilities: Default the destructor since the network owns only fixed value storage.
	 */
	~THostLoopback() noexcept = default;

	/**
	 * Motivation: Hands hosts the device bound to a port so each drives its own loopback endpoint.
	 * Responsibilities: Return the device at InIndex, which must be below MaxPorts by caller contract.
	 */
	::MicroWorld::Transport::Device::IDevice& Port(const std::uint8_t InIndex) noexcept { return Ports[InIndex]; }

	/**
	 * Motivation: Lets a caller observe the fixed port count without magic numbers.
	 * Responsibilities: Report the fixed number of ports this network exposes.
	 */
	static constexpr std::size_t PortCount() noexcept { return MaxPorts; }

	/**
	 * Motivation: Lets a caller observe the fixed mailbox depth without magic numbers.
	 * Responsibilities: Report the fixed packet-slot capacity of every port's mailbox.
	 */
	static constexpr std::size_t MailboxCapacityValue() noexcept { return MailboxCapacity; }

	/**
	 * Motivation: Lets a caller observe the per-packet byte ceiling without magic numbers.
	 * Responsibilities: Report the maximum byte length accepted per packet.
	 */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return PacketBytes; }

	/**
	 * Motivation: Exposes a port's empty state without inspecting packet storage.
	 * Responsibilities: Delegate IsEmpty to the shared mailboxes for the named port.
	 */
	bool IsEmpty(const std::uint8_t InPort) const noexcept { return Mailboxes.IsEmpty(InPort); }

	/**
	 * Motivation: Exposes a port's full state so a caller can observe backpressure.
	 * Responsibilities: Delegate IsFull to the shared mailboxes for the named port.
	 */
	bool IsFull(const std::uint8_t InPort) const noexcept { return Mailboxes.IsFull(InPort); }

	/**
	 * Motivation: Exposes a port's queued count so a test can assert pending work.
	 * Responsibilities: Delegate QueuedCount to the shared mailboxes for the named port.
	 */
	std::size_t QueuedCount(const std::uint8_t InPort) const noexcept { return Mailboxes.QueuedCount(InPort); }

	/**
	 * Motivation: Lets a test reuse one port's mailbox deterministically between scenarios.
	 * Responsibilities: Delegate Drain to the shared mailboxes for the named port.
	 */
	void Drain(const std::uint8_t InPort) noexcept { Mailboxes.Drain(InPort); }

	/**
	 * Motivation: Lets a test reuse the whole network deterministically between scenarios.
	 * Responsibilities: Delegate DrainAll to the shared mailboxes.
	 */
	void DrainAll() noexcept { Mailboxes.DrainAll(); }

private:
	/** Motivation: Owns the shared mailboxes, declared before Ports so it is fully constructed when ports bind. */
	TLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes> Mailboxes{};

	/** Motivation: Holds the N embedded per-port devices handed out by Port(). */
	std::array<FPort, MaxPorts> Ports{};
};

} // namespace MicroWorld::Transport
