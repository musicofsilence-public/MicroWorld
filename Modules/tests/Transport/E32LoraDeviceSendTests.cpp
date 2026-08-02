#include "TestSupport.h"
#include "E32LoraDeviceTestHost.h"

#include <MicroWorld/Transport/FrameCodec.h>

#include <cstddef>
#include <cstdint>

namespace
{

using namespace ::MicroWorld::Tests;

/**
 * Motivation: Use send and receive before initialization, then initialize twice.
 * Responsibilities: The device remains inert before initialization, initializes once without UART I/O, and rejects a
 *   later initialization.
 */
MW_TEST_CASE(E32LoraDeviceRemainsInertUntilSingleShotInitialization)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	const FDeviceAddress DestinationAddress = MakeLoraAddress(PeerNodeId);
	FDeviceAddress From = MakeLoraAddress(SentinelNodeId);
	FReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)] = {SentinelByte, SentinelByte, SentinelByte};

	// Act
	const bool bInitiallyInitialized = Device.IsInitialized();
	const ETransportResult SendBeforeInitialize = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	const ETransportResult ReceiveBeforeInitialize = Device.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	Device.PreAdvance(PumpTimeMilliseconds);
	const ETransportResult FirstInitializeResult = Device.Initialize(LocalNodeId);
	const ETransportResult SecondInitializeResult = Device.Initialize(LocalNodeId);
	const bool bInitializedAfterFirstCall = Device.IsInitialized();
	const std::size_t ReadCalls = Stream.ReadCallCount();
	const std::size_t WriteCalls = Stream.WriteCallCount();
	const bool bDestinationPreserved = DestinationContains(Destination, sizeof(Destination), SentinelByte);
	const bool bFromPreserved = AddressHasNodeId(From, SentinelNodeId);
	const std::size_t PreservedByteCount = ReceiveResult.BytesReceived;

	// Assert
	MW_EXPECT_EQ(Test, false, bInitiallyInitialized, "Construction must leave the device uninitialized");
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, SendBeforeInitialize, "Send before initialization must be unavailable");
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, ReceiveBeforeInitialize, "Receive before initialization must be unavailable");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstInitializeResult, "The first initialization must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, SecondInitializeResult, "The second initialization must be unavailable");
	MW_EXPECT_EQ(Test, true, bInitializedAfterFirstCall, "Successful initialization must make the device usable");
	MW_EXPECT_EQ(Test, std::size_t{0}, ReadCalls, "Construction and initialization must not read the UART");
	MW_EXPECT_EQ(Test, std::size_t{0}, WriteCalls, "Construction and initialization must not write the UART");
	MW_EXPECT_EQ(Test, true, bDestinationPreserved, "Unavailable receive must preserve destination bytes");
	MW_EXPECT_EQ(Test, true, bFromPreserved, "Unavailable receive must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, PreservedByteCount, "Unavailable receive must preserve the byte count");
}

/**
 * Motivation: Send malformed, null, and oversize payloads before sending valid normal, empty, and maximum packets.
 * Responsibilities: Invalid sends do not occupy the slot; every documented valid payload boundary is accepted.
 */
MW_TEST_CASE(E32LoraDeviceValidatesSendInputsAndAcceptsPayloadBoundaries)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	FDeviceAddress InvalidAddress{};
	const FDeviceAddress DestinationAddress = MakeLoraAddress(PeerNodeId);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);

	// Act
	const ETransportResult InvalidAddressResult = Device.TrySend(InvalidAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	const ETransportResult NullPayloadResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(nullptr, 1));
	const ETransportResult OversizePayloadResult =
		Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(&OversizePayloadByte, E32MaxPayloadBytes + 1));
	const ETransportResult ValidPayloadResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	Device.PreAdvance(PumpTimeMilliseconds);
	const std::size_t ValidFrameWrittenBytes = Stream.WrittenByteCount();
	const ETransportResult EmptyPayloadResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(nullptr, 0));
	Device.PreAdvance(PumpTimeMilliseconds);
	const std::size_t EmptyFrameWrittenBytes = Stream.WrittenByteCount();
	const ETransportResult MaximumPayloadResult =
		Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(MaximumPayload, sizeof(MaximumPayload)));
	const std::size_t MaximumPacketBytes = Device.MaxPacketBytes();

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The send fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, InvalidAddressResult, "A malformed E32 destination must be rejected");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, NullPayloadResult, "A null non-empty payload must be rejected");
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, OversizePayloadResult, "A payload over the E32 limit must be rejected");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ValidPayloadResult, "A valid payload after rejected sends must be accepted");
	MW_EXPECT_EQ(
		Test, sizeof(Payload) + FrameOverheadBytes, ValidFrameWrittenBytes, "The pre-advance turn must emit the accepted valid payload frame");
	MW_EXPECT_EQ(Test, ETransportResult::Success, EmptyPayloadResult, "An empty payload must be accepted");
	MW_EXPECT_EQ(
		Test, sizeof(Payload) + (2u * FrameOverheadBytes), EmptyFrameWrittenBytes, "The pre-advance turn must emit the accepted empty payload frame");
	MW_EXPECT_EQ(Test, ETransportResult::Success, MaximumPayloadResult, "The maximum E32 payload must be accepted");
	MW_EXPECT_EQ(Test, E32MaxPayloadBytes, MaximumPacketBytes, "MaxPacketBytes must expose the E32 payload limit");
}

/**
 * Motivation: Queue one maximum packet, attempt a second while occupied, then advance once through an
 *   always-writable UART.
 * Responsibilities: The second send reports Full; one advance drains the complete fixed frame within its bounded byte
 *   budget and frees the slot.
 */
MW_TEST_CASE(E32LoraDeviceAppliesBackpressureAndDrainsMaximumFrameInOneAdvance)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	const FDeviceAddress DestinationAddress = MakeLoraAddress(PeerNodeId);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	const ETransportResult FirstSendResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(MaximumPayload, sizeof(MaximumPayload)));
	const ETransportResult FullResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	// Act
	Device.PreAdvance(PumpTimeMilliseconds);
	const std::size_t WrittenBytes = Stream.WrittenByteCount();
	const std::size_t WriteCalls = Stream.WriteCallCount();
	const ETransportResult ReuseResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The transmit fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstSendResult, "An empty transmit slot must accept one maximum frame");
	MW_EXPECT_EQ(Test, ETransportResult::Full, FullResult, "A queued frame must apply one-frame backpressure");
	MW_EXPECT_EQ(Test, EncodedFrameCapacity, WrittenBytes, "One advance must drain every byte of the maximum encoded frame");
	MW_EXPECT_EQ(Test, EncodedFrameCapacity, WriteCalls, "One advance must attempt no more than the fixed encoded-frame capacity");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReuseResult, "Draining the final frame byte must free the transmit slot");
}

/**
 * Motivation: Queue a frame, block the first write, then make the UART writable and advance again.
 * Responsibilities: The blocked byte remains queued and becomes the first successfully emitted byte on retry.
 */
MW_TEST_CASE(E32LoraDeviceRetainsCurrentByteWhenWriteIsUnavailable)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	const FDeviceAddress DestinationAddress = MakeLoraAddress(PeerNodeId);
	std::uint8_t ExpectedFrame[EncodedFrameCapacity] = {};
	std::size_t ExpectedFrameBytes = 0;
	const ETransportResult EncodeResult = MicroWorld::Transport::FrameCodec::EncodeFrame(
		LocalNodeId,
		TSpan<const std::uint8_t>(Payload, sizeof(Payload)),
		TSpan<std::uint8_t>(ExpectedFrame, sizeof(ExpectedFrame)),
		ExpectedFrameBytes);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	const ETransportResult SendResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	Stream.SetWriteResult(EUartByteStreamResult::Unavailable);

	// Act
	Device.PreAdvance(PumpTimeMilliseconds);
	const std::size_t BlockedWriteCalls = Stream.WriteCallCount();
	const std::size_t BlockedWrittenBytes = Stream.WrittenByteCount();
	const ETransportResult FullWhileBlockedResult =
		Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(ReplacementPayload, sizeof(ReplacementPayload)));
	Stream.SetWriteResult(EUartByteStreamResult::Success);
	Device.PreAdvance(PumpTimeMilliseconds);
	const std::size_t WrittenBytesAfterRetry = Stream.WrittenByteCount();
	const std::uint8_t FirstWrittenByte = Stream.WrittenByteAt(0);
	const ETransportResult ReuseResult =
		Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(ReplacementPayload, sizeof(ReplacementPayload)));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, EncodeResult, "The expected wire frame fixture must encode");
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The blocked-write fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Success, SendResult, "The first frame must queue before the write blocks");
	MW_EXPECT_EQ(Test, std::size_t{1}, BlockedWriteCalls, "A blocked advance must attempt the current byte once");
	MW_EXPECT_EQ(Test, std::size_t{0}, BlockedWrittenBytes, "A blocked write must not consume the current byte");
	MW_EXPECT_EQ(Test, ETransportResult::Full, FullWhileBlockedResult, "A blocked byte must keep the transmit slot occupied");
	MW_EXPECT_EQ(Test, ExpectedFrameBytes, WrittenBytesAfterRetry, "The retry must drain the original complete frame");
	MW_EXPECT_EQ(Test, ExpectedFrame[0], FirstWrittenByte, "The retry must begin with the byte blocked on the earlier advance");
	MW_EXPECT_EQ(Test, ETransportResult::Success, ReuseResult, "The slot must release after the retained frame drains");
}

/**
 * Motivation: Queue a frame, make the first UART write fail permanently, then queue another frame.
 * Responsibilities: The failed frame is discarded so the later send is accepted instead of remaining Full forever.
 */
MW_TEST_CASE(E32LoraDeviceDiscardsQueuedFrameAfterWriteError)
{
	// Arrange
	FFakeUartByteStream Stream;
	FE32LoraDevice Device(Stream);
	const FDeviceAddress DestinationAddress = MakeLoraAddress(PeerNodeId);
	const ETransportResult InitializeResult = Device.Initialize(LocalNodeId);
	const ETransportResult FirstSendResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	Stream.SetSuccessfulWriteLimit(2);

	// Act
	Device.PreAdvance(PumpTimeMilliseconds);
	const std::size_t WriteCalls = Stream.WriteCallCount();
	const std::size_t WrittenBytes = Stream.WrittenByteCount();
	const ETransportResult LaterSendResult =
		Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(ReplacementPayload, sizeof(ReplacementPayload)));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The write-error fixture device must initialize");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstSendResult, "The first frame must queue before the write error");
	MW_EXPECT_EQ(Test, std::size_t{3}, WriteCalls, "The hard-error advance must attempt two bytes before failing on the next write");
	MW_EXPECT_EQ(Test, std::size_t{2}, WrittenBytes, "Only bytes accepted before the hard write error may reach the UART");
	MW_EXPECT_EQ(Test, ETransportResult::Success, LaterSendResult, "A hard write error must release the slot for a later send");
}

} // namespace
