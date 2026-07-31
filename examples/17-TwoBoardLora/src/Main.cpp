#include <MicroWorld/Core/Log.h>
#include <MicroWorld/Transport/TransportResult.h>
#include <MicroWorld/Platform/Esp32/Esp32LoraDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32OutputDevice.h>
#include <MicroWorld/Platform/Esp32/Esp32Sleep.h>
#include <MicroWorld/Platform/Esp32/Esp32TimeSource.h>
#include <MicroWorld/Platform/Esp32/LoraAddress.h>

#include <cstddef>
#include <cstdint>

#if defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
#include "LoraPayloadRegression.h"
#endif

// Role is chosen at build time by the platformio.ini environment, exactly as the
// two-board WiFi and UART examples select theirs — never with build_src_filter.
#ifndef MICROWORLD_EXAMPLE_NODE_ID
#error "Define MICROWORLD_EXAMPLE_NODE_ID=1 or 2 via the node-a / node-b build environment."
#endif

namespace
{
/** Single real-time source; every deadline in this example reads it. */
MicroWorld::FEsp32TimeSource GTimeSource{};

/** This board's node id, stamped on every frame it sends. */
constexpr std::uint8_t LocalNodeId = MICROWORLD_EXAMPLE_NODE_ID;

/** The only other node in this pairing; the destination the device validates before broadcasting the frame. */
constexpr std::uint8_t PeerNodeId = (LocalNodeId == 1) ? 2 : 1;

#if !defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/** Node 1 sends the opening frame; node 2 waits to receive before it replies. */
constexpr std::uint8_t VolleyInitiatorNodeId = 1;
#endif

/** UART port and the two GPIOs wired to the E32 module (TX 17 -> module RXD, RX 18 -> module TXD); identical on both boards. */
constexpr std::int32_t UartPortNumber = 1;
constexpr std::int32_t TxGpioNumber = 17;
constexpr std::int32_t RxGpioNumber = 18;

/** The E32's UART runs at its factory default of 9600 8N1 (D7); this is the module's serial baud, not a wire speed. */
constexpr std::uint32_t UartBaudRate = 9600;

/** Volley period: one second, since a LoRa frame costs hundreds of ms of airtime and 500 ms (the wired example's period) would congest the channel.
 */
constexpr std::uint64_t VolleyPeriodMilliseconds = 1000;

/** Poll far faster than the volley so the FreeRTOS idle task (and its watchdog) always runs. */
constexpr unsigned PollPacingMilliseconds = 10;

#if !defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/** Volley payload layout: byte 0 is the sender node id, bytes 1..4 the counter (big-endian). */
constexpr std::size_t VolleyPayloadBytes = 5;
#endif

/** Renders one device outcome as a short label so the serial trace reads plainly. */
const char* ToText(const MicroWorld::Transport::ETransportResult Result) noexcept
{
	switch (Result)
	{
		case MicroWorld::Transport::ETransportResult::Success:
			return "Success";
		case MicroWorld::Transport::ETransportResult::Full:
			return "Full";
		case MicroWorld::Transport::ETransportResult::Invalid:
			return "Invalid";
		case MicroWorld::Transport::ETransportResult::Unavailable:
			return "Unavailable";
		default:
			return "unknown";
	}
}

#if !defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/** Packs the sender id and counter into the five-byte volley payload. */
void WriteVolleyPayload(std::uint8_t* const Out, const std::uint8_t SenderId, const std::uint32_t Counter) noexcept
{
	Out[0] = SenderId;
	Out[1] = static_cast<std::uint8_t>((Counter >> 24) & 0xFFu);
	Out[2] = static_cast<std::uint8_t>((Counter >> 16) & 0xFFu);
	Out[3] = static_cast<std::uint8_t>((Counter >> 8) & 0xFFu);
	Out[4] = static_cast<std::uint8_t>(Counter & 0xFFu);
}

/** Reads the big-endian counter back out of a received volley payload. */
std::uint32_t ReadVolleyCounter(const std::uint8_t* const In) noexcept
{
	return (static_cast<std::uint32_t>(In[1]) << 24) | (static_cast<std::uint32_t>(In[2]) << 16) | (static_cast<std::uint32_t>(In[3]) << 8)
		| static_cast<std::uint32_t>(In[4]);
}
#endif

/** Builds the device configuration for this node from the fixed pins and baud. */
MicroWorld::FEsp32E32LoraConfig MakeLoraConfig(const std::uint8_t NodeId) noexcept
{
	MicroWorld::FEsp32E32LoraConfig Config;
	Config.UartPort = UartPortNumber;
	Config.TxGpio = TxGpioNumber;
	Config.RxGpio = RxGpioNumber;
	Config.BaudRate = UartBaudRate;
	Config.LocalNodeId = NodeId;
	return Config;
}

#if defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/** Tracks the one-way regression exchange so an unexpected frame cannot be mistaken for a valid echo. */
enum class EPayloadRegressionState : std::uint8_t
{
	AwaitEmpty,
	SendEmptyEcho,
	ResendEmptyEcho,
	AwaitTypical,
	SendTypicalEcho,
	ScheduleMaximum,
	SendMaximum,
	AwaitMaximumEcho,
	Passed,
	Failed
};

/** Bounds every radio wait while allowing several LoRa airtime periods for normal startup and retries. */
constexpr std::uint64_t RegressionStepTimeoutMilliseconds = VolleyPeriodMilliseconds * 5;

/** Allows one established volley period for each leg of the maximum-payload round trip. */
constexpr std::uint64_t MaximumRoundTripTimeoutMilliseconds = VolleyPeriodMilliseconds * 2;

/** Carries the finite test state and its deadlines without allocating or retaining radio frames. */
struct FPayloadRegressionContext
{
	/** Prevents valid bytes from advancing the test out of sequence. */
	EPayloadRegressionState State{EPayloadRegressionState::AwaitEmpty};

	/** Makes every wait observable as a bounded failure rather than an infinite silent poll. */
	std::uint64_t StateDeadlineMilliseconds{0};

	/** Delays the maximum send by the established volley period after the typical echo. */
	std::uint64_t MaximumSendDueMilliseconds{0};

	/** Anchors the two-period maximum-payload round-trip budget at queued-send acceptance. */
	std::uint64_t MaximumSendAcceptedMilliseconds{0};
};

/** Identifies terminal states so completed or failed regressions cannot be altered by later radio traffic. */
bool IsPayloadRegressionTerminal(const EPayloadRegressionState InState) noexcept
{
	return InState == EPayloadRegressionState::Passed || InState == EPayloadRegressionState::Failed;
}

/** Restricts receive validation to the three protocol states that are waiting for a peer frame. */
bool IsAwaitingPayloadRegressionReceive(const EPayloadRegressionState InState) noexcept
{
	return InState == EPayloadRegressionState::AwaitEmpty || InState == EPayloadRegressionState::AwaitTypical
		|| InState == EPayloadRegressionState::AwaitMaximumEcho;
}

/** Maps each receive state to its one permitted payload boundary after the state check succeeds. */
MicroWorld::Example17::EPayloadRegressionCase ExpectedPayloadRegressionCase(const EPayloadRegressionState InState) noexcept
{
	if (InState == EPayloadRegressionState::AwaitTypical)
	{
		return MicroWorld::Example17::EPayloadRegressionCase::Typical;
	}
	if (InState == EPayloadRegressionState::AwaitMaximumEcho)
	{
		return MicroWorld::Example17::EPayloadRegressionCase::Maximum;
	}
	return MicroWorld::Example17::EPayloadRegressionCase::Empty;
}

/** Records a bounded wait failure, distinguishing the contractual maximum-frame deadline from setup/retry waits. */
void HandlePayloadRegressionTimeout(FPayloadRegressionContext& OutContext, const std::uint64_t InNowMilliseconds) noexcept
{
	if (InNowMilliseconds <= OutContext.StateDeadlineMilliseconds)
	{
		return;
	}

	if (OutContext.State == EPayloadRegressionState::AwaitMaximumEcho)
	{
		MW_LOG(Error, "ex17", "reg FAIL timeout case=maximum limit_ms=%llu", static_cast<unsigned long long>(MaximumRoundTripTimeoutMilliseconds));
	}
	else
	{
		MW_LOG(Error, "ex17", "reg FAIL timeout state=%u", static_cast<unsigned>(OutContext.State));
	}
	OutContext.State = EPayloadRegressionState::Failed;
}

/** Validates one completed peer frame before moving the state machine, using source identity because empty payloads have no sender byte. */
void HandlePayloadRegressionReceive(
	FPayloadRegressionContext& OutContext,
	const MicroWorld::Transport::Address::FDeviceAddress& InFrom,
	const MicroWorld::Transport::Device::FReceiveResult& InReceived,
	const std::uint8_t* const InPayload,
	const std::uint64_t InNowMilliseconds) noexcept
{
	const MicroWorld::Example17::EPayloadRegressionCase ExpectedCase = ExpectedPayloadRegressionCase(OutContext.State);
	const std::uint8_t FromNodeId = MicroWorld::Transport::LoraAddressNodeId(InFrom);
	const bool bRepeatedEmpty = OutContext.State == EPayloadRegressionState::AwaitTypical && FromNodeId == PeerNodeId
		&& MicroWorld::Example17::IsCanonicalPayload(MicroWorld::Example17::EPayloadRegressionCase::Empty, InPayload, InReceived.BytesReceived);
	if (bRepeatedEmpty)
	{
		MW_LOG(
			Log,
			"ex17",
			"reg rx retry case=empty bytes=%u from=%u",
			static_cast<unsigned>(InReceived.BytesReceived),
			static_cast<unsigned>(FromNodeId));
		OutContext.State = EPayloadRegressionState::ResendEmptyEcho;
		return;
	}

	const bool bExpectedFrame = IsAwaitingPayloadRegressionReceive(OutContext.State) && FromNodeId == PeerNodeId
		&& MicroWorld::Example17::IsCanonicalPayload(ExpectedCase, InPayload, InReceived.BytesReceived);
	if (!bExpectedFrame)
	{
		MW_LOG(
			Error,
			"ex17",
			"reg FAIL rx expected=%s bytes=%u from=%u state=%u",
			MicroWorld::Example17::PayloadRegressionLabel(ExpectedCase),
			static_cast<unsigned>(InReceived.BytesReceived),
			static_cast<unsigned>(FromNodeId),
			static_cast<unsigned>(OutContext.State));
		OutContext.State = EPayloadRegressionState::Failed;
		return;
	}

	if (OutContext.State == EPayloadRegressionState::AwaitEmpty)
	{
		MW_LOG(Log, "ex17", "reg rx case=empty bytes=%u from=%u", static_cast<unsigned>(InReceived.BytesReceived), static_cast<unsigned>(FromNodeId));
		OutContext.State = EPayloadRegressionState::SendEmptyEcho;
		return;
	}
	if (OutContext.State == EPayloadRegressionState::AwaitTypical)
	{
		MW_LOG(
			Log, "ex17", "reg rx case=typical bytes=%u from=%u", static_cast<unsigned>(InReceived.BytesReceived), static_cast<unsigned>(FromNodeId));
		OutContext.State = EPayloadRegressionState::SendTypicalEcho;
		return;
	}

	const std::uint64_t RoundTripMilliseconds = InNowMilliseconds - OutContext.MaximumSendAcceptedMilliseconds;
	const bool bPassed = RoundTripMilliseconds <= MaximumRoundTripTimeoutMilliseconds;
	MW_LOG(
		Log,
		"ex17",
		"reg rx case=maximum bytes=%u from=%u rtt_ms=%llu pass=%u",
		static_cast<unsigned>(InReceived.BytesReceived),
		static_cast<unsigned>(FromNodeId),
		static_cast<unsigned long long>(RoundTripMilliseconds),
		bPassed ? 1u : 0u);
	if (bPassed)
	{
		MW_LOG(Log, "ex17", "reg PASS cases=3");
		OutContext.State = EPayloadRegressionState::Passed;
		return;
	}

	MW_LOG(
		Error,
		"ex17",
		"reg FAIL rtt_ms=%llu limit_ms=%llu",
		static_cast<unsigned long long>(RoundTripMilliseconds),
		static_cast<unsigned long long>(MaximumRoundTripTimeoutMilliseconds));
	OutContext.State = EPayloadRegressionState::Failed;
}

/** Pulls at most one completed frame from the device so radio polling remains bounded by the transport contract. */
void ReceivePayloadRegressionFrame(
	MicroWorld::FEsp32LoraDevice& InDevice,
	FPayloadRegressionContext& OutContext,
	std::uint8_t* const InOutRxBuffer,
	const std::uint64_t InNowMilliseconds) noexcept
{
	MicroWorld::Transport::Address::FDeviceAddress From{};
	MicroWorld::Transport::Device::FReceiveResult Received{};
	const MicroWorld::Transport::ETransportResult RxResult =
		InDevice.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(InOutRxBuffer, MicroWorld::Transport::E32MaxPayloadBytes), Received);
	if (RxResult == MicroWorld::Transport::ETransportResult::Success)
	{
		HandlePayloadRegressionReceive(OutContext, From, Received, InOutRxBuffer, InNowMilliseconds);
		return;
	}
	if (RxResult != MicroWorld::Transport::ETransportResult::Unavailable)
	{
		MW_LOG(Error, "ex17", "reg FAIL receive result=%s", ToText(RxResult));
		OutContext.State = EPayloadRegressionState::Failed;
	}
}

/** Queues one canonical echo or maximum frame, preserving the device's non-blocking retry behavior when its slot is full. */
void QueuePayloadRegressionFrame(
	MicroWorld::FEsp32LoraDevice& InDevice,
	FPayloadRegressionContext& OutContext,
	std::uint8_t (&InOutTxBuffer)[MicroWorld::Transport::E32MaxPayloadBytes],
	const MicroWorld::Example17::EPayloadRegressionCase InCase,
	const std::uint64_t InNowMilliseconds) noexcept
{
	MicroWorld::Example17::FillCanonicalPayload(InCase, InOutTxBuffer);
	const std::size_t PayloadBytes = MicroWorld::Example17::PayloadRegressionByteCount(InCase);
	const MicroWorld::Transport::ETransportResult TxResult = InDevice.TrySend(
		MicroWorld::Transport::MakeLoraAddress(PeerNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(InOutTxBuffer, PayloadBytes));
	if (InCase == MicroWorld::Example17::EPayloadRegressionCase::Maximum)
	{
		MW_LOG(
			Log,
			"ex17",
			"reg tx case=maximum bytes=%u to=%u result=%s start_ms=%llu",
			static_cast<unsigned>(PayloadBytes),
			static_cast<unsigned>(PeerNodeId),
			ToText(TxResult),
			static_cast<unsigned long long>(InNowMilliseconds));
	}
	else
	{
		MW_LOG(
			Log,
			"ex17",
			"reg tx case=%s bytes=%u to=%u result=%s",
			MicroWorld::Example17::PayloadRegressionLabel(InCase),
			static_cast<unsigned>(PayloadBytes),
			static_cast<unsigned>(PeerNodeId),
			ToText(TxResult));
	}
	if (TxResult != MicroWorld::Transport::ETransportResult::Success)
	{
		return;
	}

	if (OutContext.State == EPayloadRegressionState::SendEmptyEcho)
	{
		OutContext.State = EPayloadRegressionState::AwaitTypical;
		OutContext.StateDeadlineMilliseconds = InNowMilliseconds + RegressionStepTimeoutMilliseconds;
		return;
	}
	if (OutContext.State == EPayloadRegressionState::ResendEmptyEcho)
	{
		OutContext.State = EPayloadRegressionState::AwaitTypical;
		return;
	}
	if (OutContext.State == EPayloadRegressionState::SendTypicalEcho)
	{
		OutContext.State = EPayloadRegressionState::ScheduleMaximum;
		OutContext.MaximumSendDueMilliseconds = InNowMilliseconds + VolleyPeriodMilliseconds;
		OutContext.StateDeadlineMilliseconds = OutContext.MaximumSendDueMilliseconds + RegressionStepTimeoutMilliseconds;
		MW_LOG(Log, "ex17", "reg schedule case=maximum due_ms=%llu", static_cast<unsigned long long>(OutContext.MaximumSendDueMilliseconds));
		return;
	}

	OutContext.State = EPayloadRegressionState::AwaitMaximumEcho;
	OutContext.MaximumSendAcceptedMilliseconds = InNowMilliseconds;
	OutContext.StateDeadlineMilliseconds = InNowMilliseconds + MaximumRoundTripTimeoutMilliseconds;
}

/** Selects the next outbound case without delaying the polling loop while the required maximum-frame period elapses. */
void SendPayloadRegressionFrame(
	MicroWorld::FEsp32LoraDevice& InDevice,
	FPayloadRegressionContext& OutContext,
	std::uint8_t (&InOutTxBuffer)[MicroWorld::Transport::E32MaxPayloadBytes],
	const std::uint64_t InNowMilliseconds) noexcept
{
	if (OutContext.State == EPayloadRegressionState::ScheduleMaximum && InNowMilliseconds >= OutContext.MaximumSendDueMilliseconds)
	{
		OutContext.State = EPayloadRegressionState::SendMaximum;
	}

	if (OutContext.State == EPayloadRegressionState::SendEmptyEcho || OutContext.State == EPayloadRegressionState::ResendEmptyEcho)
	{
		QueuePayloadRegressionFrame(InDevice, OutContext, InOutTxBuffer, MicroWorld::Example17::EPayloadRegressionCase::Empty, InNowMilliseconds);
		return;
	}
	if (OutContext.State == EPayloadRegressionState::SendTypicalEcho)
	{
		QueuePayloadRegressionFrame(InDevice, OutContext, InOutTxBuffer, MicroWorld::Example17::EPayloadRegressionCase::Typical, InNowMilliseconds);
		return;
	}
	if (OutContext.State == EPayloadRegressionState::SendMaximum)
	{
		QueuePayloadRegressionFrame(InDevice, OutContext, InOutTxBuffer, MicroWorld::Example17::EPayloadRegressionCase::Maximum, InNowMilliseconds);
	}
}

/** Advances one bounded regression iteration; physical UART progress remains owned by the composition loop. */
void AdvancePayloadRegression(
	MicroWorld::FEsp32LoraDevice& InDevice,
	FPayloadRegressionContext& OutContext,
	std::uint8_t* const InOutRxBuffer,
	std::uint8_t (&InOutTxBuffer)[MicroWorld::Transport::E32MaxPayloadBytes],
	const std::uint64_t InNowMilliseconds) noexcept
{
	if (IsPayloadRegressionTerminal(OutContext.State))
	{
		return;
	}

	HandlePayloadRegressionTimeout(OutContext, InNowMilliseconds);
	if (IsPayloadRegressionTerminal(OutContext.State))
	{
		return;
	}

	ReceivePayloadRegressionFrame(InDevice, OutContext, InOutRxBuffer, InNowMilliseconds);
	if (IsPayloadRegressionTerminal(OutContext.State))
	{
		return;
	}

	SendPayloadRegressionFrame(InDevice, OutContext, InOutTxBuffer, InNowMilliseconds);
}
#endif
} // namespace

/** Composition root: installs the output device, then runs the selected bounded LoRa exchange with the peer board. */
extern "C" void app_main(void)
{
	MicroWorld::Core::SetOutputDevice(&MicroWorld::WriteEsp32LogRecord);

	// Static, never on the app_main stack (the ESP32-S3 stack lesson, §2.2).
	static MicroWorld::FEsp32LoraDevice Device{MakeLoraConfig(LocalNodeId)};
	MW_LOG(Log, "ex17", "node=%u open=%d", static_cast<unsigned>(LocalNodeId), Device.IsOpen() ? 1 : 0);
	if (!Device.IsOpen())
	{
		// A failed UART open cannot recover here; stop with a clear line instead of looping.
		MW_LOG(Error, "ex17", "uart failed to open; halting");
		return;
	}

	static std::uint8_t RxBuffer[MicroWorld::Transport::E32MaxPayloadBytes];

#if defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
	// The fixed buffers protect the example's main task stack while the helper owns bounded protocol state.
	static std::uint8_t TxBuffer[MicroWorld::Transport::E32MaxPayloadBytes];
	FPayloadRegressionContext Context{};
	Context.StateDeadlineMilliseconds = GTimeSource.Now() + RegressionStepTimeoutMilliseconds;

	for (;;)
	{
		const std::uint64_t Now = GTimeSource.Now();
		AdvancePayloadRegression(Device, Context, RxBuffer, TxBuffer, Now);
		Device.AdvanceTransmit();
		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}
#else
	// Node 1 seeds the volley one period after boot; node 2 stays idle until it hears frame 1.
	bool bHasPendingTx = (LocalNodeId == VolleyInitiatorNodeId);
	std::uint32_t PendingCounter = 1;
	std::uint64_t PendingDueMilliseconds = GTimeSource.Now() + VolleyPeriodMilliseconds;

	for (;;)
	{
		const std::uint64_t Now = GTimeSource.Now();

		// Receive at most one frame; a completed volley schedules the reply (counter + 1).
		MicroWorld::Transport::Address::FDeviceAddress From{};
		MicroWorld::Transport::Device::FReceiveResult Received{};
		const MicroWorld::Transport::ETransportResult RxResult =
			Device.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(RxBuffer, sizeof(RxBuffer)), Received);
		if (RxResult == MicroWorld::Transport::ETransportResult::Success && Received.BytesReceived == VolleyPayloadBytes)
		{
			const std::uint32_t Counter = ReadVolleyCounter(RxBuffer);
			const std::uint8_t FromId = MicroWorld::Transport::LoraAddressNodeId(From);
			MW_LOG(Log, "ex17", "rx n=%u from=%u", static_cast<unsigned>(Counter), static_cast<unsigned>(FromId));
			bHasPendingTx = true;
			PendingCounter = Counter + 1;
			PendingDueMilliseconds = Now + VolleyPeriodMilliseconds;
		}

		// When the reply is due, send it; keep it pending until a send actually succeeds.
		if (bHasPendingTx && Now >= PendingDueMilliseconds)
		{
			std::uint8_t Payload[VolleyPayloadBytes];
			WriteVolleyPayload(Payload, LocalNodeId, PendingCounter);
			const MicroWorld::Transport::ETransportResult TxResult = Device.TrySend(
				MicroWorld::Transport::MakeLoraAddress(PeerNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			MW_LOG(Log, "ex17", "tx n=%u result=%s", static_cast<unsigned>(PendingCounter), ToText(TxResult));
			if (TxResult == MicroWorld::Transport::ETransportResult::Success)
			{
				bHasPendingTx = false;
			}
		}

		// Physical UART progress is independent of packet acceptance, so a Full slot keeps draining every iteration.
		Device.AdvanceTransmit();

		MicroWorld::SleepMilliseconds(PollPacingMilliseconds);
	}
#endif
}
