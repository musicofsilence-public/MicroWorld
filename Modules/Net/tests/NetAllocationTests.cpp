#include "NetAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Containers/Span.h>
#include <MicroWorld/Net/ByteReader.h>
#include <MicroWorld/Net/ByteWriter.h>
#include <MicroWorld/Net/HostLoopback.h>
#include <MicroWorld/Net/NetAddress.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetManager.h>
#include <MicroWorld/Net/NetPacketStorage.h>
#include <MicroWorld/Net/NetProtocol.h>
#include <MicroWorld/Net/NetResult.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::EControlMessageType;
using MicroWorld::ENetResult;
using MicroWorld::FByteReader;
using MicroWorld::FByteWriter;
using MicroWorld::FControlMessage;
using MicroWorld::FHostLoopback;
using MicroWorld::FMessageHeader;
using MicroWorld::FNetAddress;
using MicroWorld::FNetManager;
using MicroWorld::FNetPacketStorage;
using MicroWorld::FNetReceiveResult;
using MicroWorld::INetDriver;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::ReadControlMessage;
using MicroWorld::ReadMessage;
using MicroWorld::TSpan;
using MicroWorld::WriteControlMessage;
using MicroWorld::WriteMessage;
using MicroWorld::Tests::GlobalAllocationCount;

/**
 * Exercises every steady-state Net path and proves none of them allocate.
 *
 * The test captures the allocation counter after construction, then drives byte
 * writer/reader operations, manager queue/send-advance/receive, and loopback
 * delivery, full, unavailable, drain, and reuse paths. A steady-state delta of
 * zero proves the bounded fixed-storage contract holds across the public API.
 */
MW_TEST_CASE(NetOperationsPerformNoObservableAllocation)
{
	std::array<std::uint8_t, 8> WriterStorage{};
	const std::array<std::uint8_t, 8> Source{0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

	FByteWriter Writer(TSpan<std::uint8_t>(WriterStorage.data(), WriterStorage.size()));
	FByteReader Reader(TSpan<const std::uint8_t>(Source.data(), Source.size()));
	// A two-port loopback with port 0 sending to its own mailbox reproduces the single-link FIFO.
	FHostLoopback<2, 2, 8> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(0);
	FNetPacketStorage<2, 8> ManagerStorage;
	FNetManager<2, 8> Manager(Loopback.Port(0), ManagerStorage);
	// Framing buffers live outside the counted region so only steady-state framing work is measured.
	std::array<std::uint8_t, 16> FramingBuffer{};
	const std::array<std::uint8_t, 3> FramingPayload{0xC0, 0xC1, 0xC2};

	// Capture the counter only after every fixed-storage object is constructed.
	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	for (std::size_t Index = 0; Index < WriterStorage.size(); ++Index)
	{
		Writer.WriteByte(static_cast<std::uint8_t>(0xA0 + Index));
	}
	const TSpan<const std::uint8_t> WrittenView = Writer.WrittenBytes();
	for (std::size_t Index = 0; Index < WrittenView.Size(); ++Index)
	{
		std::uint8_t Byte = 0;
		Reader.ReadByte(Byte);
	}

	const std::array<std::uint8_t, 4> FirstPacket{0x01, 0x02, 0x03, 0x04};
	const std::array<std::uint8_t, 4> SecondPacket{0x05, 0x06, 0x07, 0x08};
	(void)Manager.QueueSend(Port0, TSpan<const std::uint8_t>(FirstPacket.data(), FirstPacket.size()));
	(void)Manager.QueueSend(Port0, TSpan<const std::uint8_t>(SecondPacket.data(), SecondPacket.size()));

	(void)Manager.AdvanceSend();
	(void)Manager.AdvanceSend();

	std::array<std::uint8_t, 8> ReceiveDestination{};
	FNetReceiveResult FirstReceive{};
	FNetAddress FirstFrom{};
	(void)Manager.Receive(FirstFrom, TSpan<std::uint8_t>(ReceiveDestination.data(), ReceiveDestination.size()), FirstReceive);
	FNetReceiveResult SecondReceive{};
	FNetAddress SecondFrom{};
	(void)Manager.Receive(SecondFrom, TSpan<std::uint8_t>(ReceiveDestination.data(), ReceiveDestination.size()), SecondReceive);

	// Exercise the empty and full paths: advance an empty FIFO and queue into a full one.
	(void)Manager.AdvanceSend();
	FNetPacketStorage<1, 4> FullManagerStorage;
	FNetManager<1, 4> FullManager(Loopback.Port(0), FullManagerStorage);
	const std::array<std::uint8_t, 2> Packet{0xAA, 0xBB};
	(void)FullManager.QueueSend(Port0, TSpan<const std::uint8_t>(Packet.data(), Packet.size()));
	(void)FullManager.QueueSend(Port0, TSpan<const std::uint8_t>(Packet.data(), Packet.size()));

	// Exercise drain and capacity reuse on the loopback.
	Loopback.Drain(0);
	(void)Loopback.Port(0).TrySend(Port0, TSpan<const std::uint8_t>(Packet.data(), Packet.size()));

	// Exercise the unavailable receive path on a drained loopback.
	FNetReceiveResult EmptyReceive{std::size_t{0xEE}};
	std::array<std::uint8_t, 4> EmptyDestination{};
	FNetAddress EmptyFrom{};
	(void)Loopback.Port(0).TryReceive(EmptyFrom, TSpan<std::uint8_t>(EmptyDestination.data(), EmptyDestination.size()), EmptyReceive);

	// Exercise the message-framing path: write/read an application message and a control message.
	FByteWriter FrameWriter(TSpan<std::uint8_t>(FramingBuffer.data(), FramingBuffer.size()));
	(void)WriteMessage(FrameWriter, std::uint8_t{7}, TSpan<const std::uint8_t>(FramingPayload.data(), FramingPayload.size()));
	FMessageHeader FrameHeader{};
	TSpan<const std::uint8_t> FramePayload{};
	(void)ReadMessage(FrameWriter.WrittenBytes(), FrameHeader, FramePayload);

	FByteWriter ControlWriter(TSpan<std::uint8_t>(FramingBuffer.data(), FramingBuffer.size()));
	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Welcome;
	Outgoing.ProtocolVersion = 1;
	Outgoing.PeerIndex = 2;
	Outgoing.PeerGeneration = 3;
	(void)WriteControlMessage(ControlWriter, Outgoing);
	FMessageHeader ControlHeader{};
	TSpan<const std::uint8_t> ControlPayload{};
	(void)ReadMessage(ControlWriter.WrittenBytes(), ControlHeader, ControlPayload);
	FControlMessage DecodedControl{};
	(void)ReadControlMessage(ControlPayload, DecodedControl);

	const std::uint32_t AllocationsAfter = GlobalAllocationCount;
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "Steady-state Net operations must not allocate");
}

} // namespace
