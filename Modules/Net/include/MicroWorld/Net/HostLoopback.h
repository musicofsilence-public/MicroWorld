#pragma once

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace MicroWorld
{
namespace Detail
{

	/**
	 * Owns the N per-port inbound mailboxes and the address-keyed routing for one
	 * in-process loopback network. Each mailbox is a bounded FIFO of packets carrying
	 * the sender's address; delivery and receive are transactional exactly like the
	 * single-link loopback they generalize.
	 */
	template<std::size_t MaxPorts, std::size_t MailboxCapacity, std::size_t PacketBytes>
	class FLoopbackMailboxes final
	{
		static_assert(MaxPorts > 0, "FLoopbackMailboxes requires at least one port.");
		static_assert(MailboxCapacity > 0, "FLoopbackMailboxes requires a nonzero per-mailbox capacity.");
		static_assert(PacketBytes > 0, "FLoopbackMailboxes requires a nonzero per-packet byte capacity.");

	public:
		/** Defaulted so the network can live in automatic or static storage without side effects. */
		FLoopbackMailboxes() noexcept = default;

		/**
		 * Enqueues one packet into the destination port's mailbox, stamped with the sender.
		 * `To` must be a 1-byte address whose value is a valid port index, else `Invalid`.
		 * Then applies the same null/oversized/full validation as the single-link loopback.
		 */
		ENetResult Deliver(const FNetAddress& To, const FNetAddress& From, TSpan<const std::uint8_t> Packet) noexcept
		{
			// Validate the destination address first: it must be exactly one byte naming a valid port.
			if (To.Size != 1 || To.Bytes[0] >= MaxPorts)
			{
				return ENetResult::Invalid;
			}
			FMailbox& Target = Mailboxes[To.Bytes[0]];
			const std::size_t PacketSize = Packet.Size();
			if (PacketSize == 0)
			{
				// A zero-length packet is a valid transport op; enqueue it so receive mirrors send.
				if (Target.QueuedCount >= MailboxCapacity)
				{
					return ENetResult::Full;
				}
				StorePacketAt(Target, Target.TailIndex, From, Packet, 0);
				AdvanceTail(Target);
				return ENetResult::Success;
			}
			if (Packet.Data() == nullptr)
			{
				return ENetResult::Invalid;
			}
			if (PacketSize > PacketBytes)
			{
				// The packet can never fit a slot; the request is malformed.
				return ENetResult::Invalid;
			}
			if (Target.QueuedCount >= MailboxCapacity)
			{
				return ENetResult::Full;
			}
			StorePacketAt(Target, Target.TailIndex, From, Packet, PacketSize);
			AdvanceTail(Target);
			return ENetResult::Success;
		}

		/**
		 * Pops one packet from `LocalPort`'s mailbox into the caller destination.
		 * On Success writes the head bytes, `OutResult.BytesReceived`, AND `OutFrom` (the
		 * stored sender). On Full/Invalid/Unavailable leaves destination, OutResult, and
		 * OutFrom UNCHANGED. Same null-dest / empty / too-small rules as the single link.
		 */
		ENetResult Receive(const std::uint8_t LocalPort, FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept
		{
			// A null destination with nonzero length is an invalid request independent of the
			// mailbox state: validate it before the empty check so an empty mailbox still
			// returns Invalid for a malformed destination.
			if (Destination.Size() != 0 && Destination.Data() == nullptr)
			{
				return ENetResult::Invalid;
			}
			FMailbox& Mailbox = Mailboxes[LocalPort];
			if (Mailbox.QueuedCount == 0)
			{
				return ENetResult::Unavailable;
			}
			const std::size_t HeadSize = Mailbox.PacketLengths[Mailbox.HeadIndex];
			const std::size_t DestinationSize = Destination.Size();
			if (DestinationSize == 0)
			{
				// An empty destination cannot accept even a zero-length head packet
				// without losing the ability to signal that a packet was delivered.
				if (HeadSize != 0)
				{
					return ENetResult::Full;
				}
			}
			if (HeadSize > DestinationSize)
			{
				// Keep the head packet so the caller can retry with a larger buffer.
				return ENetResult::Full;
			}
			if (HeadSize > 0)
			{
				std::memcpy(Destination.Data(), Mailbox.PacketStorage[Mailbox.HeadIndex].data(), HeadSize);
			}
			OutResult.BytesReceived = HeadSize;
			// Stamp the sender only on the success path, before the head advances past it.
			OutFrom = Mailbox.SenderAddresses[Mailbox.HeadIndex];
			Mailbox.PacketLengths[Mailbox.HeadIndex] = 0;
			Mailbox.HeadIndex = (Mailbox.HeadIndex + 1) % MailboxCapacity;
			--Mailbox.QueuedCount;
			return ENetResult::Success;
		}

		/** Distinguishes an empty mailbox without inspecting packet storage. */
		bool IsEmpty(const std::uint8_t Port) const noexcept { return Mailboxes[Port].QueuedCount == 0; }

		/** Distinguishes a full mailbox so a caller can observe backpressure. */
		bool IsFull(const std::uint8_t Port) const noexcept { return Mailboxes[Port].QueuedCount >= MailboxCapacity; }

		/** Reports how many packets are currently queued for receive on `Port`. */
		std::size_t QueuedCount(const std::uint8_t Port) const noexcept { return Mailboxes[Port].QueuedCount; }

		/** Drops every queued packet on `Port` so that mailbox's capacity can be reused deterministically. */
		void Drain(const std::uint8_t Port) noexcept
		{
			FMailbox& Mailbox = Mailboxes[Port];
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
		/** One bounded FIFO mailbox: fixed byte storage, per-slot length, per-slot sender, indices. */
		struct FMailbox
		{
			/** Fixed per-packet byte storage; only the leading `PacketLengths[i]` bytes are valid. */
			std::array<std::array<std::uint8_t, PacketBytes>, MailboxCapacity> PacketStorage{};

			/** Records the valid byte length of each queued packet so receives stay exact. */
			std::array<std::size_t, MailboxCapacity> PacketLengths{};

			/** Records the sender address stamped on each queued packet so receive can report it. */
			std::array<FNetAddress, MailboxCapacity> SenderAddresses{};

			/** Indexes the next packet to receive so the FIFO order is preserved. */
			std::size_t HeadIndex{0};

			/** Indexes the next free slot so delivers append without overwriting the head. */
			std::size_t TailIndex{0};

			/** Tracks occupancy so full and empty states are observable without wrap arithmetic. */
			std::size_t QueuedCount{0};
		};

		/** Copies one accepted packet, its length, and its sender into the slot at `Index`. */
		static void StorePacketAt(
			FMailbox& Mailbox,
			const std::size_t Index,
			const FNetAddress& From,
			TSpan<const std::uint8_t> Packet,
			const std::size_t PacketSize) noexcept
		{
			if (PacketSize > 0)
			{
				std::memcpy(Mailbox.PacketStorage[Index].data(), Packet.Data(), PacketSize);
			}
			Mailbox.PacketLengths[Index] = PacketSize;
			Mailbox.SenderAddresses[Index] = From;
		}

		/** Advances the tail and count after one accepted packet. */
		static void AdvanceTail(FMailbox& Mailbox) noexcept
		{
			Mailbox.TailIndex = (Mailbox.TailIndex + 1) % MailboxCapacity;
			++Mailbox.QueuedCount;
		}

		/** The N caller-owned mailboxes, indexed by port. */
		std::array<FMailbox, MaxPorts> Mailboxes{};
	};

} // namespace Detail

/**
 * Deterministic in-process multi-port loopback network for host tests.
 *
 * Owns N mailboxes and N embedded per-port `INetDriver`s; `Port(index)` hands out
 * the driver bound to the 1-byte loopback address equal to `index`. Two hosts
 * share one `FHostLoopback` and each drive their own `Port(i)`; the ports live
 * inside the network, so their lifetimes track it automatically.
 */
template<std::size_t MaxPorts, std::size_t MailboxCapacity, std::size_t PacketBytes>
class FHostLoopback final
{
	static_assert(MaxPorts > 0, "FHostLoopback requires at least one port.");
	static_assert(MailboxCapacity > 0, "FHostLoopback requires a nonzero per-mailbox capacity.");
	static_assert(PacketBytes > 0, "FHostLoopback requires a nonzero per-packet byte capacity.");

	/** One port's driver view: forwards send/receive to the shared mailboxes using its bound index. */
	class FPort final : public INetDriver
	{
	public:
		/** Default-constructed then bound by the enclosing network's constructor. */
		FPort() noexcept = default;

		/** Defaulted so an embedded port destructs without side effects. */
		~FPort() noexcept override = default;

		/** Binds this port to the shared mailboxes and its own 1-byte address; called once at construction. */
		void Bind(Detail::FLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes>* InMailboxes, const std::uint8_t InLocalIndex) noexcept
		{
			Mailboxes = InMailboxes;
			LocalIndex = InLocalIndex;
		}

		/** Delivers one packet to `To`'s mailbox stamped with this port's address. */
		ENetResult TrySend(const FNetAddress& To, TSpan<const std::uint8_t> Packet) noexcept override
		{
			return Mailboxes->Deliver(To, MakeLoopbackAddress(LocalIndex), Packet);
		}

		/** Pops one packet from this port's mailbox, reporting the sender via OutFrom. */
		ENetResult TryReceive(FNetAddress& OutFrom, TSpan<std::uint8_t> Destination, FNetReceiveResult& OutResult) noexcept override
		{
			return Mailboxes->Receive(LocalIndex, OutFrom, Destination, OutResult);
		}

		/** Reports the per-packet byte capacity of this loopback network. */
		std::size_t MaxPacketBytes() const noexcept override { return PacketBytes; }

	private:
		/** Shared mailboxes owned by the enclosing network; never owned here. */
		Detail::FLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes>* Mailboxes{nullptr};

		/** This port's 1-byte loopback address value. */
		std::uint8_t LocalIndex{0};
	};

public:
	/** Constructs N mailboxes and binds each embedded port to its own index. */
	FHostLoopback() noexcept
	{
		for (std::uint8_t Index = 0; Index < MaxPorts; ++Index)
		{
			Ports[Index].Bind(&Mailboxes, Index);
		}
	}

	/** Prevents copying so one network value owns its fixed mailbox storage and ports. */
	FHostLoopback(const FHostLoopback&) = delete;

	/** Prevents copying so one network value owns its fixed mailbox storage and ports. */
	FHostLoopback& operator=(const FHostLoopback&) = delete;

	/** Defaulted so a network with automatic storage destructs without side effects. */
	~FHostLoopback() noexcept = default;

	/** Returns the driver bound to `Index`; `Index` must be < MaxPorts (caller contract). */
	INetDriver& Port(const std::uint8_t Index) noexcept { return Ports[Index]; }

	/** Reports the fixed number of ports this network exposes. */
	static constexpr std::size_t PortCount() noexcept { return MaxPorts; }

	/** Reports the fixed packet-slot capacity of every port's mailbox. */
	static constexpr std::size_t MailboxCapacityValue() noexcept { return MailboxCapacity; }

	/** Reports the maximum byte length accepted per packet. */
	static constexpr std::size_t MaximumPacketBytes() noexcept { return PacketBytes; }

	/** Distinguishes an empty mailbox on `Port` without inspecting packet storage. */
	bool IsEmpty(const std::uint8_t Port) const noexcept { return Mailboxes.IsEmpty(Port); }

	/** Distinguishes a full mailbox on `Port` so a caller can observe backpressure. */
	bool IsFull(const std::uint8_t Port) const noexcept { return Mailboxes.IsFull(Port); }

	/** Reports how many packets are currently queued for receive on `Port`. */
	std::size_t QueuedCount(const std::uint8_t Port) const noexcept { return Mailboxes.QueuedCount(Port); }

	/** Drops every queued packet on `Port` so that mailbox's capacity can be reused deterministically. */
	void Drain(const std::uint8_t Port) noexcept { Mailboxes.Drain(Port); }

	/** Drops every queued packet on every port so the whole network can be reused deterministically. */
	void DrainAll() noexcept { Mailboxes.DrainAll(); }

private:
	/** The shared mailboxes; declared before Ports so it is fully constructed when ports bind. */
	Detail::FLoopbackMailboxes<MaxPorts, MailboxCapacity, PacketBytes> Mailboxes{};

	/** The N embedded per-port drivers handed out by Port(). */
	std::array<FPort, MaxPorts> Ports{};
};

} // namespace MicroWorld
