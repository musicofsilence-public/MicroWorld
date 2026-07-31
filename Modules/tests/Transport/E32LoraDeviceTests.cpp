#include "TestSupport.h"

#include <MicroWorld/Core/IO/UartByteStream.h>
#include <MicroWorld/Transport/Lora/E32Lora.h>
#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/Device.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Transport/Lora/E32LoraDevice.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::EUartByteStreamResult;
using MicroWorld::IUartByteStream;
using MicroWorld::TSpan;
using MicroWorld::Transport::E32MaxPayloadBytes;
using MicroWorld::Transport::ETransportResult;
using MicroWorld::Transport::FE32LoraDevice;
using MicroWorld::Transport::MakeLoraAddress;
using MicroWorld::Transport::Address::FDeviceAddress;
using MicroWorld::Transport::Device::FReceiveResult;
using MicroWorld::Transport::FrameCodec::FrameOverheadBytes;

/** Fixed encoded E32 frame capacity used by transmit and receive test fixtures. */
constexpr std::size_t EncodedFrameCapacity = E32MaxPayloadBytes + FrameOverheadBytes;

/** Device receive budget derived from the largest possible encoded E32 frame. */
constexpr std::size_t ReceivePumpByteCap = 2u * EncodedFrameCapacity;

/** Fixed receive-stream capacity for one capped prefix followed by up to two encoded frames. */
constexpr std::size_t ReceiveStreamCapacity = ReceivePumpByteCap + (2u * EncodedFrameCapacity);

/** Local node id used by every initialized transmitting device fixture. */
constexpr std::uint8_t LocalNodeId = 7;

/** Peer node id used by every valid one-byte E32 destination and decoded frame fixture. */
constexpr std::uint8_t PeerNodeId = 9;

/** Distinct node id used to prove non-success receives preserve caller sender outputs. */
constexpr std::uint8_t SentinelNodeId = 0xEE;

/** Distinct value used to prove non-success receives preserve caller byte outputs. */
constexpr std::uint8_t SentinelByte = 0xD3;

/** Distinct byte count used to prove non-success receives preserve the result output. */
constexpr std::size_t SentinelByteCount = 123;

/** Three-byte payload used by normal send, receive, recovery, and exchange cases. */
constexpr std::uint8_t Payload[] = {0x10, 0x20, 0x30};

/** Different payload that proves a released transmit slot accepts later work. */
constexpr std::uint8_t ReplacementPayload[] = {0x91, 0x82};

/** Largest valid payload, used to exercise fixed-frame capacity and bounded burst progress. */
constexpr std::uint8_t MaximumPayload[E32MaxPayloadBytes] = {};

/** One backed byte paired with an oversize span length so validation rejects before reading beyond it. */
constexpr std::uint8_t OversizePayloadByte = 0;

/**
 * Fixed-capacity UART fake exposing explicit non-blocking read and write outcomes.
 *
 * Each test owns one fresh fake, so recorded traffic and configured outcomes never cross test boundaries.
 */
class FFakeUartByteStream final : public IUartByteStream
{
public:
	/** Records a successful byte write or reports the result currently selected by the test. */
	EUartByteStreamResult TryWriteByte(const std::uint8_t InByte) noexcept override
	{
		++WriteCallCountValue;
		if (WriteResult != EUartByteStreamResult::Success)
		{
			return WriteResult;
		}
		if (WrittenByteCountValue == SuccessfulWriteLimit)
		{
			return EUartByteStreamResult::Error;
		}
		if (WrittenByteCountValue == EncodedFrameCapacity)
		{
			return EUartByteStreamResult::Error;
		}

		WrittenBytes[WrittenByteCountValue] = InByte;
		++WrittenByteCountValue;
		return EUartByteStreamResult::Success;
	}

	/** Supplies the next queued byte or reports the result currently selected by the test. */
	EUartByteStreamResult TryReadByte(std::uint8_t& OutByte) noexcept override
	{
		++ReadCallCountValue;
		if (ReadResult != EUartByteStreamResult::Success)
		{
			return ReadResult;
		}
		if (NextReceivedByteIndex == QueuedReceiveByteCount)
		{
			return EUartByteStreamResult::Unavailable;
		}

		OutByte = QueuedReceiveBytes[NextReceivedByteIndex];
		++NextReceivedByteIndex;
		return EUartByteStreamResult::Success;
	}

	/** Selects the outcome returned by every later write attempt until another test change. */
	void SetWriteResult(const EUartByteStreamResult InResult) noexcept { WriteResult = InResult; }

	/** Limits accepted writes before later attempts report Error, exercising partial-frame failure without result storage. */
	void SetSuccessfulWriteLimit(const std::size_t InLimit) noexcept { SuccessfulWriteLimit = InLimit; }

	/** Selects the outcome returned by every later read attempt until another test change. */
	void SetReadResult(const EUartByteStreamResult InResult) noexcept { ReadResult = InResult; }

	/** Queues one raw incoming UART byte when fixed test storage remains available. */
	bool QueueReceivedByte(const std::uint8_t InByte) noexcept
	{
		if (QueuedReceiveByteCount == ReceiveStreamCapacity)
		{
			return false;
		}

		QueuedReceiveBytes[QueuedReceiveByteCount] = InByte;
		++QueuedReceiveByteCount;
		return true;
	}

	/** Reports how many write attempts the device made, including blocked and failed attempts. */
	std::size_t WriteCallCount() const noexcept { return WriteCallCountValue; }

	/** Reports how many bytes the fake accepted after successful write attempts. */
	std::size_t WrittenByteCount() const noexcept { return WrittenByteCountValue; }

	/** Reports how many read attempts the device made, including empty and failed attempts. */
	std::size_t ReadCallCount() const noexcept { return ReadCallCountValue; }

	/** Reads one captured successful write for observable wire-traffic assertions. */
	std::uint8_t WrittenByteAt(const std::size_t InIndex) const noexcept { return WrittenBytes[InIndex]; }

private:
	/** Stores all successful writes from the one fixed frame a device may queue at once. */
	std::uint8_t WrittenBytes[EncodedFrameCapacity]{};

	/** Stores raw bytes supplied to later receive polls without dynamic storage. */
	std::uint8_t QueuedReceiveBytes[ReceiveStreamCapacity]{};

	/** Controls whether a write succeeds, blocks, or fails for the active test scenario. */
	EUartByteStreamResult WriteResult{EUartByteStreamResult::Success};

	/** Controls whether a read consumes queued data, blocks, or fails for the active test scenario. */
	EUartByteStreamResult ReadResult{EUartByteStreamResult::Success};

	/** Bounds successful writes before the fake reports Error for later attempts in a partial-frame failure scenario. */
	std::size_t SuccessfulWriteLimit{EncodedFrameCapacity};

	/** Counts accepted bytes in WrittenBytes and bounds later indexed observations. */
	std::size_t WrittenByteCountValue{0};

	/** Counts queued inbound bytes and bounds later UART read attempts. */
	std::size_t QueuedReceiveByteCount{0};

	/** Identifies the next queued inbound byte that a successful read may consume. */
	std::size_t NextReceivedByteIndex{0};

	/** Counts every write operation so blocked/error attempts stay observable. */
	std::size_t WriteCallCountValue{0};

	/** Counts every read operation so bounded receive polling stays observable. */
	std::size_t ReadCallCountValue{0};
};

/** Encodes one peer frame into fixed storage for public-device receive scenarios. */
ETransportResult EncodePeerFrame(
	const std::uint8_t* const InPayload,
	const std::size_t InPayloadSize,
	std::uint8_t (&OutFrame)[EncodedFrameCapacity],
	std::size_t& OutFrameBytes) noexcept
{
	return MicroWorld::Transport::FrameCodec::EncodeFrame(
		PeerNodeId, TSpan<const std::uint8_t>(InPayload, InPayloadSize), TSpan<std::uint8_t>(OutFrame, sizeof(OutFrame)), OutFrameBytes);
}

/** Queues every byte of one fixed frame into a fake stream and reports whether its capacity was sufficient. */
bool QueueFrame(FFakeUartByteStream& InStream, const std::uint8_t* const InFrame, const std::size_t InFrameBytes) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < InFrameBytes; ++ByteIndex)
	{
		if (!InStream.QueueReceivedByte(InFrame[ByteIndex]))
		{
			return false;
		}
	}

	return true;
}

/** Reports whether a destination retains one expected repeated sentinel value. */
bool DestinationContains(const std::uint8_t* const InDestination, const std::size_t InDestinationBytes, const std::uint8_t InExpected) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < InDestinationBytes; ++ByteIndex)
	{
		if (InDestination[ByteIndex] != InExpected)
		{
			return false;
		}
	}

	return true;
}

/** Reports whether one received destination exactly matches the supplied expected payload. */
bool DestinationMatches(const std::uint8_t* const InDestination, const std::uint8_t* const InExpected, const std::size_t InBytes) noexcept
{
	for (std::size_t ByteIndex = 0; ByteIndex < InBytes; ++ByteIndex)
	{
		if (InDestination[ByteIndex] != InExpected[ByteIndex])
		{
			return false;
		}
	}

	return true;
}

/** Reports whether an output address remains the expected one-byte E32 address. */
bool AddressHasNodeId(const FDeviceAddress& InAddress, const std::uint8_t InNodeId) noexcept
{
	return InAddress.Size == 1 && InAddress.Bytes[0] == InNodeId;
}

/**
 * Scenario: Use send and receive before initialization, then initialize twice.
 * Expected: The device remains inert before initialization, initializes once without UART I/O, and rejects a later initialization.
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
	Device.AdvanceTransmit();
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
 * Scenario: Send malformed, null, and oversize payloads before sending valid normal, empty, and maximum packets.
 * Expected: Invalid sends do not occupy the slot; every documented valid payload boundary is accepted.
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
	Device.AdvanceTransmit();
	const std::size_t ValidFrameWrittenBytes = Stream.WrittenByteCount();
	const ETransportResult EmptyPayloadResult = Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(nullptr, 0));
	Device.AdvanceTransmit();
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
	MW_EXPECT_EQ(Test, sizeof(Payload) + FrameOverheadBytes, ValidFrameWrittenBytes, "AdvanceTransmit must emit the accepted valid payload frame");
	MW_EXPECT_EQ(Test, ETransportResult::Success, EmptyPayloadResult, "An empty payload must be accepted");
	MW_EXPECT_EQ(
		Test, sizeof(Payload) + (2u * FrameOverheadBytes), EmptyFrameWrittenBytes, "AdvanceTransmit must emit the accepted empty payload frame");
	MW_EXPECT_EQ(Test, ETransportResult::Success, MaximumPayloadResult, "The maximum E32 payload must be accepted");
	MW_EXPECT_EQ(Test, E32MaxPayloadBytes, MaximumPacketBytes, "MaxPacketBytes must expose the E32 payload limit");
}

/**
 * Scenario: Queue one maximum packet, attempt a second while occupied, then advance once through an always-writable UART.
 * Expected: The second send reports Full; one advance drains the complete fixed frame within its bounded byte budget and frees the slot.
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
	Device.AdvanceTransmit();
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
 * Scenario: Queue a frame, block the first write, then make the UART writable and advance again.
 * Expected: The blocked byte remains queued and becomes the first successfully emitted byte on retry.
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
	Device.AdvanceTransmit();
	const std::size_t BlockedWriteCalls = Stream.WriteCallCount();
	const std::size_t BlockedWrittenBytes = Stream.WrittenByteCount();
	const ETransportResult FullWhileBlockedResult =
		Device.TrySend(DestinationAddress, TSpan<const std::uint8_t>(ReplacementPayload, sizeof(ReplacementPayload)));
	Stream.SetWriteResult(EUartByteStreamResult::Success);
	Device.AdvanceTransmit();
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
 * Scenario: Queue a frame, make the first UART write fail permanently, then queue another frame.
 * Expected: The failed frame is discarded so the later send is accepted instead of remaining Full forever.
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
	Device.AdvanceTransmit();
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

/**
 * Scenario: Supply one valid peer frame through the byte stream and receive it through the public device.
 * Expected: Success changes every receive
 * output to the decoded sender, payload bytes, and byte count.
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
 * Scenario: Poll an initialized device whose byte stream has no queued data.
 * Expected: Unavailable leaves every caller-owned receive output
 * unchanged.
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
 * Scenario: Hold one decoded frame, reject a short and null destination, then retry with sufficient storage.
 * Expected: Both rejections preserve caller outputs and retain the frame; the later valid retry delivers it.
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
 * Scenario: Feed a CRC-corrupt frame immediately followed by a valid peer frame.
 * Expected: The corrupt candidate is discarded and the public device resynchronizes to deliver the later valid frame.
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
 * Scenario: Queue exactly one receive budget of garbage before a complete valid frame and poll twice.
 * Expected: The first poll consumes no more than the budget and preserves outputs; the second poll delivers the frame.
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
 * Scenario: Make the UART return a hard read error while caller outputs contain sentinels.
 * Expected: The device maps the error to Invalid and preserves every caller-owned receive output.
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
 * Scenario: Queue a packet on one initialized public device, advance it over a fake UART, and feed those exact bytes to another public device.
 *
 * Expected: The receiving device decodes the real emitted frame and delivers the original sender, payload, and byte count.
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
	Sender.AdvanceTransmit();
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
