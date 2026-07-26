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

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::EControlMessageType;
using MicroWorld::ENetResult;
using MicroWorld::FByteReader;
using MicroWorld::FByteWriter;
using MicroWorld::FControlMessage;
using MicroWorld::FMessageHeader;
using MicroWorld::FNetAddress;
using MicroWorld::FNetReceiveResult;
using MicroWorld::INetDriver;
using MicroWorld::MakeLoopbackAddress;
using MicroWorld::ReadControlMessage;
using MicroWorld::ReadMessage;
using MicroWorld::THostLoopback;
using MicroWorld::TNetManager;
using MicroWorld::TNetPacketStorage;
using MicroWorld::TSpan;
using MicroWorld::WriteControlMessage;
using MicroWorld::WriteMessage;
using MicroWorld::Tests::GlobalAllocationCount;

/** Per-buffer byte capacity of the writer/reader storage exercised by the allocation test. */
constexpr std::size_t BufferByteCount = 8;
/** Capacity of the framing buffer that lives outside the counted region. */
constexpr std::size_t FramingBufferCapacity = 16;
/** Sentinel value pre-loaded into BytesReceived so an unchanged failed receive is observable. */
constexpr std::size_t UntouchedBytesReceivedSentinel = 0xEE;
/** Loopback port index whose mailbox receives the single-link FIFO traffic. */
constexpr std::uint8_t SourcePort = 0;
/** Number of mailboxes the two-port loopback exposes for the single-link FIFO. */
constexpr std::size_t LoopbackPortCount = 2;
/** Mailbox slot depth the loopback exposes for the single-link FIFO. */
constexpr std::size_t LoopbackMailboxDepth = 2;
/** Per-packet byte capacity the loopback exposes for the single-link FIFO. */
constexpr std::size_t LoopbackPacketBytes = 8;
/** One-slot packet storage capacity that forces a full-FIFO queue attempt. */
constexpr std::size_t FullQueueSlotCount = 1;
/** Four-byte packet capacity used by the full-FIFO storage and the unavailable-receive destination. */
constexpr std::size_t FourBytePacketCapacity = 4;
/** Channel byte the message-framing path writes into the application message header. */
constexpr std::uint8_t ApplicationChannel = 7;
/** Base value added to each writer index so every written byte is distinct and observable. */
constexpr std::uint8_t WrittenByteBase = 0xA0;
/** Welcome protocol version the message-framing control path encodes. */
constexpr std::uint8_t WelcomeProtocolVersion = 1;
/** Welcome peer index the message-framing control path encodes. */
constexpr std::uint8_t WelcomePeerIndex = 2;
/** Welcome peer generation the message-framing control path encodes. */
constexpr std::uint8_t WelcomePeerGeneration = 3;

/** Eight distinct source bytes the reader consumes in order. */
constexpr std::uint8_t ReaderSourceBytes[BufferByteCount] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
/** Three-byte payload the framing path writes as an application message. */
constexpr std::uint8_t FramingPayloadBytes[3] = {0xC0, 0xC1, 0xC2};
/** Four-byte first packet queued into the manager FIFO. */
constexpr std::uint8_t FirstManagerPacket[4] = {0x01, 0x02, 0x03, 0x04};
/** Four-byte second packet queued into the manager FIFO. */
constexpr std::uint8_t SecondManagerPacket[4] = {0x05, 0x06, 0x07, 0x08};
/** Two-byte packet the full-FIFO path queues twice into a one-slot manager. */
constexpr std::uint8_t FullFifoPacket[2] = {0xAA, 0xBB};

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
	std::uint8_t WriterStorage[BufferByteCount] = {};

	FByteWriter Writer(TSpan<std::uint8_t>(WriterStorage, sizeof(WriterStorage)));
	FByteReader Reader(TSpan<const std::uint8_t>(ReaderSourceBytes, sizeof(ReaderSourceBytes)));
	// A two-port loopback with port 0 sending to its own mailbox reproduces the single-link FIFO.
	THostLoopback<LoopbackPortCount, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	const FNetAddress Port0 = MakeLoopbackAddress(SourcePort);
	TNetPacketStorage<2, 8> ManagerStorage;
	TNetManager<2, 8> Manager(Loopback.Port(SourcePort), ManagerStorage);
	// Framing buffers live outside the counted region so only steady-state framing work is measured.
	std::uint8_t FramingBuffer[FramingBufferCapacity] = {};

	// Capture the counter only after every fixed-storage object is constructed.
	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	for (std::size_t Index = 0; Index < sizeof(WriterStorage); ++Index)
	{
		Writer.WriteByte(static_cast<std::uint8_t>(WrittenByteBase + Index));
	}
	const TSpan<const std::uint8_t> WrittenView = Writer.WrittenBytes();
	for (std::size_t Index = 0; Index < WrittenView.Size(); ++Index)
	{
		std::uint8_t Byte = 0;
		Reader.ReadByte(Byte);
	}

	(void)Manager.QueueSend(Port0, TSpan<const std::uint8_t>(FirstManagerPacket, sizeof(FirstManagerPacket)));
	(void)Manager.QueueSend(Port0, TSpan<const std::uint8_t>(SecondManagerPacket, sizeof(SecondManagerPacket)));

	(void)Manager.AdvanceSend();
	(void)Manager.AdvanceSend();

	std::uint8_t ReceiveDestination[BufferByteCount] = {};
	FNetReceiveResult FirstReceive{};
	FNetAddress FirstFrom{};
	(void)Manager.Receive(FirstFrom, TSpan<std::uint8_t>(ReceiveDestination, sizeof(ReceiveDestination)), FirstReceive);
	FNetReceiveResult SecondReceive{};
	FNetAddress SecondFrom{};
	(void)Manager.Receive(SecondFrom, TSpan<std::uint8_t>(ReceiveDestination, sizeof(ReceiveDestination)), SecondReceive);

	// Exercise the empty and full paths: advance an empty FIFO and queue into a full one.
	(void)Manager.AdvanceSend();
	TNetPacketStorage<FullQueueSlotCount, FourBytePacketCapacity> FullManagerStorage;
	TNetManager<FullQueueSlotCount, FourBytePacketCapacity> FullManager(Loopback.Port(SourcePort), FullManagerStorage);
	(void)FullManager.QueueSend(Port0, TSpan<const std::uint8_t>(FullFifoPacket, sizeof(FullFifoPacket)));
	(void)FullManager.QueueSend(Port0, TSpan<const std::uint8_t>(FullFifoPacket, sizeof(FullFifoPacket)));

	// Exercise drain and capacity reuse on the loopback.
	Loopback.Drain(SourcePort);
	(void)Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(FullFifoPacket, sizeof(FullFifoPacket)));

	// Exercise the unavailable receive path on a drained loopback.
	FNetReceiveResult EmptyReceive{UntouchedBytesReceivedSentinel};
	std::uint8_t EmptyDestination[FourBytePacketCapacity] = {};
	FNetAddress EmptyFrom{};
	(void)Loopback.Port(SourcePort).TryReceive(EmptyFrom, TSpan<std::uint8_t>(EmptyDestination, sizeof(EmptyDestination)), EmptyReceive);

	// Exercise the message-framing path: write/read an application message and a control message.
	FByteWriter FrameWriter(TSpan<std::uint8_t>(FramingBuffer, sizeof(FramingBuffer)));
	(void)WriteMessage(FrameWriter, ApplicationChannel, TSpan<const std::uint8_t>(FramingPayloadBytes, sizeof(FramingPayloadBytes)));
	FMessageHeader FrameHeader{};
	TSpan<const std::uint8_t> FramePayload{};
	(void)ReadMessage(FrameWriter.WrittenBytes(), FrameHeader, FramePayload);

	FByteWriter ControlWriter(TSpan<std::uint8_t>(FramingBuffer, sizeof(FramingBuffer)));
	FControlMessage Outgoing{};
	Outgoing.Type = EControlMessageType::Welcome;
	Outgoing.ProtocolVersion = WelcomeProtocolVersion;
	Outgoing.PeerIndex = WelcomePeerIndex;
	Outgoing.PeerGeneration = WelcomePeerGeneration;
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
