#include "TestSupport.h"
#include "E32LoraDeviceTestHost.h"

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Supply one valid peer frame through the byte stream and receive it through the public device.
 * Responsibilities: Success changes every receive.
 */
MW_TEST_CASE(E32LoraDeviceDeliversValidReceivedFrameTransactionally)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	std::uint8_t Frame[EncodedFrameCapacity] = {};
	std::size_t FrameBytes = 0;
	const ETransportResult EncodeResult = EncodePeerFrame(Payload, sizeof(Payload), Frame, FrameBytes);
	const bool bQueuedFrame = QueueFrame(Stream, Frame, FrameBytes);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)] = {SentinelByte, SentinelByte, SentinelByte};

	// Act
	const ETransportResult ReceiveOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const bool bPayloadMatches = DestinationMatches(Destination, Payload, sizeof(Payload));
	const bool bSenderMatches = AddressHasNodeId(From, PeerNodeId);
	const std::size_t ReceivedBytes = ReceiveResult.BytesReceived;

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, EncodeResult, "The valid peer frame fixture must encode");
	MW_EXPECT_EQ(Test, true, bQueuedFrame, "The fake UART must retain the complete valid frame fixture");
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The receive fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiveOutcome, "A valid queued frame must deliver successfully");
	MW_EXPECT_EQ(Test, true, bPayloadMatches, "Successful receive must copy every payload byte");
	MW_EXPECT_EQ(Test, true, bSenderMatches, "Successful receive must replace the sender output");
	MW_EXPECT_EQ(Test, sizeof(Payload), ReceivedBytes, "Successful receive must replace the byte count");
}

/**
 * Motivation: Poll an initialized device whose byte stream has no queued data.
 * Responsibilities: Unavailable leaves every caller-owned receive output.
 */
MW_TEST_CASE(E32LoraDeviceNoDataReceivePreservesOutputs)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)] = {SentinelByte, SentinelByte, SentinelByte};

	// Act
	const ETransportResult ReceiveOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const bool bDestinationPreserved = DestinationContains(Destination, sizeof(Destination), SentinelByte);
	const bool bFromPreserved = AddressHasNodeId(From, SentinelNodeId);
	const std::size_t PreservedByteCount = ReceiveResult.BytesReceived;
	const std::size_t ReadCalls = Stream.ReadCallCount();

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The no-data fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, ReceiveOutcome, "An empty byte stream must report Unavailable");
	MW_EXPECT_EQ(Test, true, bDestinationPreserved, "Unavailable receive must preserve destination bytes");
	MW_EXPECT_EQ(Test, true, bFromPreserved, "Unavailable receive must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, PreservedByteCount, "Unavailable receive must preserve the byte count");
	MW_EXPECT_EQ(Test, std::size_t{1}, ReadCalls, "An empty poll must stop after the first unavailable read");
}

/**
 * Motivation: Hold one decoded frame, reject a short and null destination, then retry with sufficient storage.
 * Responsibilities: Both rejections preserve caller outputs and retain the frame; the later valid retry delivers it.
 */
MW_TEST_CASE(E32LoraDeviceRetainsDecodedFrameAcrossFullAndInvalidDestinations)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	std::uint8_t Frame[EncodedFrameCapacity] = {};
	std::size_t FrameBytes = 0;
	const ETransportResult EncodeResult = EncodePeerFrame(Payload, sizeof(Payload), Frame, FrameBytes);
	const bool bQueuedFrame = QueueFrame(Stream, Frame, FrameBytes);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t ShortDestination[sizeof(Payload) - 1] = {SentinelByte, SentinelByte};

	// Act
	const ETransportResult FullOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(ShortDestination, sizeof(ShortDestination)), ReceiveResult);
	const bool bShortDestinationPreserved = DestinationContains(ShortDestination, sizeof(ShortDestination), SentinelByte);
	const bool bFromPreservedAfterFull = AddressHasNodeId(From, SentinelNodeId);
	const std::size_t ByteCountAfterFull = ReceiveResult.BytesReceived;
	const ETransportResult InvalidOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(nullptr, 1), ReceiveResult);
	const bool bFromPreservedAfterInvalid = AddressHasNodeId(From, SentinelNodeId);
	const std::size_t ByteCountAfterInvalid = ReceiveResult.BytesReceived;
	std::uint8_t Destination[sizeof(Payload)] = {};
	const ETransportResult RetryOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const bool bRetryPayloadMatches = DestinationMatches(Destination, Payload, sizeof(Payload));
	const bool bRetrySenderMatches = AddressHasNodeId(From, PeerNodeId);
	const std::size_t RetryByteCount = ReceiveResult.BytesReceived;

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, EncodeResult, "The retained-frame fixture must encode");
	MW_EXPECT_EQ(Test, true, bQueuedFrame, "The fake UART must queue the retained-frame fixture");
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The retained-frame device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Full, FullOutcome, "A too-small destination must report Full");
	MW_EXPECT_EQ(Test, true, bShortDestinationPreserved, "Full must preserve every short-destination byte");
	MW_EXPECT_EQ(Test, true, bFromPreservedAfterFull, "Full must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, ByteCountAfterFull, "Full must preserve the byte count");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, InvalidOutcome, "A null non-empty destination must report Invalid");
	MW_EXPECT_EQ(Test, true, bFromPreservedAfterInvalid, "Invalid must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, ByteCountAfterInvalid, "Invalid must preserve the byte count");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RetryOutcome, "A fitting retry must deliver the retained frame");
	MW_EXPECT_EQ(Test, true, bRetryPayloadMatches, "The fitting retry must copy the retained payload");
	MW_EXPECT_EQ(Test, true, bRetrySenderMatches, "The fitting retry must replace the sender output");
	MW_EXPECT_EQ(Test, sizeof(Payload), RetryByteCount, "The fitting retry must replace the byte count");
}

/**
 * Motivation: Feed a CRC-corrupt frame immediately followed by a valid peer frame.
 * Responsibilities: The corrupt candidate is discarded and the public device resynchronizes to deliver the later valid
 *   frame.
 */
MW_TEST_CASE(E32LoraDeviceResynchronizesAfterCorruptFrame)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	std::uint8_t CorruptFrame[EncodedFrameCapacity] = {};
	std::uint8_t ValidFrame[EncodedFrameCapacity] = {};
	std::size_t CorruptFrameBytes = 0;
	std::size_t ValidFrameBytes = 0;
	const ETransportResult CorruptEncodeResult = EncodePeerFrame(Payload, sizeof(Payload), CorruptFrame, CorruptFrameBytes);
	CorruptFrame[CorruptFrameBytes - 1] ^= 0x01u;
	const ETransportResult ValidEncodeResult = EncodePeerFrame(ReplacementPayload, sizeof(ReplacementPayload), ValidFrame, ValidFrameBytes);
	const bool bQueuedCorruptFrame = QueueFrame(Stream, CorruptFrame, CorruptFrameBytes);
	const bool bQueuedValidFrame = QueueFrame(Stream, ValidFrame, ValidFrameBytes);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)] = {SentinelByte, SentinelByte, SentinelByte};

	// Act
	const ETransportResult ReceiveOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const bool bPayloadMatches = DestinationMatches(Destination, ReplacementPayload, sizeof(ReplacementPayload));
	const bool bSenderMatches = AddressHasNodeId(From, PeerNodeId);
	const std::size_t ReceivedBytes = ReceiveResult.BytesReceived;

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, CorruptEncodeResult, "The corrupt-frame fixture must begin as a valid frame");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ValidEncodeResult, "The post-corruption frame fixture must encode");
	MW_EXPECT_EQ(Test, true, bQueuedCorruptFrame, "The fake UART must queue the corrupt candidate");
	MW_EXPECT_EQ(Test, true, bQueuedValidFrame, "The fake UART must queue the valid recovery frame");
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The corruption fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiveOutcome, "A valid frame after corruption must still deliver");
	MW_EXPECT_EQ(Test, true, bPayloadMatches, "Recovery must deliver the later valid payload bytes");
	MW_EXPECT_EQ(Test, true, bSenderMatches, "Recovery must deliver the later valid sender");
	MW_EXPECT_EQ(Test, sizeof(ReplacementPayload), ReceivedBytes, "Recovery must deliver the later valid byte count");
}

/**
 * Motivation: Queue exactly one receive budget of garbage before a complete valid frame and poll twice.
 * Responsibilities: The first poll consumes no more than the budget and preserves outputs; the second poll delivers the
 *   frame.
 */
MW_TEST_CASE(E32LoraDeviceCapsReceivePumpBeforeLaterFrameDelivery)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	std::uint8_t Frame[EncodedFrameCapacity] = {};
	std::size_t FrameBytes = 0;
	const ETransportResult EncodeResult = EncodePeerFrame(Payload, sizeof(Payload), Frame, FrameBytes);
	bool bQueuedGarbage = true;
	for (std::size_t ByteIndex = 0; ByteIndex < ReceivePumpByteCap; ++ByteIndex)
	{
		if (!Stream.QueueReceivedByte(0))
		{
			bQueuedGarbage = false;
		}
	}
	const bool bQueuedFrame = QueueFrame(Stream, Frame, FrameBytes);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)] = {SentinelByte, SentinelByte, SentinelByte};

	// Act
	const ETransportResult FirstReceiveOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const std::size_t FirstReadCalls = Stream.ReadCallCount();
	const bool bDestinationPreserved = DestinationContains(Destination, sizeof(Destination), SentinelByte);
	const bool bFromPreserved = AddressHasNodeId(From, SentinelNodeId);
	const std::size_t PreservedByteCount = ReceiveResult.BytesReceived;
	const ETransportResult SecondReceiveOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const std::size_t TotalReadCalls = Stream.ReadCallCount();
	const bool bDeliveredPayloadMatches = DestinationMatches(Destination, Payload, sizeof(Payload));
	const bool bDeliveredSenderMatches = AddressHasNodeId(From, PeerNodeId);
	const std::size_t DeliveredByteCount = ReceiveResult.BytesReceived;

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, EncodeResult, "The capped-receive frame fixture must encode");
	MW_EXPECT_EQ(Test, true, bQueuedGarbage, "The fake UART must queue the full garbage budget");
	MW_EXPECT_EQ(Test, true, bQueuedFrame, "The fake UART must queue the later valid frame");
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The capped-receive device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, FirstReceiveOutcome, "A frame beyond the receive budget must wait for another poll");
	MW_EXPECT_EQ(Test, ReceivePumpByteCap, FirstReadCalls, "One receive poll must not exceed the derived byte budget");
	MW_EXPECT_EQ(Test, true, bDestinationPreserved, "A budget-limited receive must preserve destination bytes");
	MW_EXPECT_EQ(Test, true, bFromPreserved, "A budget-limited receive must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, PreservedByteCount, "A budget-limited receive must preserve the byte count");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SecondReceiveOutcome, "The next receive poll must deliver the queued frame");
	MW_EXPECT_EQ(Test, ReceivePumpByteCap + FrameBytes, TotalReadCalls, "The second poll must consume only the valid frame bytes");
	MW_EXPECT_EQ(Test, true, bDeliveredPayloadMatches, "The later receive poll must copy every payload byte");
	MW_EXPECT_EQ(Test, true, bDeliveredSenderMatches, "The later receive poll must replace the sender output");
	MW_EXPECT_EQ(Test, sizeof(Payload), DeliveredByteCount, "The later receive poll must replace the byte count");
}

/**
 * Motivation: Make the UART return a hard read error while caller outputs contain sentinels.
 * Responsibilities: The device maps the error to Invalid and preserves every caller-owned receive output.
 */
MW_TEST_CASE(E32LoraDeviceReadErrorPreservesReceiveOutputs)
{
	// Arrange
	FFakeUartByteStream Stream;
	Stream.SetReadResult(EUartByteStreamResult::Error);
	FE32LoraDevice Device(Stream);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)] = {SentinelByte, SentinelByte, SentinelByte};

	// Act
	const ETransportResult ReceiveOutcome = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const bool bDestinationPreserved = DestinationContains(Destination, sizeof(Destination), SentinelByte);
	const bool bFromPreserved = AddressHasNodeId(From, SentinelNodeId);
	const std::size_t PreservedByteCount = ReceiveResult.BytesReceived;
	const std::size_t ReadCalls = Stream.ReadCallCount();

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The read-error fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, ReceiveOutcome, "A hard UART read error must map to Invalid");
	MW_EXPECT_EQ(Test, true, bDestinationPreserved, "Read error must preserve destination bytes");
	MW_EXPECT_EQ(Test, true, bFromPreserved, "Read error must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, PreservedByteCount, "Read error must preserve the byte count");
	MW_EXPECT_EQ(Test, std::size_t{1}, ReadCalls, "A hard read error must stop the receive poll immediately");
}

/**
 * Motivation: Queue a packet on one initialized public device, advance it over a fake UART, and feed those exact
 *   bytes to another public device.
 * Responsibilities: The receiving device decodes the real emitted frame and delivers the original sender, payload, and
 *   byte count.
 */
MW_TEST_CASE(E32LoraDevicesExchangeOneRealEncodedFrame)
{
	// Arrange
	FFakeUartByteStream SenderStream;
	FFakeUartByteStream ReceiverStream;
	FE32LoraDevice Sender(SenderStream);
	FE32LoraDevice Receiver(ReceiverStream);
	const FDeviceAddress DestinationAddress = MakeLoraAddress(PeerNodeId);
	const ETransportResult SenderInitializeResult = Sender.Initialize(LocalNodeId);
	const ETransportResult ReceiverInitializeResult = Receiver.Initialize(PeerNodeId);
	const ETransportResult SendResult = Sender.TrySend(DestinationAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	// Act
	Sender.PreAdvance(PumpTimeMilliseconds);
	const std::size_t EmittedBytes = SenderStream.WrittenByteCount();
	bool bCopiedEveryByte = true;
	for (std::size_t ByteIndex = 0; ByteIndex < EmittedBytes; ++ByteIndex)
	{
		const std::uint8_t EmittedByte = SenderStream.WrittenByteAt(ByteIndex);
		if (!ReceiverStream.QueueReceivedByte(EmittedByte))
		{
			bCopiedEveryByte = false;
		}
	}
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)] = {};
	const ETransportResult ReceiveOutcome = Receiver.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const bool bPayloadMatches = DestinationMatches(Destination, Payload, sizeof(Payload));
	const bool bSenderMatches = AddressHasNodeId(From, LocalNodeId);
	const std::size_t ReceivedBytes = ReceiveResult.BytesReceived;

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, SenderInitializeResult, "The sending device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiverInitializeResult, "The receiving device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "The sending device must accept the packet");
	MW_EXPECT_EQ(Test, sizeof(Payload) + FrameOverheadBytes, EmittedBytes, "One advance must emit the complete real encoded frame");
	MW_EXPECT_EQ(Test, true, bCopiedEveryByte, "The receiving fake UART must accept every emitted frame byte");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReceiveOutcome, "The receiving device must decode the transmitted frame");
	MW_EXPECT_EQ(Test, true, bPayloadMatches, "The receiving device must deliver the original payload");
	MW_EXPECT_EQ(Test, true, bSenderMatches, "The receiving device must report the sending local node id");
	MW_EXPECT_EQ(Test, sizeof(Payload), ReceivedBytes, "The receiving device must report the original payload size");
}

} // namespace
