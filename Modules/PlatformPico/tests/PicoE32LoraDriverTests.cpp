#include "TestSupport.h"

#include <MicroWorld/Net/E32Lora.h>
#include <MicroWorld/Net/FrameCodec.h>
#include <MicroWorld/Net/NetDriver.h>
#include <MicroWorld/Net/NetResult.h>
#include <MicroWorld/PlatformPico/Detail/PicoE32LoraPlatform.h>
#include <MicroWorld/PlatformPico/PicoE32LoraDriver.h>

#include <cstddef>
#include <cstdint>

namespace
{

using MicroWorld::E32MaxPayloadBytes;
using MicroWorld::ENetResult;
using MicroWorld::FNetAddress;
using MicroWorld::FNetReceiveResult;
using MicroWorld::FPicoE32LoraConfig;
using MicroWorld::FPicoE32LoraDriver;
using MicroWorld::FrameOverheadBytes;
using MicroWorld::MakeLoraAddress;
using MicroWorld::TSpan;
using MicroWorld::Detail::IPicoE32LoraPlatform;

/** Source id stored by the initialized driver in transmit frame fixtures. */
constexpr std::uint8_t LocalNodeId = 7;

/** Peer id used for destination metadata and decoded receive frames. */
constexpr std::uint8_t PeerNodeId = 9;

/** Exact configured baud rate expected from the fake UART binding. */
constexpr std::uint32_t ExpectedBaudRate = 9600;

/** Bounded driver receive budget: twice the largest framed E32 payload. */
constexpr std::size_t ReceivePumpByteCap = 2u * (E32MaxPayloadBytes + FrameOverheadBytes);

/** Fixed test payload used to validate full frame send and receive behavior. */
constexpr std::uint8_t Payload[] = {0x10, 0x20, 0x30};

/** Sentinel source byte that proves an unavailable receive preserves caller outputs. */
constexpr std::uint8_t SentinelNodeId = 0xEE;

/** Sentinel byte count that proves an unavailable receive preserves caller outputs. */
constexpr std::size_t SentinelByteCount = 123;

/**
 * Bounded, per-test Pico UART fake that exposes driver-visible resource and byte operations.
 *
 * The fake owns no global state so each test creates a fresh platform timeline.
 */
class FFakePicoE32LoraPlatform final : public IPicoE32LoraPlatform
{
public:
	/** Records one open request and returns the configured achieved baud rate. */
	std::uint32_t OpenUart(
		const std::uint8_t InUartIndex, const unsigned int InTxGpio, const unsigned int InRxGpio, const std::uint32_t InBaudRate) noexcept override
	{
		++OpenCallCount;
		LastOpenedUartIndex = InUartIndex;
		LastOpenedTxGpio = InTxGpio;
		LastOpenedRxGpio = InRxGpio;
		LastRequestedBaudRate = InBaudRate;
		return AchievedBaudRate;
	}

	/** Records one close request so failed setup and destruction are observable. */
	void CloseUart(const std::uint8_t InUartIndex) noexcept override
	{
		++CloseCallCount;
		LastClosedUartIndex = InUartIndex;
	}

	/** Reports the locally controlled transmit capacity. */
	bool IsUartWritable(const std::uint8_t InUartIndex) noexcept override
	{
		LastWritableUartIndex = InUartIndex;
		return bWritable;
	}

	/** Captures one byte accepted by the driver after the writable check succeeds. */
	void WriteUartByte(const std::uint8_t InUartIndex, const std::uint8_t InByte) noexcept override
	{
		LastWrittenUartIndex = InUartIndex;
		WrittenBytes[WrittenByteCount] = InByte;
		++WrittenByteCount;
	}

	/** Supplies queued receive bytes in FIFO order and records every polling attempt. */
	bool TryReadUartByte(const std::uint8_t InUartIndex, std::uint8_t& OutByte) noexcept override
	{
		LastReadUartIndex = InUartIndex;
		++ReadCallCount;
		if (NextReceivedByteIndex == QueuedReceiveByteCount)
		{
			return false;
		}

		OutByte = QueuedReceiveBytes[NextReceivedByteIndex];
		++NextReceivedByteIndex;
		return true;
	}

	/** Appends one predetermined raw UART byte for a later driver receive poll. */
	void QueueReceiveByte(const std::uint8_t InByte) noexcept
	{
		QueuedReceiveBytes[QueuedReceiveByteCount] = InByte;
		++QueuedReceiveByteCount;
	}

	/** Achieved baud rate returned from each configurable fake UART open. */
	std::uint32_t AchievedBaudRate{ExpectedBaudRate};

	/** Allows tests to hold the UART unavailable without blocking. */
	bool bWritable{true};

	/** Counts platform open requests made by the driver. */
	std::size_t OpenCallCount{0};

	/** Counts platform close requests made by the driver. */
	std::size_t CloseCallCount{0};

	/** Retains the UART identity from the latest open request. */
	std::uint8_t LastOpenedUartIndex{0};

	/** Retains the TX GPIO from the latest open request. */
	unsigned int LastOpenedTxGpio{0};

	/** Retains the RX GPIO from the latest open request. */
	unsigned int LastOpenedRxGpio{0};

	/** Retains the baud rate from the latest open request. */
	std::uint32_t LastRequestedBaudRate{0};

	/** Retains the UART identity from the latest close request. */
	std::uint8_t LastClosedUartIndex{0};

	/** Retains the UART identity observed by the latest writable query. */
	std::uint8_t LastWritableUartIndex{0};

	/** Retains the UART identity used for the latest transmitted byte. */
	std::uint8_t LastWrittenUartIndex{0};

	/** Retains the UART identity observed by the latest receive poll. */
	std::uint8_t LastReadUartIndex{0};

	/** Stores every bounded transmit byte accepted by the fake UART. */
	std::uint8_t WrittenBytes[E32MaxPayloadBytes + FrameOverheadBytes]{};

	/** Counts transmit bytes retained in WrittenBytes. */
	std::size_t WrittenByteCount{0};

	/** Stores enough raw bytes to exceed one receive pump and append a full frame. */
	std::uint8_t QueuedReceiveBytes[ReceivePumpByteCap + E32MaxPayloadBytes + FrameOverheadBytes]{};

	/** Counts bytes queued for the next driver receive calls. */
	std::size_t QueuedReceiveByteCount{0};

	/** Identifies the next queued byte the fake will return. */
	std::size_t NextReceivedByteIndex{0};

	/** Counts every polling attempt, including the first empty poll. */
	std::size_t ReadCallCount{0};
};

/** Supplies the supported UART0 routing needed by all successful initialization fixtures. */
FPicoE32LoraConfig MakeValidConfig() noexcept
{
	FPicoE32LoraConfig Config{};
	Config.UartIndex = 0;
	Config.TxGpio = 0;
	Config.RxGpio = 1;
	Config.BaudRate = ExpectedBaudRate;
	Config.LocalNodeId = LocalNodeId;
	return Config;
}

/**
 * Scenario: Initialize the driver with an unsupported UART pin configuration.
 * Expected: Initialization is rejected as Invalid; the driver is left closed; the fake UART is neither acquired nor released.
 */
MW_TEST_CASE(PicoE32DriverRejectsInvalidConfigBeforePlatformOpen)
{
	// Arrange
	FFakePicoE32LoraPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	FPicoE32LoraConfig InvalidConfig = MakeValidConfig();
	InvalidConfig.TxGpio = 2;

	// Act
	const ENetResult InitializeResult = Driver.Initialize(InvalidConfig);

	// Assert
	const bool bDriverOpen = Driver.IsOpen();
	const bool bDriverClosed = !bDriverOpen;
	const std::size_t OpenCalls = Platform.OpenCallCount;
	const std::size_t CloseCalls = Platform.CloseCallCount;
	MW_EXPECT_EQ(Test, ENetResult::Invalid, InitializeResult, "Unsupported UART pins must reject initialization");
	MW_EXPECT_TRUE(Test, bDriverClosed, "Invalid initialization must leave the driver closed");
	MW_EXPECT_EQ(Test, std::size_t{0}, OpenCalls, "Invalid configuration must not acquire a UART");
	MW_EXPECT_EQ(Test, std::size_t{0}, CloseCalls, "Invalid configuration must not release an unopened UART");
}

/**
 * Scenario: Initialize the driver with valid pins but a platform that achieves a baud rate below the requested rate.
 * Expected: Initialization is rejected as Invalid after one UART open; the acquired UART is released once; the driver is left closed.
 */
MW_TEST_CASE(PicoE32DriverClosesMismatchedBaudAttempt)
{
	// Arrange
	FFakePicoE32LoraPlatform Platform;
	Platform.AchievedBaudRate = ExpectedBaudRate - 1;
	FPicoE32LoraDriver Driver(Platform);
	const FPicoE32LoraConfig Config = MakeValidConfig();

	// Act
	const ENetResult InitializeResult = Driver.Initialize(Config);

	// Assert
	const bool bDriverOpen = Driver.IsOpen();
	const bool bDriverClosed = !bDriverOpen;
	const std::size_t OpenCalls = Platform.OpenCallCount;
	const std::size_t CloseCalls = Platform.CloseCallCount;
	const std::uint8_t ClosedUartIndex = Platform.LastClosedUartIndex;
	const std::uint8_t RequestedUartIndex = Config.UartIndex;
	MW_EXPECT_EQ(Test, ENetResult::Invalid, InitializeResult, "An inexact achieved baud rate must reject initialization");
	MW_EXPECT_TRUE(Test, bDriverClosed, "An inexact baud rate must leave the driver closed");
	MW_EXPECT_EQ(Test, std::size_t{1}, OpenCalls, "Valid pins must reach the platform open attempt");
	MW_EXPECT_EQ(Test, std::size_t{1}, CloseCalls, "A failed baud match must release the opened UART");
	MW_EXPECT_EQ(Test, RequestedUartIndex, ClosedUartIndex, "Failed initialization must release the requested UART identity");
}

/**
 * Scenario: Queue one frame, attempt a second, drive an AdvanceTransmit while the UART is blocked, then advance through every encoded byte.
 * Expected: The second send stays Full while the slot is occupied and while the UART is blocked; writable polls transmit every encoded byte in order;
 * only the final byte releases the slot for a new send.
 */
MW_TEST_CASE(PicoE32DriverAppliesTransmitBackpressureUntilAllBytesAdvance)
{
	// Arrange
	FFakePicoE32LoraPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	const FPicoE32LoraConfig Config = MakeValidConfig();
	const FNetAddress Destination = MakeLoraAddress(PeerNodeId);
	std::uint8_t ExpectedFrame[E32MaxPayloadBytes + FrameOverheadBytes]{};
	std::size_t ExpectedFrameBytes = 0;
	const ENetResult EncodeResult = MicroWorld::EncodeFrame(
		LocalNodeId,
		TSpan<const std::uint8_t>(Payload, sizeof(Payload)),
		TSpan<std::uint8_t>(ExpectedFrame, sizeof(ExpectedFrame)),
		ExpectedFrameBytes);
	const ENetResult InitializeResult = Driver.Initialize(Config);
	const ENetResult FirstSendResult = Driver.TrySend(Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	const ENetResult FullBeforeProgressResult = Driver.TrySend(Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	Platform.bWritable = false;

	// Act
	Driver.AdvanceTransmit();
	const std::size_t BytesWhileBlocked = Platform.WrittenByteCount;
	const ENetResult FullWhileBlockedResult = Driver.TrySend(Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
	Platform.bWritable = true;
	for (std::size_t ByteIndex = 0; ByteIndex < ExpectedFrameBytes; ++ByteIndex)
	{
		Driver.AdvanceTransmit();
	}
	const ENetResult SendAfterFinalByteResult = Driver.TrySend(Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	// Assert
	const std::size_t WrittenByteCount = Platform.WrittenByteCount;
	MW_EXPECT_EQ(Test, ENetResult::Success, EncodeResult, "The expected test frame must encode successfully");
	MW_EXPECT_EQ(Test, ENetResult::Success, InitializeResult, "A valid configuration must open the driver");
	MW_EXPECT_EQ(Test, ENetResult::Success, FirstSendResult, "An empty transmit slot must accept one frame");
	MW_EXPECT_EQ(Test, ENetResult::Full, FullBeforeProgressResult, "A queued frame must apply immediate send backpressure");
	MW_EXPECT_EQ(Test, std::size_t{0}, BytesWhileBlocked, "A blocked UART must not consume the queued frame");
	MW_EXPECT_EQ(Test, ENetResult::Full, FullWhileBlockedResult, "A blocked UART must retain transmit backpressure");
	MW_EXPECT_EQ(Test, ExpectedFrameBytes, WrittenByteCount, "Writable polls must transmit every queued frame byte");
	for (std::size_t ByteIndex = 0; ByteIndex < ExpectedFrameBytes; ++ByteIndex)
	{
		const std::uint8_t ExpectedByte = ExpectedFrame[ByteIndex];
		const std::uint8_t WrittenByte = Platform.WrittenBytes[ByteIndex];
		MW_EXPECT_EQ(Test, ExpectedByte, WrittenByte, "Writable polls must preserve encoded frame byte order");
	}
	MW_EXPECT_EQ(Test, ENetResult::Success, SendAfterFinalByteResult, "The final transmitted byte must release the transmit slot");
}

/**
 * Scenario: Queue a full frame beyond the receive byte budget, then poll receive twice.
 * Expected: The first poll defers the frame as Unavailable after consuming only the fixed byte budget and preserves caller outputs; the second poll
 * delivers the complete valid frame with the correct sender, size, and bytes.
 */
MW_TEST_CASE(PicoE32DriverCapsReceiveWorkThenDeliversValidFrame)
{
	// Arrange
	FFakePicoE32LoraPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	const FPicoE32LoraConfig Config = MakeValidConfig();
	std::uint8_t EncodedFrame[E32MaxPayloadBytes + FrameOverheadBytes]{};
	std::size_t EncodedFrameBytes = 0;
	const ENetResult EncodeResult = MicroWorld::EncodeFrame(
		PeerNodeId, TSpan<const std::uint8_t>(Payload, sizeof(Payload)), TSpan<std::uint8_t>(EncodedFrame, sizeof(EncodedFrame)), EncodedFrameBytes);
	for (std::size_t ByteIndex = 0; ByteIndex < ReceivePumpByteCap; ++ByteIndex)
	{
		Platform.QueueReceiveByte(0);
	}
	for (std::size_t ByteIndex = 0; ByteIndex < EncodedFrameBytes; ++ByteIndex)
	{
		Platform.QueueReceiveByte(EncodedFrame[ByteIndex]);
	}
	const ENetResult InitializeResult = Driver.Initialize(Config);
	FNetAddress From = MakeLoraAddress(SentinelNodeId);
	FNetReceiveResult ReceiveResult{SentinelByteCount};
	std::uint8_t Destination[sizeof(Payload)]{};

	// Act
	const ENetResult FirstReceiveResult = Driver.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);
	const std::size_t ReadCallsAfterFirstPoll = Platform.ReadCallCount;
	const std::uint8_t SenderAfterFirstPoll = From.Bytes[0];
	const std::size_t BytesReceivedAfterFirstPoll = ReceiveResult.BytesReceived;
	const ENetResult SecondReceiveResult = Driver.TryReceive(From, TSpan<std::uint8_t>(Destination, sizeof(Destination)), ReceiveResult);

	// Assert
	const std::size_t TotalReadCalls = Platform.ReadCallCount;
	const std::uint8_t SenderNodeId = From.Bytes[0];
	const std::size_t ReceivedBytes = ReceiveResult.BytesReceived;
	const std::size_t ExpectedTotalReadCalls = ReceivePumpByteCap + EncodedFrameBytes;
	const std::size_t PayloadBytes = sizeof(Payload);
	MW_EXPECT_EQ(Test, ENetResult::Success, EncodeResult, "The valid peer frame fixture must encode successfully");
	MW_EXPECT_EQ(Test, ENetResult::Success, InitializeResult, "A valid configuration must open the driver");
	MW_EXPECT_EQ(Test, ENetResult::Unavailable, FirstReceiveResult, "A frame beyond the receive budget must wait for another poll");
	MW_EXPECT_EQ(Test, ReceivePumpByteCap, ReadCallsAfterFirstPoll, "One receive poll must consume no more than the fixed byte budget");
	MW_EXPECT_EQ(Test, SentinelNodeId, SenderAfterFirstPoll, "A budget-limited receive must preserve the sender output");
	MW_EXPECT_EQ(Test, SentinelByteCount, BytesReceivedAfterFirstPoll, "A budget-limited receive must preserve the byte count");
	MW_EXPECT_EQ(Test, ENetResult::Success, SecondReceiveResult, "The later receive poll must deliver the complete valid frame");
	MW_EXPECT_EQ(Test, ExpectedTotalReadCalls, TotalReadCalls, "The second poll must read only the queued frame bytes");
	MW_EXPECT_EQ(Test, PeerNodeId, SenderNodeId, "Delivered frames must report their encoded peer node id");
	MW_EXPECT_EQ(Test, PayloadBytes, ReceivedBytes, "Delivered frames must report their complete payload length");
	for (std::size_t ByteIndex = 0; ByteIndex < sizeof(Payload); ++ByteIndex)
	{
		const std::uint8_t ExpectedByte = Payload[ByteIndex];
		const std::uint8_t ReceivedByte = Destination[ByteIndex];
		MW_EXPECT_EQ(Test, ExpectedByte, ReceivedByte, "Delivered frames must preserve every payload byte");
	}
}

/**
 * Scenario: Initialize a driver that successfully opens its UART, then destroy the driver.
 * Expected: The UART is acquired exactly once during initialization and released exactly once on destruction.
 */
MW_TEST_CASE(PicoE32DriverClosesOpenedUartOnDestruction)
{
	// Arrange
	FFakePicoE32LoraPlatform Platform;
	const FPicoE32LoraConfig Config = MakeValidConfig();
	ENetResult InitializeResult = ENetResult::Unavailable;

	// Act
	{
		FPicoE32LoraDriver Driver(Platform);
		InitializeResult = Driver.Initialize(Config);
	}

	// Assert
	const std::size_t OpenCalls = Platform.OpenCallCount;
	const std::size_t CloseCalls = Platform.CloseCallCount;
	const std::uint8_t ClosedUartIndex = Platform.LastClosedUartIndex;
	const std::uint8_t RequestedUartIndex = Config.UartIndex;
	MW_EXPECT_EQ(Test, ENetResult::Success, InitializeResult, "A valid configuration must open the driver before destruction");
	MW_EXPECT_EQ(Test, std::size_t{1}, OpenCalls, "The driver must acquire one UART for one initialization");
	MW_EXPECT_EQ(Test, std::size_t{1}, CloseCalls, "Destroying an open driver must release its UART once");
	MW_EXPECT_EQ(Test, RequestedUartIndex, ClosedUartIndex, "Destruction must release the initialized UART identity");
}

} // namespace
