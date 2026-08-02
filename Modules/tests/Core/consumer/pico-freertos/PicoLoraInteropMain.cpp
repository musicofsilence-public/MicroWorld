#include <MicroWorld/Transport/Lora/E32Lora.h>
#include <MicroWorld/Core/IO/TransportDevice.h>
#include <MicroWorld/Platform/Pico/PicoLoraDevice.h>

#include <FreeRTOS.h>
#include <task.h>

#include <cstddef>
#include <cstdint>

#if defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
#include "LoraPayloadRegression.h"
#endif

/**
 * Motivation: Satisfies virtual deleting-destructor linkage; the LoRa device is never dynamically deleted.
 * Responsibilities: Provide an empty no-op body so the static-only firmware links without a heap.
 */
void operator delete(void*) noexcept {}

/**
 * Motivation: Satisfies array deleting-destructor linkage without enabling an allocator for this static-only firmware.
 * Responsibilities: Provide an empty no-op body so array-delete references resolve at link time.
 */
void operator delete[](void*) noexcept {}

/**
 * Motivation: Satisfies sized virtual deleting-destructor linkage; static device storage is never reclaimed.
 * Responsibilities: Provide an empty no-op body so the sized delete symbol links without a heap.
 */
void operator delete(void*, std::size_t) noexcept {}

/**
 * Motivation: Satisfies sized array deleting-destructor linkage without creating a heap dependency.
 * Responsibilities: Provide an empty no-op body so the sized array-delete symbol links without a heap.
 */
void operator delete[](void*, std::size_t) noexcept {}

namespace
{

/** Motivation: Identifies the Pico as the node that starts the paired LoRa counter volley. */
/** Motivation: Names the stamp handed to a pre-advance turn taken without a millisecond clock, so the zero reads as deliberate. */
constexpr MicroWorld::Core::TimePointMilliseconds UnpacedPumpTimeMilliseconds{0};

constexpr std::uint8_t LocalNodeId = 1;

/** Motivation: Identifies the unchanged ESP32 example-17 node-B peer. */
constexpr std::uint8_t PeerNodeId = 2;

/** Motivation: Selects RP2040 UART1, whose supported GP4/GP5 routing matches the wired Pico H. */
constexpr std::uint8_t LoraUartIndex = 1;

/** Motivation: Matches the E32-433T20D factory UART rate used by the paired ESP32 example. */
constexpr std::uint32_t LoraBaudRate = 9600;

/** Motivation: Routes Pico UART1 transmit output to the wired E32 RXD pin. */
constexpr unsigned int LoraTransmitPin = 4;

/** Motivation: Routes Pico UART1 receive input from the wired E32 TXD pin. */
constexpr unsigned int LoraReceivePin = 5;

/** Motivation: Leaves enough air time between alternating transparent-mode E32 messages. */
constexpr TickType_t VolleyPeriodTicks = pdMS_TO_TICKS(1000);

/** Motivation: Paces UART polling and one-byte transmit advancement without busy-spinning the task. */
constexpr TickType_t PollPeriodTicks = pdMS_TO_TICKS(10);

/** Motivation: Reserves a fixed stack for the sole LoRa task through the firmware lifetime. */
constexpr configSTACK_DEPTH_TYPE LoraTaskStackDepth = 512;

/** Motivation: Fails the task before an unmeasured stack margin can silently become a radio failure. */
constexpr UBaseType_t MinimumStackHeadroomWords = 128;

/** Motivation: Configures the reusable device with the exact UART wiring proven by the Pico-to-ESP32 exchange. */
constexpr MicroWorld::Platform::Pico::FPicoE32LoraConfig LoraConfig{
	LoraUartIndex,
	LoraTransmitPin,
	LoraReceivePin,
	LoraBaudRate,
	LocalNodeId,
};

/**
 * Motivation: Reports whether `InNow` has reached `InDue`, including the normal tick-counter wraparound case.
 * Responsibilities: Succeed only when the tick delta is non-negative, so wraparound reads as elapsed.
 */
bool IsTickDue(const TickType_t InNow, const TickType_t InDue) noexcept
{
	return static_cast<std::int32_t>(InNow - InDue) >= 0;
}

#if !defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/** Motivation: Preserves the node-id plus big-endian counter payload shared with example 17. */
constexpr std::size_t VolleyPayloadBytes = 5;

/**
 * Motivation: Writes the shared five-byte node-and-counter payload without allocating.
 * Responsibilities: Lay out the fixed five bytes in the example-17 big-endian order.
 */
void WriteVolleyPayload(std::uint8_t (&OutPayload)[VolleyPayloadBytes], const std::uint8_t InNodeId, const std::uint32_t InCounter) noexcept
{
	OutPayload[0] = InNodeId;
	OutPayload[1] = static_cast<std::uint8_t>(InCounter >> 24u);
	OutPayload[2] = static_cast<std::uint8_t>(InCounter >> 16u);
	OutPayload[3] = static_cast<std::uint8_t>(InCounter >> 8u);
	OutPayload[4] = static_cast<std::uint8_t>(InCounter);
}

/**
 * Motivation: Reads the big-endian counter field after the caller validated the volley payload shape.
 * Responsibilities: Assemble the four payload bytes into one u32 in shared big-endian order.
 */
std::uint32_t ReadVolleyCounter(const std::uint8_t* const InPayload) noexcept
{
	return (static_cast<std::uint32_t>(InPayload[1]) << 24u) | (static_cast<std::uint32_t>(InPayload[2]) << 16u)
		| (static_cast<std::uint32_t>(InPayload[3]) << 8u) | static_cast<std::uint32_t>(InPayload[4]);
}
#endif

/** Motivation: Owns FreeRTOS metadata for the one LoRa task. */
StaticTask_t LoraTaskControlBlock;

/** Motivation: Owns the static stack consumed by the LoRa task. */
StackType_t LoraTaskStack[LoraTaskStackDepth];

#if !defined(MICROWORLD_LORA_PAYLOAD_REGRESSION)
/**
 * Motivation: Checks one decoded packet is the expected five-byte reply from ESP32 node B.
 * Responsibilities: Return true only when the sender, length, and node byte all match the peer reply.
 */
bool IsExpectedPeerPayload(
	const MicroWorld::Core::FDeviceAddress& InFrom,
	const MicroWorld::Core::FReceiveResult& InResult,
	const std::uint8_t (&InPayload)[MicroWorld::Transport::E32MaxPayloadBytes]) noexcept
{
	return MicroWorld::Transport::IsLoraAddress(InFrom) && MicroWorld::Transport::LoraAddressNodeId(InFrom) == PeerNodeId
		&& InResult.BytesReceived == VolleyPayloadBytes && InPayload[0] == PeerNodeId;
}

/**
 * Motivation: Drives the Pico node-1 counter volley and continuously checks its measured task-stack margin.
 * Responsibilities: Alternate send and receive with the ESP32 peer and assert the stack headroom each loop.
 */
void RunLoraInteropTask(void* const InContext)
{
	auto& LoraDevice = *static_cast<MicroWorld::Platform::Pico::FPicoLoraDevice*>(InContext);
	std::uint8_t ReceiveBuffer[MicroWorld::Transport::E32MaxPayloadBytes]{};
	bool bHasPendingTransmit = true;
	std::uint32_t PendingCounter = 1;
	TickType_t PendingTransmitDue = xTaskGetTickCount() + VolleyPeriodTicks;

	for (;;)
	{
		// This task counts FreeRTOS ticks, not milliseconds, and the E32 radio paces nothing by the clock, so passing a
		// converted tick would assert a precision the device never reads. The turn is what matters here, not its stamp.
		LoraDevice.PreAdvance(UnpacedPumpTimeMilliseconds);

		MicroWorld::Core::FDeviceAddress From{};
		MicroWorld::Core::FReceiveResult Received{};
		const MicroWorld::Core::ETransportResult ReceiveResult =
			LoraDevice.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(ReceiveBuffer, sizeof(ReceiveBuffer)), Received);
		const TickType_t Now = xTaskGetTickCount();
		if (ReceiveResult == MicroWorld::Core::ETransportResult::Success && IsExpectedPeerPayload(From, Received, ReceiveBuffer))
		{
			bHasPendingTransmit = true;
			PendingCounter = ReadVolleyCounter(ReceiveBuffer) + 1u;
			PendingTransmitDue = Now + VolleyPeriodTicks;
		}

		if (bHasPendingTransmit && IsTickDue(Now, PendingTransmitDue))
		{
			std::uint8_t Payload[VolleyPayloadBytes]{};
			WriteVolleyPayload(Payload, LocalNodeId, PendingCounter);
			const MicroWorld::Core::ETransportResult SendResult = LoraDevice.TrySend(
				MicroWorld::Transport::MakeLoraAddress(PeerNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(Payload, sizeof(Payload)));
			if (SendResult == MicroWorld::Core::ETransportResult::Success)
			{
				// Success transfers the complete frame into the device slot; later task iterations advance it onto UART.
				bHasPendingTransmit = false;
			}
		}

		configASSERT(uxTaskGetStackHighWaterMark(nullptr) >= MinimumStackHeadroomWords);
		vTaskDelay(PollPeriodTicks);
	}
}
#else
/**
 * Motivation: Distinguishes the Pico's bounded regression exchanges so only the expected peer frame can advance them.
 * Responsibilities: Name each step of the empty/typical/maximum volley and never let an out-of-order frame advance it.
 * Example:
 *   EPayloadRegressionState State = EPayloadRegressionState::SendEmpty;
 */
enum class EPayloadRegressionState : std::uint8_t
{
	SendEmpty,		  ///< Motivation: Queue the empty-frame case to start the regression volley.
	AwaitEmptyEcho,	  ///< Motivation: Wait for the peer's echo of the empty frame before advancing.
	SendTypical,	  ///< Motivation: Queue the typical-size frame once the empty echo arrives.
	AwaitTypicalEcho, ///< Motivation: Wait for the peer's echo of the typical frame.
	AwaitMaximum,	  ///< Motivation: Wait for the peer's maximum-size frame after the typical echo.
	EchoMaximum		  ///< Motivation: Echo the maximum-size frame back to the peer to close the exchange.
};

/** Motivation: Requires an empty-frame retry to wait one complete transparent-radio airtime period. */
constexpr TickType_t EmptyRetryPeriodTicks = VolleyPeriodTicks;

/**
 * Motivation: Checks the sender address, exact length, and canonical bytes before accepting one regression frame.
 * Responsibilities: Return true only when the peer address and the canonical case bytes both match.
 */
bool IsExpectedRegressionPayload(
	const MicroWorld::Core::FDeviceAddress& InFrom,
	const MicroWorld::Core::FReceiveResult& InResult,
	const std::uint8_t (&InPayload)[MicroWorld::Transport::E32MaxPayloadBytes],
	const MicroWorld::Example17::EPayloadRegressionCase InExpectedCase) noexcept
{
	return MicroWorld::Transport::IsLoraAddress(InFrom) && MicroWorld::Transport::LoraAddressNodeId(InFrom) == PeerNodeId
		&& MicroWorld::Example17::IsCanonicalPayload(InExpectedCase, InPayload, InResult.BytesReceived);
}

/**
 * Motivation: Advances only when the received peer frame matches the payload case expected by the current exchange state.
 * Responsibilities: Move the state forward on a matching echo and leave it untouched on anything else.
 */
void AdvancePayloadRegressionReceiveState(
	EPayloadRegressionState& InOutState,
	const MicroWorld::Core::FDeviceAddress& InFrom,
	const MicroWorld::Core::FReceiveResult& InResult,
	const std::uint8_t (&InPayload)[MicroWorld::Transport::E32MaxPayloadBytes]) noexcept
{
	switch (InOutState)
	{
		case EPayloadRegressionState::AwaitEmptyEcho:
			if (IsExpectedRegressionPayload(InFrom, InResult, InPayload, MicroWorld::Example17::EPayloadRegressionCase::Empty))
			{
				InOutState = EPayloadRegressionState::SendTypical;
			}
			return;
		case EPayloadRegressionState::AwaitTypicalEcho:
			if (IsExpectedRegressionPayload(InFrom, InResult, InPayload, MicroWorld::Example17::EPayloadRegressionCase::Typical))
			{
				InOutState = EPayloadRegressionState::AwaitMaximum;
			}
			return;
		case EPayloadRegressionState::AwaitMaximum:
			if (IsExpectedRegressionPayload(InFrom, InResult, InPayload, MicroWorld::Example17::EPayloadRegressionCase::Maximum))
			{
				InOutState = EPayloadRegressionState::EchoMaximum;
			}
			return;
		default:
			return;
	}
}

/**
 * Motivation: Queues one canonical frame and advances only after the device accepted it into its bounded transmit slot.
 * Responsibilities: Fill the canonical payload, send it, and move the state only on a successful device accept.
 */
void QueuePendingPayloadRegressionTransmit(
	MicroWorld::Platform::Pico::FPicoLoraDevice& InDevice,
	EPayloadRegressionState& InOutState,
	TickType_t& OutEmptyRetryDue,
	const TickType_t InNow,
	std::uint8_t (&InOutPayloadBuffer)[MicroWorld::Transport::E32MaxPayloadBytes]) noexcept
{
	MicroWorld::Example17::EPayloadRegressionCase TransmitCase = MicroWorld::Example17::EPayloadRegressionCase::Empty;
	switch (InOutState)
	{
		case EPayloadRegressionState::SendEmpty:
			break;
		case EPayloadRegressionState::SendTypical:
			TransmitCase = MicroWorld::Example17::EPayloadRegressionCase::Typical;
			break;
		case EPayloadRegressionState::EchoMaximum:
			TransmitCase = MicroWorld::Example17::EPayloadRegressionCase::Maximum;
			break;
		default:
			return;
	}

	MicroWorld::Example17::FillCanonicalPayload(TransmitCase, InOutPayloadBuffer);
	const std::size_t PayloadBytes = MicroWorld::Example17::PayloadRegressionByteCount(TransmitCase);
	const MicroWorld::Core::ETransportResult SendResult = InDevice.TrySend(
		MicroWorld::Transport::MakeLoraAddress(PeerNodeId), MicroWorld::Core::TSpan<const std::uint8_t>(InOutPayloadBuffer, PayloadBytes));
	if (SendResult != MicroWorld::Core::ETransportResult::Success)
	{
		return;
	}

	switch (InOutState)
	{
		case EPayloadRegressionState::SendEmpty:
			InOutState = EPayloadRegressionState::AwaitEmptyEcho;
			OutEmptyRetryDue = InNow + EmptyRetryPeriodTicks;
			return;
		case EPayloadRegressionState::SendTypical:
			InOutState = EPayloadRegressionState::AwaitTypicalEcho;
			return;
		case EPayloadRegressionState::EchoMaximum:
			InOutState = EPayloadRegressionState::AwaitMaximum;
			return;
		default:
			return;
	}
}

/**
 * Motivation: Drives the Pico peer through the fixed empty, typical, and maximum E32 payload exchange.
 * Responsibilities: Step the regression state machine on each receive/transmit tick and assert the stack headroom.
 */
void RunLoraInteropTask(void* const InContext)
{
	auto& LoraDevice = *static_cast<MicroWorld::Platform::Pico::FPicoLoraDevice*>(InContext);
	std::uint8_t PayloadBuffer[MicroWorld::Transport::E32MaxPayloadBytes]{};
	EPayloadRegressionState State = EPayloadRegressionState::SendEmpty;
	TickType_t EmptyRetryDue = xTaskGetTickCount();

	for (;;)
	{
		MicroWorld::Core::FDeviceAddress From{};
		MicroWorld::Core::FReceiveResult Received{};
		const MicroWorld::Core::ETransportResult ReceiveResult =
			LoraDevice.TryReceive(From, MicroWorld::Core::TSpan<std::uint8_t>(PayloadBuffer, sizeof(PayloadBuffer)), Received);
		if (ReceiveResult == MicroWorld::Core::ETransportResult::Success)
		{
			AdvancePayloadRegressionReceiveState(State, From, Received, PayloadBuffer);
		}

		const TickType_t Now = xTaskGetTickCount();
		if (State == EPayloadRegressionState::AwaitEmptyEcho && IsTickDue(Now, EmptyRetryDue))
		{
			State = EPayloadRegressionState::SendEmpty;
		}

		QueuePendingPayloadRegressionTransmit(LoraDevice, State, EmptyRetryDue, Now, PayloadBuffer);
		LoraDevice.PreAdvance(Now);

		configASSERT(uxTaskGetStackHighWaterMark(nullptr) >= MinimumStackHeadroomWords);
		vTaskDelay(PollPeriodTicks);
	}
}
#endif

} // namespace

/**
 * Motivation: Initializes the reusable device, creates the static LoRa task, and transfers control to FreeRTOS.
 * Responsibilities: Initialize the device, create the static task, and start the scheduler.
 */
int main()
{
	MicroWorld::Platform::Pico::FPicoLoraDevice LoraDevice;
	if (LoraDevice.Initialize(LoraConfig) != MicroWorld::Core::ETransportResult::Success)
	{
		return 1;
	}

	TaskHandle_t LoraTask = xTaskCreateStatic(
		RunLoraInteropTask, "MicroWorldLora", LoraTaskStackDepth, &LoraDevice, tskIDLE_PRIORITY + 1, LoraTaskStack, &LoraTaskControlBlock);
	if (LoraTask == nullptr)
	{
		return 2;
	}

	vTaskStartScheduler();
	return 3;
}
