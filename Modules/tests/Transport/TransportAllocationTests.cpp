#include "CoreAllocationCounters.h"
#include "TestSupport.h"

#include <MicroWorld/Core/Containers/Span.h>
#include <MicroWorld/Core/IO/DeviceAddress.h>
#include <MicroWorld/Core/IO/ReceiveResult.h>
#include <MicroWorld/Core/IO/TransportResult.h>
#include <MicroWorld/Transport/ByteReader.h>
#include <MicroWorld/Transport/ByteWriter.h>
#include <MicroWorld/Transport/HostLoopback.h>
#include <MicroWorld/Transport/TransportManager.h>
#include <MicroWorld/Transport/TransportPacketStorage.h>
#include <MicroWorld/Transport/EControlMessageType.h>
#include <MicroWorld/Transport/FControlMessage.h>
#include <MicroWorld/Transport/FMessageHeader.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::Core::ETransportResult;
using MicroWorld::Core::FDeviceAddress;
using MicroWorld::Core::FReceiveResult;
using MicroWorld::Core::MakeLoopbackAddress;
using MicroWorld::Core::TSpan;
using MicroWorld::Tests::GlobalAllocationCount;
using MicroWorld::Transport::EControlMessageType;
using MicroWorld::Transport::FByteReader;
using MicroWorld::Transport::FByteWriter;
using MicroWorld::Transport::FControlMessage;
using MicroWorld::Transport::FMessageHeader;
using MicroWorld::Transport::ReadControlMessage;
using MicroWorld::Transport::ReadMessage;
using MicroWorld::Transport::THostLoopback;
using MicroWorld::Transport::TTransportManager;
using MicroWorld::Transport::TTransportPacketStorage;
using MicroWorld::Transport::WriteControlMessage;
using MicroWorld::Transport::WriteMessage;

/** Motivation: Per-buffer byte capacity of the writer/reader storage exercised by the allocation test. */
constexpr std::size_t BufferByteCount = 8;
/** Motivation: Capacity of the framing buffer that lives outside the counted region. */
constexpr std::size_t FramingBufferCapacity = 16;
/** Motivation: Sentinel value pre-loaded into BytesReceived so an unchanged failed receive is observable. */
constexpr std::size_t UntouchedBytesReceivedSentinel = 0xEE;
/** Motivation: Loopback port index whose mailbox receives the single-link FIFO traffic. */
constexpr std::uint8_t SourcePort = 0;
/** Motivation: Number of mailboxes the two-port loopback exposes for the single-link FIFO. */
constexpr std::size_t LoopbackPortCount = 2;
/** Motivation: Mailbox slot depth the loopback exposes for the single-link FIFO. */
constexpr std::size_t LoopbackMailboxDepth = 2;
/** Motivation: Per-packet byte capacity the loopback exposes for the single-link FIFO. */
constexpr std::size_t LoopbackPacketBytes = 8;
/** Motivation: One-slot packet storage capacity that forces a full-FIFO queue attempt. */
constexpr std::size_t FullQueueSlotCount = 1;
/** Motivation: Four-byte packet capacity used by the full-FIFO storage and the unavailable-receive destination. */
constexpr std::size_t FourBytePacketCapacity = 4;
/** Motivation: Channel byte the message-framing path writes into the application message header. */
constexpr std::uint8_t ApplicationChannel = 7;
/** Motivation: Base value added to each writer index so every written byte is distinct and observable. */
constexpr std::uint8_t WrittenByteBase = 0xA0;
/** Motivation: Welcome protocol version the message-framing control path encodes. */
constexpr std::uint8_t WelcomeProtocolVersion = 1;
/** Motivation: Welcome peer index the message-framing control path encodes. */
constexpr std::uint8_t WelcomePeerIndex = 2;
/** Motivation: Welcome peer generation the message-framing control path encodes. */
constexpr std::uint8_t WelcomePeerGeneration = 3;

/** Motivation: Eight distinct source bytes the reader consumes in order. */
constexpr std::uint8_t ReaderSourceBytes[BufferByteCount] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};
/** Motivation: Three-byte payload the framing path writes as an application message. */
constexpr std::uint8_t FramingPayloadBytes[3] = {0xC0, 0xC1, 0xC2};
/** Motivation: Four-byte first packet queued into the manager FIFO. */
constexpr std::uint8_t FirstManagerPacket[4] = {0x01, 0x02, 0x03, 0x04};
/** Motivation: Four-byte second packet queued into the manager FIFO. */
constexpr std::uint8_t SecondManagerPacket[4] = {0x05, 0x06, 0x07, 0x08};
/** Motivation: Two-byte packet the full-FIFO path queues twice into a one-slot manager. */
constexpr std::uint8_t FullFifoPacket[2] = {0xAA, 0xBB};

/**
 * Motivation: Scenario: Capture the allocation counter after construction, then drive byte writer/reader
 *   operations, manager queue/send-advance/receive, loopback delivery, full, unavailable, drain, and
 *   reuse paths, and message-framing control round trips.
 * Responsibilities: Expected: Every steady-state Transport path performs no observable heap allocation, proving the
 *   bounded fixed-storage contract holds across the public API.
 */
MW_TEST_CASE(TransportOperationsPerformNoObservableAllocation)
{
	// Arrange
	std::uint8_t WriterStorage[BufferByteCount] = {};

	FByteWriter Writer(TSpan<std::uint8_t>(WriterStorage, sizeof(WriterStorage)));
	FByteReader Reader(TSpan<const std::uint8_t>(ReaderSourceBytes, sizeof(ReaderSourceBytes)));
	// A two-port loopback with port 0 sending to its own mailbox reproduces the single-link FIFO.
	THostLoopback<LoopbackPortCount, LoopbackMailboxDepth, LoopbackPacketBytes> Loopback;
	const FDeviceAddress Port0 = MakeLoopbackAddress(SourcePort);
	TTransportPacketStorage<2, 8> ManagerStorage;
	TTransportManager<2, 8> Manager(Loopback.Port(SourcePort), ManagerStorage);
	// Framing buffers live outside the counted region so only steady-state framing work is measured.
	std::uint8_t FramingBuffer[FramingBufferCapacity] = {};

	// Capture the counter only after every fixed-storage object is constructed.
	const std::uint32_t AllocationsBefore = GlobalAllocationCount;

	// Act - exercise the steady-state writer/reader path.
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

	// Act - exercise the manager queue/send-advance/receive path.
	(void)Manager.QueueSend(Port0, TSpan<const std::uint8_t>(FirstManagerPacket, sizeof(FirstManagerPacket)));
	(void)Manager.QueueSend(Port0, TSpan<const std::uint8_t>(SecondManagerPacket, sizeof(SecondManagerPacket)));

	(void)Manager.AdvanceSend();
	(void)Manager.AdvanceSend();

	std::uint8_t ReceiveDestination[BufferByteCount] = {};
	FReceiveResult FirstReceive{};
	FDeviceAddress FirstFrom{};
	(void)Manager.Receive(FirstFrom, TSpan<std::uint8_t>(ReceiveDestination, sizeof(ReceiveDestination)), FirstReceive);
	FReceiveResult SecondReceive{};
	FDeviceAddress SecondFrom{};
	(void)Manager.Receive(SecondFrom, TSpan<std::uint8_t>(ReceiveDestination, sizeof(ReceiveDestination)), SecondReceive);

	// Act - exercise the empty and full paths: advance an empty FIFO and queue into a full one.
	(void)Manager.AdvanceSend();
	TTransportPacketStorage<FullQueueSlotCount, FourBytePacketCapacity> FullManagerStorage;
	TTransportManager<FullQueueSlotCount, FourBytePacketCapacity> FullManager(Loopback.Port(SourcePort), FullManagerStorage);
	(void)FullManager.QueueSend(Port0, TSpan<const std::uint8_t>(FullFifoPacket, sizeof(FullFifoPacket)));
	(void)FullManager.QueueSend(Port0, TSpan<const std::uint8_t>(FullFifoPacket, sizeof(FullFifoPacket)));

	// Act - exercise drain and capacity reuse on the loopback.
	Loopback.Drain(SourcePort);
	(void)Loopback.Port(SourcePort).TrySend(Port0, TSpan<const std::uint8_t>(FullFifoPacket, sizeof(FullFifoPacket)));

	// Act - exercise the unavailable receive path on a drained loopback.
	FReceiveResult EmptyReceive{UntouchedBytesReceivedSentinel};
	std::uint8_t EmptyDestination[FourBytePacketCapacity] = {};
	FDeviceAddress EmptyFrom{};
	(void)Loopback.Port(SourcePort).TryReceive(EmptyFrom, TSpan<std::uint8_t>(EmptyDestination, sizeof(EmptyDestination)), EmptyReceive);

	// Act - exercise the message-framing path: write/read an application message and a control message.
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
	// Assert
	MW_EXPECT_EQ(Test, AllocationsBefore, AllocationsAfter, "Steady-state Transport operations must not allocate");
}

} // namespace
