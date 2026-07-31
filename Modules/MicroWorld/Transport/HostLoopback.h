#pragma once

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Transport/DeviceAddress.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld
{

/**
 * Deterministic in-process multi-port loopback network for host tests.
 *
 * Owns N mailboxes and N embedded per-port `IDevice`s; `Port(index)` hands out
 * the driver bound to the 1-byte loopback address equal to `index`. Two hosts
 * share one `THostLoopback` and each drive their own `Port(i)`; the ports live
 * inside the network, so their lifetimes track it automatically.
 */
template<std::size_t MaxPorts, std::size_t MailboxCapacity, std::size_t PacketBytes>
class THostLoopback final
{
	static_assert(MaxPorts > 0, "THostLoopback requires at least one port.");
	static_assert(MailboxCapacity > 0, "THostLoopback requires a nonzero per-mailbox capacity.");
	static_assert(PacketBytes > 0, "THostLoopback requires a nonzero per-packet byte capacity.");

	/**
	 * Owns the N per-port inbound mailboxes and the address-keyed routing for one
	 * in-process loopback network. Each mailbox is a bounded FIFO of packets carrying
	 * the sender's address; delivery and receive are transactional exactly like the
	 * single-link loopback they generalize.
	 */
	template<std::size_t MaxPorts, std::size_t MailboxCapacity, std::size_t PacketBytes>
	class TLoopbackMailboxes final
	{
		static_assert(MaxPorts > 0, "TLoopbackMailboxes requires at least one port.");
		static_assert(MailboxCapacity > 0, "TLoopbackMailboxes requires a nonzero per-mailbox capacity.");
		static_assert(PacketBytes > 0, "TLoopbackMailboxes requires a nonzero per-packet byte capacity.");

	public:
		/** Defaulted so the network can live in automatic or static storage without side effects. */
		TLoopbackMailboxes() noexcept = default;

		/**
		 * Enqueues one packet into the destination port's mailbox, stamped with the sender.
		 * `To` must be a 1-byte address whose value is a valid port index, else `Invalid`.
		 * Then applies the same null/oversized/full validation as the single-link loopback.
		 */
		ETransportResult Deliver(const FDeviceAddress& InTo, const FDeviceAddress& InFrom, TSpan<const std::uint8_t> InPacket) noexcept
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
		 * Pops one packet from `InLocalPort`'s mailbox into the caller destination.
		 * On Success writes the head bytes, `OutResult.BytesReceived`, AND `OutFrom` (the
		 * stored sender). On Full/Invalid/Unavailable leaves destination, OutResult, and
		 * OutFrom UNCHANGED. Same null-dest / empty / too-small rules as the single link.
		 */
		ETransportResult Receive(
			const std::uint8_t InLocalPort, FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept
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

		/** Distinguishes an empty mailbox without inspecting packet storage. */
		bool IsEmpty(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].QueuedCount == 0; }

		/** Distinguishes a full mailbox so a caller can observe backpressure. */
		bool IsFull(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].QueuedCount >= MailboxCapacity; }

		/** Reports how many packets are currently queued for receive on `InPort`. */
		std::size_t QueuedCount(const std::uint8_t InPort) const noexcept { return Mailboxes[InPort].QueuedCount; }

		/** Drops every queued packet on `InPort` so that mailbox's capacity can be reused deterministically. */
		void Drain(const std::uint8_t InPort) noexcept
		{
			FMailbox& Mailbox = Mailboxes[InPort];
			Mailbox.PacketLengths.fill(0);
			Mailbox.HeadIndex = 0;
			Mailbox.TailIndex = 0;
			Mailbox.QueuedCount = 0;
		}

		/** Drops every queued packet on every port so the whole network can be reused deterministically. */
		void DrainAll() noexcept
		{
			for (std::uint8_t Port = 0; Port < MaxPorts; ++Port)
			{
				Drain(Port);
			}
		}

	private:
		/** Active byte count of a loopback port address: one byte naming the destination port index. */
		static constexpr std::uint8_t LoopbackPortAddressBytes = 1;

		/** One bounded FIFO mailbox: fixed byte storage, per-slot length, per-slot sender, indices. */
		struct FMailbox
		{
			/** Fixed per-packet byte storage; only the leading `PacketLengths[i]` bytes are valid. */
			std::array<std::array<std::uint8_t, PacketBytes>, MailboxCapacity> PacketStorage{};

			/** Records the valid byte length of each queued packet so receives stay exact. */
			std::array<std::size_t, MailboxCapacity> PacketLengths{};

			/** Records the sender address stamped on each queued packet so receive can report it. */
			std::array<FDeviceAddress, MailboxCapacity> SenderAddresses{};

			/** Indexes the next packet to receive so the FIFO order is preserved. */
			std::size_t HeadIndex{0};

			/** Indexes the next free slot so delivers append without overwriting the head. */
			std::size_t TailIndex{0};

			/** Tracks occupancy so full and empty states are observable without wrap arithmetic. */
			std::size_t QueuedCount{0};
		};

		/** Validates a loopback destination: it must be exactly one byte naming a valid port. */
		static ETransportResult ValidateDeliverAddress(const FDeviceAddress& InTo) noexcept
		{
			if (InTo.Size != LoopbackPortAddressBytes || InTo.Bytes[0] >= MaxPorts)
			{
				return ETransportResult::Invalid;
			}
			return ETransportResult::Success;
		}

		/** Enqueues one already-validated packet at the tail, or `Full` when the mailbox has no free slot. */
		static ETransportResult EnqueuePacket(FMailbox& InTarget, const FDeviceAddress& InFrom, TSpan<const std::uint8_t> InPacket) noexcept
		{
			if (InTarget.QueuedCount >= MailboxCapacity)
			{
				return ETransportResult::Full;
			}
			StorePacketAt(InTarget, InTarget.TailIndex, InFrom, InPacket, InPacket.Size());
			AdvanceTail(InTarget);
			return ETransportResult::Success;
		}

		/** Rejects a null destination with nonzero length before the mailbox state is consulted. */
		static ETransportResult ValidateReceiveDestination(TSpan<std::uint8_t> InDestination) noexcept
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

		/** Reports whether the head packet fits the caller destination; false means the caller must retry larger. */
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

		/** Copies the head packet into the destination, stamps the sender and byte count, and advances the FIFO head. */
		static void PopHeadInto(
			FMailbox& InMailbox,
			const std::size_t InHeadSize,
			FDeviceAddress& OutFrom,
			TSpan<std::uint8_t> InDestination,
			FReceiveResult& OutResult) noexcept
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

		/** Copies one accepted packet, its length, and its sender into the slot at `InIndex`. */
		static void StorePacketAt(
			FMailbox& InMailbox,
			const std::size_t InIndex,
			const FDeviceAddress& InFrom,
			TSpan<const std::uint8_t> InPacket,
			const std::size_t InPacketSize) noexcept
		{
			if (InPacketSize > 0)
			{
				std::memcpy(InMailbox.PacketStorage[InIndex].data(), InPacket.Data(), InPacketSize);
			}
			InMailbox.PacketLengths[InIndex] = InPacketSize;
			InMailbox.SenderAddresses[InIndex] = InFrom;
		}

		/** Advances the tail and count after one accepted packet. */
		static void AdvanceTail(FMailbox& InMailbox) noexcept
		{
			InMailbox.TailIndex = (InMailbox.TailIndex + 1) % MailboxCapacity;
			++InMailbox.QueuedCount;
		}

		/** The N caller-owned mailboxes, indexed by port. */
		std::array<FMailbox, MaxPorts> Mailboxes{};
	};

	/** One port's driver view: forwards send/receive to the shared mailboxes using its bound index. */
	class FPort final : public IDevice
	{
	public:
		/** Default-constructed then bound by the enclosing network's constructor. */
		FPort() noexcept = default;

		/** Defaulted so an embedded port destructs without side effects. */
		~FPort() noexcept override = default;

		/** Binds this port to the shared mailboxes and its own 1-byte address; called once at construction. */
		void Bind(TLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes>* InMailboxes, const std::uint8_t InLocalIndex) noexcept
		{
			Mailboxes = InMailboxes;
			LocalIndex = InLocalIndex;
		}

		/** Delivers one packet to `InTo`'s mailbox stamped with this port's address. */
		ETransportResult TrySend(const FDeviceAddress& InTo, TSpan<const std::uint8_t> InPacket) noexcept override
		{
			return Mailboxes->Deliver(InTo, MakeLoopbackAddress(LocalIndex), InPacket);
		}

		/** Pops one packet from this port's mailbox, reporting the sender via OutFrom. */
		ETransportResult TryReceive(FDeviceAddress& OutFrom, TSpan<std::uint8_t> InDestination, FReceiveResult& OutResult) noexcept override
		{
			return Mailboxes->Receive(LocalIndex, OutFrom, InDestination, OutResult);
		}

		/** Reports the per-packet byte capacity of this loopback network. */
		std::size_t MaxPacketBytes() const noexcept override { return PacketBytes; }

	private:
		/** Shared mailboxes owned by the enclosing network; never owned here. */
		TLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes>* Mailboxes{nullptr};

		/** This port's 1-byte loopback address value. */
		std::uint8_t LocalIndex{0};
	};

public:
	/** Constructs N mailboxes and binds each embedded port to its own index. */
	THostLoopback() noexcept
	{
		for (std::uint8_t Index = 0; Index < MaxPorts; ++Index)
		{
			Ports[Index].Bind(&Mailboxes, Index);
		}
	}

	/** Prevents copying so one network value owns its fixed mailbox storage and ports. */
	THostLoopback(const THostLoopback&) = delete;

	/** Prevents copying so one network value owns its fixed mailbox storage and ports. */
	THostLoopback& operator=(const THostLoopback&) = delete;

	/** Defaulted so a network with automatic storage destructs without side effects. */
	~THostLoopback() noexcept = default;

	/** Returns the driver bound to `InIndex`; `InIndex` must be < MaxPorts (caller contract). */
	IDevice& Port(const std::uint8_t InIndex) noexcept { return Ports[InIndex]; }

	/** Reports the fixed number of ports this network exposes. */
	static constexpr std::size_t PortCount() noexcept { return MaxPorts; }

	/** Reports the fixed packet-slot capacity of every port's mailbox. */
	static constexpr std::size_t MailboxCapacityValue() noexcept { return MailboxCapacity; }

	/** Reports the maximum byte length accepted per packet. */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return PacketBytes; }

	/** Distinguishes an empty mailbox on `InPort` without inspecting packet storage. */
	bool IsEmpty(const std::uint8_t InPort) const noexcept { return Mailboxes.IsEmpty(InPort); }

	/** Distinguishes a full mailbox on `InPort` so a caller can observe backpressure. */
	bool IsFull(const std::uint8_t InPort) const noexcept { return Mailboxes.IsFull(InPort); }

	/** Reports how many packets are currently queued for receive on `InPort`. */
	std::size_t QueuedCount(const std::uint8_t InPort) const noexcept { return Mailboxes.QueuedCount(InPort); }

	/** Drops every queued packet on `InPort` so that mailbox's capacity can be reused deterministically. */
	void Drain(const std::uint8_t InPort) noexcept { Mailboxes.Drain(InPort); }

	/** Drops every queued packet on every port so the whole network can be reused deterministically. */
	void DrainAll() noexcept { Mailboxes.DrainAll(); }

private:
	/** The shared mailboxes; declared before Ports so it is fully constructed when ports bind. */
	TLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes> Mailboxes{};

	/** The N embedded per-port drivers handed out by Port(). */
	std::array<FPort, MaxPorts> Ports{};
};

} // namespace MicroWorld
