#include "TestSupport.h"

#include <MicroWorld/Transport/E32Lora.h>
#include <MicroWorld/Transport/FrameCodec.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Pico/Detail/PicoE32LoraPlatform.h>
#include <MicroWorld/Platform/Pico/Detail/PicoUartPlatform.h>
#include <MicroWorld/Platform/Pico/PicoE32LoraDriver.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace
{

using MicroWorld::E32MaxPayloadBytes;
using MicroWorld::ETransportResult;
using MicroWorld::FPicoE32LoraConfig;
using MicroWorld::FPicoE32LoraDriver;
using MicroWorld::FrameOverheadBytes;
using MicroWorld::MakeLoraAddress;
using MicroWorld::TSpan;
using MicroWorld::Detail::IPicoE32LoraPlatform;
using MicroWorld::Detail::IPicoUartPlatform;

/** Exact UART rate returned by a successful fake platform open. */
constexpr std::uint32_t ExpectedBaudRate = 9600;

/** Source node id used by every facade initialization fixture. */
constexpr std::uint8_t LocalNodeId = 7;

/** Destination node id needed to queue a public E32 send through the facade. */
constexpr std::uint8_t PeerNodeId = 9;

/** Largest complete encoded E32 frame the facade can delegate in one transmit advance. */
constexpr std::size_t EncodedFrameCapacity = E32MaxPayloadBytes + FrameOverheadBytes;

/**
 * Narrow Pico UART fake that exposes only facade-owned platform behavior.
 *
 * Protocol framing stays in RadioE32 tests; this fake records UART lifetime,
 * configuration, and physical byte-progress delegation without SDK access.
 */
class FFakePicoUartPlatform final : public IPicoE32LoraPlatform
{
public:
	/** Records one requested UART configuration and returns the controlled achieved baud rate. */
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

	/** Records each facade-driven UART release. */
	void CloseUart(const std::uint8_t InUartIndex) noexcept override
	{
		++CloseCallCount;
		LastClosedUartIndex = InUartIndex;
	}

	/** Reports writable capacity so the facade can delegate a bounded transmit burst. */
	bool IsUartWritable(const std::uint8_t InUartIndex) noexcept override
	{
		LastWritableUartIndex = InUartIndex;
		return true;
	}

	/** Counts every byte RadioE32 delegates through the Pico byte stream. */
	void WriteUartByte(const std::uint8_t InUartIndex, const std::uint8_t InByte) noexcept override
	{
		LastWrittenUartIndex = InUartIndex;
		WrittenBytes[WrittenByteCount] = InByte;
		++WrittenByteCount;
	}

	/** Supplies no receive bytes because protocol decoding belongs to RadioE32 tests. */
	bool TryReadUartByte(const std::uint8_t InUartIndex, std::uint8_t& OutByte) noexcept override
	{
		(void)InUartIndex;
		(void)OutByte;
		return false;
	}

	/** Controls the reported exact baud rate for success and rollback fixtures. */
	std::uint32_t AchievedBaudRate{ExpectedBaudRate};

	/** Counts each facade open request. */
	std::size_t OpenCallCount{0};

	/** Counts each rollback or destruction release. */
	std::size_t CloseCallCount{0};

	/** Records the latest UART identity requested for opening. */
	std::uint8_t LastOpenedUartIndex{0};

	/** Records the latest TX GPIO requested for opening. */
	unsigned int LastOpenedTxGpio{0};

	/** Records the latest RX GPIO requested for opening. */
	unsigned int LastOpenedRxGpio{0};

	/** Records the latest baud rate requested for opening. */
	std::uint32_t LastRequestedBaudRate{0};

	/** Records the latest UART identity passed to a release. */
	std::uint8_t LastClosedUartIndex{0};

	/** Records the UART identity used for the latest writable query. */
	std::uint8_t LastWritableUartIndex{0};

	/** Records the UART identity used for the latest delegated write. */
	std::uint8_t LastWrittenUartIndex{0};

	/** Counts bytes accepted during delegated physical transmit progress. */
	std::size_t WrittenByteCount{0};

	/** Retains the one complete encoded frame emitted by the facade during a test. */
	std::uint8_t WrittenBytes[EncodedFrameCapacity]{};
};

/** Supplies a supported UART0 configuration for successful facade initialization. */
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

static_assert(
	std::is_same<IPicoE32LoraPlatform, IPicoUartPlatform>::value,
	"The released E32 platform type must remain the generic Pico UART compatibility alias.");

/**
 * Scenario: Initialize the facade with an unsupported UART TX routing.
 * Expected: The byte stream rejects the configuration before the platform receives an open request.
 */
MW_TEST_CASE(PicoE32FacadeRejectsInvalidConfigBeforeOpeningUart)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	FPicoE32LoraConfig InvalidConfig = MakeValidConfig();
	InvalidConfig.TxGpio = 2;

	// Act
	const ETransportResult InitializeResult = Driver.Initialize(InvalidConfig);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, InitializeResult, "Unsupported Pico UART routing must be invalid");
	MW_EXPECT_TRUE(Test, !Driver.IsOpen(), "Invalid configuration must leave the facade closed");
	MW_EXPECT_EQ(Test, std::size_t{0}, Platform.OpenCallCount, "Invalid configuration must not acquire a UART");
	MW_EXPECT_EQ(Test, std::size_t{0}, Platform.CloseCallCount, "Invalid configuration must not release an unopened UART");
}

/**
 * Scenario: Initialize the facade with a supported UART1 configuration.
 * Expected: The facade delegates the exact index, pins, and baud rate to the generic `IPicoUartPlatform` interface.
 */
MW_TEST_CASE(PicoE32FacadeDelegatesExactUartOpenConfiguration)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	FPicoE32LoraConfig Config = MakeValidConfig();
	Config.UartIndex = 1;
	Config.TxGpio = 4;
	Config.RxGpio = 5;
	Config.BaudRate = 19200;
	Platform.AchievedBaudRate = Config.BaudRate;

	// Act
	const ETransportResult InitializeResult = Driver.Initialize(Config);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "A supported exact configuration must initialize");
	MW_EXPECT_TRUE(Test, Driver.IsOpen(), "Successful initialization must open the facade");
	MW_EXPECT_EQ(Test, std::size_t{1}, Platform.OpenCallCount, "Initialization must delegate exactly one UART open");
	MW_EXPECT_EQ(Test, Config.UartIndex, Platform.LastOpenedUartIndex, "The UART index must reach the platform unchanged");
	MW_EXPECT_EQ(Test, Config.TxGpio, Platform.LastOpenedTxGpio, "The TX GPIO must reach the platform unchanged");
	MW_EXPECT_EQ(Test, Config.RxGpio, Platform.LastOpenedRxGpio, "The RX GPIO must reach the platform unchanged");
	MW_EXPECT_EQ(Test, Config.BaudRate, Platform.LastRequestedBaudRate, "The requested baud rate must reach the platform unchanged");
}

/**
 * Scenario: The platform opens a valid UART but reports an inexact achieved baud rate.
 * Expected: The byte stream rolls the UART back and the facade remains closed.
 */
MW_TEST_CASE(PicoE32FacadeRollsBackMismatchedBaudRate)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	Platform.AchievedBaudRate = ExpectedBaudRate - 1;
	FPicoE32LoraDriver Driver(Platform);
	const FPicoE32LoraConfig Config = MakeValidConfig();

	// Act
	const ETransportResult InitializeResult = Driver.Initialize(Config);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Invalid, InitializeResult, "Inexact baud must reject facade initialization");
	MW_EXPECT_TRUE(Test, !Driver.IsOpen(), "Inexact baud must leave the facade closed");
	MW_EXPECT_EQ(Test, std::size_t{1}, Platform.OpenCallCount, "Valid routing must reach one platform open attempt");
	MW_EXPECT_EQ(Test, std::size_t{1}, Platform.CloseCallCount, "Inexact baud must release the initialized UART");
	MW_EXPECT_EQ(Test, Config.UartIndex, Platform.LastClosedUartIndex, "Rollback must release the requested UART identity");
}

/**
 * Scenario: Initialize a facade twice with the same valid configuration.
 * Expected: The second call is rejected without opening or closing the already-owned UART.
 */
MW_TEST_CASE(PicoE32FacadeRejectsDoubleInitializationWithoutReopening)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	const FPicoE32LoraConfig Config = MakeValidConfig();

	// Act
	const ETransportResult FirstInitializeResult = Driver.Initialize(Config);
	const ETransportResult SecondInitializeResult = Driver.Initialize(Config);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstInitializeResult, "The first valid initialization must succeed");
	MW_EXPECT_EQ(Test, ETransportResult::Unavailable, SecondInitializeResult, "The second initialization must be unavailable");
	MW_EXPECT_TRUE(Test, Driver.IsOpen(), "Rejected reinitialization must retain the original open facade");
	MW_EXPECT_EQ(Test, std::size_t{1}, Platform.OpenCallCount, "Rejected reinitialization must not reopen the UART");
	MW_EXPECT_EQ(Test, std::size_t{0}, Platform.CloseCallCount, "Rejected reinitialization must not close the owned UART");
}

/**
 * Scenario: Initialize a facade successfully, then let it leave scope.
 * Expected: Byte-stream destruction releases the one UART acquired by the facade.
 */
MW_TEST_CASE(PicoE32FacadeClosesOpenedUartOnDestruction)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	const FPicoE32LoraConfig Config = MakeValidConfig();
	ETransportResult InitializeResult = ETransportResult::Unavailable;

	// Act
	{
		FPicoE32LoraDriver Driver(Platform);
		InitializeResult = Driver.Initialize(Config);
	}

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The destruction fixture must initialize successfully");
	MW_EXPECT_EQ(Test, std::size_t{1}, Platform.OpenCallCount, "The facade must acquire one UART before destruction");
	MW_EXPECT_EQ(Test, std::size_t{1}, Platform.CloseCallCount, "Facade destruction must release its open UART");
	MW_EXPECT_EQ(Test, Config.UartIndex, Platform.LastClosedUartIndex, "Destruction must release the initialized UART identity");
}

/**
 * Scenario: Use the released E32 platform type spelling with the new generic Pico UART interface.
 * Expected: A fake binding is usable through both names without an adapter or SDK dependency.
 */
MW_TEST_CASE(PicoE32FacadeRetainsLegacyPlatformAliasCompatibility)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	IPicoE32LoraPlatform& LegacyPlatform = Platform;
	IPicoUartPlatform& GenericPlatform = LegacyPlatform;

	// Act
	const bool bSamePlatformObject = &LegacyPlatform == &GenericPlatform;

	// Assert
	MW_EXPECT_TRUE(Test, bSamePlatformObject, "The legacy E32 platform alias must bind directly to the generic UART platform");
}

/**
 * Scenario: Queue a packet through the facade and advance transmit once while the fake UART remains writable.
 * Expected: The delegated RadioE32 progress emits a bounded multi-byte burst and frees the transmit slot.
 */
MW_TEST_CASE(PicoE32FacadeAdvanceTransmitDelegatesBoundedFrameBurst)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	const FPicoE32LoraConfig Config = MakeValidConfig();
	const MicroWorld::FDeviceAddress Destination = MakeLoraAddress(PeerNodeId);
	const std::uint8_t Payload[] = {0xA5};
	const ETransportResult InitializeResult = Driver.Initialize(Config);
	const ETransportResult FirstSendResult = Driver.TrySend(Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	// Act
	Driver.AdvanceTransmit();
	const std::size_t WrittenBytes = Platform.WrittenByteCount;
	const ETransportResult RetrySendResult = Driver.TrySend(Destination, TSpan<const std::uint8_t>(Payload, sizeof(Payload)));

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The transmit fixture must initialize successfully");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstSendResult, "The transmit fixture must queue one packet");
	MW_EXPECT_TRUE(Test, WrittenBytes > std::size_t{1}, "One facade advance must delegate more than one byte of bounded progress");
	MW_EXPECT_EQ(Test, ETransportResult::Success, RetrySendResult, "The bounded burst must release its complete queued frame slot");
	MW_EXPECT_EQ(Test, Config.UartIndex, Platform.LastWritableUartIndex, "Transmit progress must use the configured UART identity");
	MW_EXPECT_EQ(Test, Config.UartIndex, Platform.LastWrittenUartIndex, "Delegated writes must use the configured UART identity");
}

/**
 * Scenario: Queue an empty packet through the facade, then advance transmit once with a writable UART.
 * Expected: The complete encoded empty frame reaches the platform byte stream and frees the one-frame transmit slot.
 */
MW_TEST_CASE(PicoE32FacadeEmitsEmptyFrameAndReleasesTransmitSlot)
{
	// Arrange
	FFakePicoUartPlatform Platform;
	FPicoE32LoraDriver Driver(Platform);
	const FPicoE32LoraConfig Config = MakeValidConfig();
	const MicroWorld::FDeviceAddress Destination = MakeLoraAddress(PeerNodeId);
	const TSpan<const std::uint8_t> EmptyPayload(nullptr, 0);
	std::uint8_t ExpectedFrame[EncodedFrameCapacity]{};
	std::size_t ExpectedFrameBytes = 0;
	const ETransportResult EncodeResult =
		MicroWorld::EncodeFrame(LocalNodeId, EmptyPayload, TSpan<std::uint8_t>(ExpectedFrame, sizeof(ExpectedFrame)), ExpectedFrameBytes);

	// Act
	const ETransportResult InitializeResult = Driver.Initialize(Config);
	const ETransportResult FirstSendResult = Driver.TrySend(Destination, EmptyPayload);
	Driver.AdvanceTransmit();
	const std::size_t WrittenFrameBytes = Platform.WrittenByteCount;
	const ETransportResult RetrySendResult = Driver.TrySend(Destination, EmptyPayload);

	// Assert
	MW_EXPECT_EQ(Test, ETransportResult::Success, EncodeResult, "The empty-frame fixture must encode successfully");
	MW_EXPECT_EQ(Test, ETransportResult::Success, InitializeResult, "The empty-frame facade fixture must initialize successfully");
	MW_EXPECT_EQ(Test, ETransportResult::Success, FirstSendResult, "An empty payload must queue in an available transmit slot");
	MW_EXPECT_EQ(Test, ExpectedFrameBytes, WrittenFrameBytes, "One advance must emit every encoded empty-frame byte");
	for (std::size_t ByteIndex = 0; ByteIndex < ExpectedFrameBytes; ++ByteIndex)
	{
		const std::uint8_t ExpectedByte = ExpectedFrame[ByteIndex];
		const std::uint8_t WrittenByte = Platform.WrittenBytes[ByteIndex];
		MW_EXPECT_EQ(Test, ExpectedByte, WrittenByte, "The platform byte stream must preserve encoded empty-frame byte order");
	}
	MW_EXPECT_EQ(Test, ETransportResult::Success, RetrySendResult, "Completing the empty frame must release the transmit slot for another send");
}

} // namespace
